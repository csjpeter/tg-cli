/**
 * @file test_auth.c
 * @brief Unit tests for MTProto auth key generation.
 *
 * Tests PQ factorization, individual DH exchange steps (with mocks),
 * and full auth_key_gen integration flow.
 */

#include "test_helpers.h"
#include "mtproto_auth.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_serial.h"
#include "ige_aes.h"
#include "crypto.h"
#include "mock_crypto.h"
#include "mock_socket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Telegram RSA fingerprint (from telegram_server_key.h) ---- */
#define TEST_RSA_FINGERPRINT 0xc3b42b026ce86b21ULL

/* ---- Helper: wrap TL data in unencrypted MTProto + abridged encoding ---- */

static size_t build_unenc_response(const uint8_t *tl_data, size_t tl_len,
                                    uint8_t *out) {
    /* Unencrypted wire: auth_key_id(8) + msg_id(8) + len(4) + data */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint64(&w, 0);             /* auth_key_id = 0 */
    tl_write_uint64(&w, 0x12345678ULL); /* fake msg_id */
    tl_write_uint32(&w, (uint32_t)tl_len);
    tl_write_raw(&w, tl_data, tl_len);

    /* Abridged encoding: length in 4-byte units */
    size_t wire_units = w.len / 4;
    size_t pos = 0;

    if (wire_units < 0x7F) {
        out[0] = (uint8_t)wire_units;
        pos = 1;
    } else {
        out[0] = 0x7F;
        out[1] = (uint8_t)(wire_units & 0xFF);
        out[2] = (uint8_t)((wire_units >> 8) & 0xFF);
        pos = 3;
    }
    memcpy(out + pos, w.data, w.len);
    pos += w.len;
    tl_writer_free(&w);
    return pos;
}

/* ---- Helper: build ResPQ response ---- */

static size_t build_res_pq(const uint8_t nonce[16],
                            const uint8_t server_nonce[16],
                            const uint8_t *pq_be, size_t pq_be_len,
                            uint64_t fingerprint,
                            uint8_t *out) {
    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, 0x05162463); /* CRC_resPQ */
    tl_write_int128(&tl, nonce);
    tl_write_int128(&tl, server_nonce);
    tl_write_bytes(&tl, pq_be, pq_be_len);
    /* Vector of fingerprints */
    tl_write_uint32(&tl, 0x1cb5c415); /* vector constructor */
    tl_write_uint32(&tl, 1);          /* count = 1 */
    tl_write_uint64(&tl, fingerprint);

    size_t len = build_unenc_response(tl.data, tl.len, out);
    tl_writer_free(&tl);
    return len;
}

/* ---- Helper: build server_DH_params_ok response ---- */

static size_t build_server_dh_params_ok(const uint8_t nonce[16],
                                         const uint8_t server_nonce[16],
                                         const uint8_t new_nonce[32],
                                         int32_t g,
                                         const uint8_t *dh_prime, size_t prime_len,
                                         const uint8_t *g_a, size_t g_a_len,
                                         int32_t server_time,
                                         uint8_t *out) {
    /* Build server_DH_inner_data TL */
    TlWriter inner;
    tl_writer_init(&inner);
    tl_write_uint32(&inner, 0xb5890dba); /* CRC_server_DH_inner_data */
    tl_write_int128(&inner, nonce);
    tl_write_int128(&inner, server_nonce);
    tl_write_int32(&inner, g);
    tl_write_bytes(&inner, dh_prime, prime_len);
    tl_write_bytes(&inner, g_a, g_a_len);
    tl_write_int32(&inner, server_time);

    /* Prepend 20 bytes SHA1 hash (zeros for mock) + pad to 16 */
    size_t data_with_hash_len = 20 + inner.len;
    size_t padded_len = data_with_hash_len;
    if (padded_len % 16 != 0)
        padded_len += 16 - (padded_len % 16);

    uint8_t *plaintext = (uint8_t *)calloc(1, padded_len);
    /* SHA1 hash at front (zeros) */
    memcpy(plaintext + 20, inner.data, inner.len);
    tl_writer_free(&inner);

    /* "Encrypt" with IGE using derived temp key.
     * Since test uses mock crypto (identity block cipher), we need to run
     * aes_ige_encrypt so that the production aes_ige_decrypt roundtrips. */
    uint8_t tmp_key[32], tmp_iv[32];

    /* Derive temp AES from new_nonce + server_nonce (same as production) */
    /* We call the same helpers — crypto_sha1 is mocked (returns zeros),
     * so tmp_key and tmp_iv will be deterministic zeros. */
    /* Build the same temp key/IV that auth_step_parse_dh will derive */
    uint8_t sha1_buf[64];
    uint8_t sha1_out[20];

    /* SHA1(new_nonce + server_nonce) → sha1_a */
    memcpy(sha1_buf, new_nonce, 32);
    memcpy(sha1_buf + 32, server_nonce, 16);
    crypto_sha1(sha1_buf, 48, sha1_out);
    uint8_t sha1_a[20];
    memcpy(sha1_a, sha1_out, 20);

    /* SHA1(server_nonce + new_nonce) → sha1_b */
    memcpy(sha1_buf, server_nonce, 16);
    memcpy(sha1_buf + 16, new_nonce, 32);
    crypto_sha1(sha1_buf, 48, sha1_out);
    uint8_t sha1_b[20];
    memcpy(sha1_b, sha1_out, 20);

    memcpy(tmp_key, sha1_a, 20);
    memcpy(tmp_key + 20, sha1_b, 12);

    /* SHA1(new_nonce + new_nonce) → sha1_c */
    memcpy(sha1_buf, new_nonce, 32);
    memcpy(sha1_buf + 32, new_nonce, 32);
    crypto_sha1(sha1_buf, 64, sha1_out);

    memcpy(tmp_iv, sha1_b + 12, 8);
    memcpy(tmp_iv + 8, sha1_out, 20);
    memcpy(tmp_iv + 28, new_nonce, 4);

    uint8_t *encrypted = (uint8_t *)malloc(padded_len);
    aes_ige_encrypt(plaintext, padded_len, tmp_key, tmp_iv, encrypted);
    free(plaintext);

    /* Build outer TL */
    TlWriter outer;
    tl_writer_init(&outer);
    tl_write_uint32(&outer, 0xd0e8075c); /* CRC_server_DH_params_ok */
    tl_write_int128(&outer, nonce);
    tl_write_int128(&outer, server_nonce);
    tl_write_bytes(&outer, encrypted, padded_len);
    free(encrypted);

    size_t len = build_unenc_response(outer.data, outer.len, out);
    tl_writer_free(&outer);
    return len;
}

/* ---- Helper: build dh_gen_ok response ---- */

static size_t build_dh_gen_ok(const uint8_t nonce[16],
                               const uint8_t server_nonce[16],
                               uint8_t *out) {
    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, 0x3bcbf734); /* CRC_dh_gen_ok */
    tl_write_int128(&tl, nonce);
    tl_write_int128(&tl, server_nonce);
    /* new_nonce_hash1: after QA-12 the auth step verifies this field
     * equals last 16 bytes of SHA1(new_nonce || 0x01 || auth_key_aux_hash).
     * Under mock crypto (crypto_sha1 returns zeros after mock_crypto_reset),
     * the last 16 bytes are zeros — match that to satisfy the check. */
    uint8_t zero_hash[16] = {0};
    tl_write_int128(&tl, zero_hash);

    size_t len = build_unenc_response(tl.data, tl.len, out);
    tl_writer_free(&tl);
    return len;
}

/* ---- Helper: build dh_gen response with custom constructor ---- */

static size_t build_dh_gen_response(uint32_t constructor,
                                     const uint8_t nonce[16],
                                     const uint8_t server_nonce[16],
                                     uint8_t *out) {
    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, constructor);
    tl_write_int128(&tl, nonce);
    tl_write_int128(&tl, server_nonce);
    uint8_t fake_hash[16];
    memset(fake_hash, 0xDD, 16);
    tl_write_int128(&tl, fake_hash);

    size_t len = build_unenc_response(tl.data, tl.len, out);
    tl_writer_free(&tl);
    return len;
}

/* ---- Helper: init transport + session for testing ---- */

static void test_init(Transport *t, MtProtoSession *s) {
    mock_socket_reset();
    mock_crypto_reset();
    transport_init(t);
    transport_connect(t, "localhost", 443);
    mock_socket_clear_sent(); /* clear the 0xEF marker */
    mtproto_session_init(s);
}

/* ======================================================================
 * PQ Factorization Tests (existing)
 * ====================================================================== */

void test_pq_factorize_simple(void) {
    uint32_t p, q;
    int rc = pq_factorize(21, &p, &q);
    ASSERT(rc == 0, "factorize 21 should succeed");
    ASSERT(p == 3, "p should be 3");
    ASSERT(q == 7, "q should be 7");
}

void test_pq_factorize_larger(void) {
    uint32_t p, q;
    int rc = pq_factorize(10403, &p, &q);
    ASSERT(rc == 0, "factorize 10403 should succeed");
    ASSERT(p == 101, "p should be 101");
    ASSERT(q == 103, "q should be 103");
}

void test_pq_factorize_product_of_large_primes(void) {
    uint32_t p, q;
    uint64_t pq = (uint64_t)65537 * 65521;
    int rc = pq_factorize(pq, &p, &q);
    ASSERT(rc == 0, "factorize 4295098177 should succeed");
    ASSERT(p == 65521, "p should be 65521");
    ASSERT(q == 65537, "q should be 65537");
}

void test_pq_factorize_small_primes(void) {
    uint32_t p, q;
    int rc = pq_factorize(6, &p, &q);
    ASSERT(rc == 0, "factorize 6 should succeed");
    ASSERT(p == 2, "p should be 2");
    ASSERT(q == 3, "q should be 3");
}

void test_pq_factorize_unequal_primes(void) {
    uint32_t p, q;
    int rc = pq_factorize(371, &p, &q);
    ASSERT(rc == 0, "factorize 371 should succeed");
    ASSERT(p == 7, "p should be 7");
    ASSERT(q == 53, "q should be 53");
}

void test_pq_factorize_invalid(void) {
    uint32_t p, q;
    ASSERT(pq_factorize(0, &p, &q) == -1, "factorize 0 should fail");
    ASSERT(pq_factorize(1, &p, &q) == -1, "factorize 1 should fail");
    ASSERT(pq_factorize(7, &p, &q) == -1, "factorize prime 7 should fail");
    /* 49 = 7*7: valid factorization (p=7, q=7) */
    ASSERT(pq_factorize(49, &p, &q) == 0, "factorize 49 should succeed");
    ASSERT(p == 7 && q == 7, "49 = 7*7");
}

void test_pq_factorize_null(void) {
    uint32_t p;
    ASSERT(pq_factorize(21, NULL, &p) == -1, "NULL p should fail");
    ASSERT(pq_factorize(21, &p, NULL) == -1, "NULL q should fail");
}

void test_pq_factorize_mtproto_sized(void) {
    uint32_t p, q;
    uint64_t pq = (uint64_t)1073741789ULL * 1073741827ULL;
    int rc = pq_factorize(pq, &p, &q);
    ASSERT(rc == 0, "factorize large product should succeed");
    ASSERT((uint64_t)p * q == pq, "p * q should equal original pq");
    ASSERT(p <= q, "p should be <= q");
}

/* QA-22: reject PQ whose factors would be truncated to 32 bits. We can't
 * easily compute a pq with factors > UINT32_MAX that Pollard's rho will
 * crack quickly, but we can at least assert that pq_factorize rejects
 * a clearly-too-large product by never returning truncated values. Use a
 * product of 33-bit primes and verify p*q equality still holds OR the
 * function returns -1. */
void test_pq_factorize_rejects_wide_factors(void) {
    uint32_t p, q;
    /* 4294967311 is the smallest prime > 2^32. */
    uint64_t p33 = 4294967311ULL;
    /* We need the product to be <= 2^63-1 and factor-able by our rho
     * implementation. Multiply by a small prime instead so the test
     * is tractable. Our implementation will return either a valid
     * (p, q) within uint32 OR refuse with -1 — never a truncated one. */
    uint64_t pq = p33 * 3ULL;
    int rc = pq_factorize(pq, &p, &q);
    if (rc == 0) {
        ASSERT((uint64_t)p * q == pq,
               "returned factors must satisfy p*q == pq without truncation");
    } /* rc == -1 is also acceptable — it's the explicit rejection path. */
}

/* ======================================================================
 * Step 1: auth_step_req_pq tests
 * ====================================================================== */

void test_req_pq_parses_respq(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* The mock crypto_rand_bytes fills nonce with 0xAA */
    uint8_t expected_nonce[16];
    memset(expected_nonce, 0xAA, 16);

    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);

    /* pq = 21 (3 * 7) as big-endian bytes */
    uint8_t pq_be[] = { 0x15 };

    /* Build and program ResPQ response */
    uint8_t resp[4096];
    size_t resp_len = build_res_pq(expected_nonce, server_nonce,
                                    pq_be, sizeof(pq_be),
                                    TEST_RSA_FINGERPRINT, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == 0, "req_pq should succeed");
    ASSERT(ctx.pq == 21, "pq should be 21");
    ASSERT(memcmp(ctx.server_nonce, server_nonce, 16) == 0,
           "server_nonce should match");
    ASSERT(memcmp(ctx.nonce, expected_nonce, 16) == 0,
           "nonce should be 0xAA (from mock rand_bytes)");

    transport_close(&t);
}

void test_req_pq_sends_correct_tl(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16];
    memset(nonce, 0xAA, 16);
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);
    uint8_t pq_be[] = { 0x15 };

    uint8_t resp[4096];
    size_t resp_len = build_res_pq(nonce, server_nonce,
                                    pq_be, sizeof(pq_be),
                                    TEST_RSA_FINGERPRINT, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    auth_step_req_pq(&ctx);

    /* Verify sent data contains req_pq_multi */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len > 0, "should have sent data");

    /* Sent data: abridged prefix + unenc header (20 bytes) + TL payload.
     * TL payload starts with CRC_req_pq_multi = 0xbe7e8ef1 (LE). */
    /* Skip abridged prefix (1 byte) + auth_key_id(8) + msg_id(8) + len(4) = 21 */
    ASSERT(sent_len >= 21 + 20, "sent data should be at least 41 bytes");
    uint32_t constructor;
    memcpy(&constructor, sent + 21, 4);
    ASSERT(constructor == 0xbe7e8ef1, "should send CRC_req_pq_multi");

    transport_close(&t);
}

void test_req_pq_wrong_nonce(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* Build ResPQ with wrong nonce */
    uint8_t wrong_nonce[16];
    memset(wrong_nonce, 0x99, 16); /* 0x99 != 0xAA (mock rand_bytes output) */
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);
    uint8_t pq_be[] = { 0x15 };

    uint8_t resp[4096];
    size_t resp_len = build_res_pq(wrong_nonce, server_nonce,
                                    pq_be, sizeof(pq_be),
                                    TEST_RSA_FINGERPRINT, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "req_pq should fail with wrong nonce");

    transport_close(&t);
}

void test_req_pq_no_fingerprint(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16];
    memset(nonce, 0xAA, 16);
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);
    uint8_t pq_be[] = { 0x15 };

    /* Wrong fingerprint */
    uint8_t resp[4096];
    size_t resp_len = build_res_pq(nonce, server_nonce,
                                    pq_be, sizeof(pq_be),
                                    0xDEADBEEFULL, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "req_pq should fail with wrong fingerprint");

    transport_close(&t);
}

void test_req_pq_wrong_constructor(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* Build response with wrong constructor */
    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, 0xDEADBEEF); /* wrong constructor */
    tl_write_int128(&tl, (uint8_t[16]){0});
    tl_write_int128(&tl, (uint8_t[16]){0});

    uint8_t resp[4096];
    size_t resp_len = build_unenc_response(tl.data, tl.len, resp);
    tl_writer_free(&tl);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "req_pq should fail with wrong constructor");

    transport_close(&t);
}

/* ======================================================================
 * Step 2: auth_step_req_dh tests
 * ====================================================================== */

void test_req_dh_sends_correct_tl(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    ctx.pq = 21; /* 3 * 7 */
    memset(ctx.nonce, 0xAA, 16);
    memset(ctx.server_nonce, 0xBB, 16);

    int rc = auth_step_req_dh(&ctx);
    ASSERT(rc == 0, "req_dh should succeed");

    /* Verify p, q were factorized */
    ASSERT(ctx.p == 3, "p should be 3");
    ASSERT(ctx.q == 7, "q should be 7");

    /* Verify RSA encrypt was called */
    ASSERT(mock_crypto_rsa_encrypt_call_count() == 1,
           "RSA encrypt should be called once");

    /* Verify sent data contains CRC_req_DH_params */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len > 21, "should have sent data");
    uint32_t constructor;
    memcpy(&constructor, sent + 21, 4);
    ASSERT(constructor == 0xd712e4be, "should send CRC_req_DH_params");

    transport_close(&t);
}

void test_req_dh_factorizes_pq(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    ctx.pq = 10403; /* 101 * 103 */
    memset(ctx.nonce, 0xAA, 16);
    memset(ctx.server_nonce, 0xBB, 16);

    int rc = auth_step_req_dh(&ctx);
    ASSERT(rc == 0, "req_dh should succeed");
    ASSERT(ctx.p == 101, "p should be 101");
    ASSERT(ctx.q == 103, "q should be 103");

    transport_close(&t);
}

/* ======================================================================
 * Step 3: auth_step_parse_dh tests
 * ====================================================================== */

void test_parse_dh_success(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16], new_nonce[32];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);
    memset(new_nonce, 0xCC, 32);

    /* Fake DH params */
    uint8_t dh_prime[32];
    memset(dh_prime, 0x11, 32);
    uint8_t g_a[32];
    memset(g_a, 0x22, 32);
    int32_t g = 3;
    int32_t server_time = 1700000000;

    uint8_t resp[8192];
    size_t resp_len = build_server_dh_params_ok(nonce, server_nonce, new_nonce,
                                                 g, dh_prime, 32,
                                                 g_a, 32, server_time, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    memcpy(ctx.new_nonce, new_nonce, 32);

    int rc = auth_step_parse_dh(&ctx);
    ASSERT(rc == 0, "parse_dh should succeed");
    ASSERT(ctx.g == 3, "g should be 3");
    ASSERT(ctx.dh_prime_len == 32, "dh_prime_len should be 32");
    ASSERT(memcmp(ctx.dh_prime, dh_prime, 32) == 0, "dh_prime should match");
    ASSERT(ctx.g_a_len == 32, "g_a_len should be 32");
    ASSERT(memcmp(ctx.g_a, g_a, 32) == 0, "g_a should match");
    ASSERT(ctx.server_time == 1700000000, "server_time should match");

    transport_close(&t);
}

void test_parse_dh_wrong_constructor(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, 0xDEADBEEF); /* wrong constructor */
    tl_write_int128(&tl, (uint8_t[16]){0});
    tl_write_int128(&tl, (uint8_t[16]){0});

    uint8_t resp[4096];
    size_t resp_len = build_unenc_response(tl.data, tl.len, resp);
    tl_writer_free(&tl);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;

    int rc = auth_step_parse_dh(&ctx);
    ASSERT(rc == -1, "parse_dh should fail with wrong constructor");

    transport_close(&t);
}

void test_parse_dh_nonce_mismatch(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* Build response with nonce = 0x11..., but ctx expects 0xAA... */
    uint8_t wrong_nonce[16];
    memset(wrong_nonce, 0x11, 16);
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);

    TlWriter tl;
    tl_writer_init(&tl);
    tl_write_uint32(&tl, 0xd0e8075c); /* CRC_server_DH_params_ok */
    tl_write_int128(&tl, wrong_nonce);
    tl_write_int128(&tl, server_nonce);
    /* encrypted_answer (empty bytes trigger) */
    uint8_t fake_enc[16];
    memset(fake_enc, 0, 16);
    tl_write_bytes(&tl, fake_enc, 16);

    uint8_t resp[4096];
    size_t resp_len = build_unenc_response(tl.data, tl.len, resp);
    tl_writer_free(&tl);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memset(ctx.nonce, 0xAA, 16); /* different from 0x11 */
    memcpy(ctx.server_nonce, server_nonce, 16);

    int rc = auth_step_parse_dh(&ctx);
    ASSERT(rc == -1, "parse_dh should fail with nonce mismatch");

    transport_close(&t);
}

/* ======================================================================
 * Step 4: auth_step_set_client_dh tests
 * ====================================================================== */

void test_set_client_dh_gen_ok(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16], new_nonce[32];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);
    memset(new_nonce, 0xCC, 32);

    uint8_t resp[4096];
    size_t resp_len = build_dh_gen_ok(nonce, server_nonce, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    memcpy(ctx.new_nonce, new_nonce, 32);
    ctx.g = 2;
    /* Fake DH prime and g_a */
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    int rc = auth_step_set_client_dh(&ctx);
    ASSERT(rc == 0, "set_client_dh should succeed");
    ASSERT(s.has_auth_key == 1, "session should have auth_key set");
    ASSERT(s.server_salt != 0, "server_salt should be non-zero");

    /* Verify BN mod exp was called at least twice (g_b + auth_key) */
    ASSERT(mock_crypto_bn_mod_exp_call_count() == 2,
           "bn_mod_exp should be called twice");

    transport_close(&t);
}

/* QA-12: supply a dh_gen_ok response whose new_nonce_hash1 does NOT match
 * the value our client computes. Must be rejected with -1, auth key must
 * remain unset on the session. */
void test_set_client_dh_rejects_bad_new_nonce_hash(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16], new_nonce[32];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);
    memset(new_nonce, 0xCC, 32);

    /* Build dh_gen_ok manually with an INVALID new_nonce_hash. */
    TlWriter tl; tl_writer_init(&tl);
    tl_write_uint32(&tl, 0x3bcbf734); /* CRC_dh_gen_ok */
    tl_write_int128(&tl, nonce);
    tl_write_int128(&tl, server_nonce);
    uint8_t bad_hash[16];
    memset(bad_hash, 0xDE, 16);     /* does not match expected zeros */
    tl_write_int128(&tl, bad_hash);
    uint8_t resp[4096];
    size_t resp_len = build_unenc_response(tl.data, tl.len, resp);
    tl_writer_free(&tl);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    memcpy(ctx.new_nonce, new_nonce, 32);
    ctx.g = 2;
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    int rc = auth_step_set_client_dh(&ctx);
    ASSERT(rc == -1, "QA-12: bad new_nonce_hash1 must be rejected");
    ASSERT(s.has_auth_key == 0, "auth_key must NOT be set on MITM failure");

    transport_close(&t);
}

void test_set_client_dh_sends_tl(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16], new_nonce[32];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);
    memset(new_nonce, 0xCC, 32);

    uint8_t resp[4096];
    size_t resp_len = build_dh_gen_ok(nonce, server_nonce, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    memcpy(ctx.new_nonce, new_nonce, 32);
    ctx.g = 2;
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    auth_step_set_client_dh(&ctx);

    /* Verify sent data contains CRC_set_client_DH_params */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len > 21, "should have sent data");
    uint32_t constructor;
    memcpy(&constructor, sent + 21, 4);
    ASSERT(constructor == 0xf5045f1f, "should send CRC_set_client_DH_params");

    transport_close(&t);
}

void test_set_client_dh_gen_retry(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);

    uint8_t resp[4096];
    size_t resp_len = build_dh_gen_response(0x46dc1fb9, /* dh_gen_retry */
                                             nonce, server_nonce, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    ctx.g = 2;
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    int rc = auth_step_set_client_dh(&ctx);
    ASSERT(rc == -1, "set_client_dh should fail on dh_gen_retry");

    transport_close(&t);
}

void test_set_client_dh_gen_fail(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    uint8_t nonce[16], server_nonce[16];
    memset(nonce, 0xAA, 16);
    memset(server_nonce, 0xBB, 16);

    uint8_t resp[4096];
    size_t resp_len = build_dh_gen_response(0xa69dae02, /* dh_gen_fail */
                                             nonce, server_nonce, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    ctx.g = 2;
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    int rc = auth_step_set_client_dh(&ctx);
    ASSERT(rc == -1, "set_client_dh should fail on dh_gen_fail");

    transport_close(&t);
}

void test_set_client_dh_salt_computation(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* Use specific nonces to verify XOR */
    uint8_t nonce[16];
    memset(nonce, 0xAA, 16);
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);
    uint8_t new_nonce[32];
    memset(new_nonce, 0x11, 32);

    uint8_t resp[4096];
    size_t resp_len = build_dh_gen_ok(nonce, server_nonce, resp);
    mock_socket_set_response(resp, resp_len);

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session = &s;
    memcpy(ctx.nonce, nonce, 16);
    memcpy(ctx.server_nonce, server_nonce, 16);
    memcpy(ctx.new_nonce, new_nonce, 32);
    ctx.g = 2;
    memset(ctx.dh_prime, 0x11, 32);
    ctx.dh_prime_len = 32;
    memset(ctx.g_a, 0x22, 32);
    ctx.g_a_len = 32;

    auth_step_set_client_dh(&ctx);

    /* Expected salt: new_nonce[0:8] XOR server_nonce[0:8]
     * = 0x11 XOR 0xBB = 0xAA for each byte */
    uint8_t expected_salt[8];
    memset(expected_salt, 0xAA, 8);
    uint64_t expected;
    memcpy(&expected, expected_salt, 8);
    ASSERT(s.server_salt == expected,
           "server_salt should be new_nonce XOR server_nonce");

    transport_close(&t);
}

/* ======================================================================
 * Integration test: full auth_key_gen flow
 * ====================================================================== */

void test_auth_key_gen_full_flow(void) {
    Transport t;
    MtProtoSession s;
    test_init(&t, &s);

    /* The nonce will be 0xAA... (from mock rand_bytes) */
    uint8_t nonce[16];
    memset(nonce, 0xAA, 16);
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);
    /* new_nonce will also be 0xAA (mock rand_bytes always returns 0xAA) */
    uint8_t new_nonce[32];
    memset(new_nonce, 0xAA, 32);

    /* pq = 21 (3 * 7) */
    uint8_t pq_be[] = { 0x15 };

    /* Fake DH params */
    uint8_t dh_prime[32];
    memset(dh_prime, 0x11, 32);
    uint8_t g_a[32];
    memset(g_a, 0x22, 32);

    /* Response 1: ResPQ */
    uint8_t resp1[4096];
    size_t resp1_len = build_res_pq(nonce, server_nonce,
                                     pq_be, sizeof(pq_be),
                                     TEST_RSA_FINGERPRINT, resp1);

    /* Response 2: server_DH_params_ok
     * Note: we need to reset sha1 count because build_server_dh_params_ok
     * calls crypto_sha1. But that's OK — we just need the responses queued. */
    uint8_t resp2[8192];
    size_t resp2_len = build_server_dh_params_ok(nonce, server_nonce, new_nonce,
                                                  3, dh_prime, 32,
                                                  g_a, 32, 1700000000, resp2);

    /* Response 3: dh_gen_ok */
    uint8_t resp3[4096];
    size_t resp3_len = build_dh_gen_ok(nonce, server_nonce, resp3);

    /* Queue all responses */
    mock_socket_set_response(resp1, resp1_len);
    mock_socket_append_response(resp2, resp2_len);
    mock_socket_append_response(resp3, resp3_len);

    /* Reset crypto counters after building responses */
    mock_crypto_reset();

    int rc = mtproto_auth_key_gen(&t, &s);
    ASSERT(rc == 0, "auth_key_gen should succeed");
    ASSERT(s.has_auth_key == 1, "session should have auth_key");
    ASSERT(s.server_salt != 0, "server_salt should be non-zero");

    /* Verify crypto usage */
    ASSERT(mock_crypto_rand_bytes_call_count() >= 3,
           "rand_bytes should be called at least 3 times (nonce, new_nonce, b)");
    ASSERT(mock_crypto_rsa_encrypt_call_count() == 1,
           "RSA encrypt should be called once");
    ASSERT(mock_crypto_bn_mod_exp_call_count() == 2,
           "bn_mod_exp should be called twice (g_b and auth_key)");

    transport_close(&t);
}

void test_auth_key_gen_null_args(void) {
    Transport t;
    MtProtoSession s;
    transport_init(&t);
    mtproto_session_init(&s);

    ASSERT(mtproto_auth_key_gen(NULL, &s) == -1, "NULL transport should fail");
    ASSERT(mtproto_auth_key_gen(&t, NULL) == -1, "NULL session should fail");
}

/* ======================================================================
 * Test suite entry point
 * ====================================================================== */

void test_auth(void) {
    /* PQ factorization */
    RUN_TEST(test_pq_factorize_simple);
    RUN_TEST(test_pq_factorize_larger);
    RUN_TEST(test_pq_factorize_product_of_large_primes);
    RUN_TEST(test_pq_factorize_small_primes);
    RUN_TEST(test_pq_factorize_unequal_primes);
    RUN_TEST(test_pq_factorize_invalid);
    RUN_TEST(test_pq_factorize_null);
    RUN_TEST(test_pq_factorize_mtproto_sized);
    RUN_TEST(test_pq_factorize_rejects_wide_factors);

    /* Step 1: req_pq */
    RUN_TEST(test_req_pq_parses_respq);
    RUN_TEST(test_req_pq_sends_correct_tl);
    RUN_TEST(test_req_pq_wrong_nonce);
    RUN_TEST(test_req_pq_no_fingerprint);
    RUN_TEST(test_req_pq_wrong_constructor);

    /* Step 2: req_dh */
    RUN_TEST(test_req_dh_sends_correct_tl);
    RUN_TEST(test_req_dh_factorizes_pq);

    /* Step 3: parse_dh */
    RUN_TEST(test_parse_dh_success);
    RUN_TEST(test_parse_dh_wrong_constructor);
    RUN_TEST(test_parse_dh_nonce_mismatch);

    /* Step 4: set_client_dh */
    RUN_TEST(test_set_client_dh_gen_ok);
    RUN_TEST(test_set_client_dh_rejects_bad_new_nonce_hash);
    RUN_TEST(test_set_client_dh_sends_tl);
    RUN_TEST(test_set_client_dh_gen_retry);
    RUN_TEST(test_set_client_dh_gen_fail);
    RUN_TEST(test_set_client_dh_salt_computation);

    /* Integration */
    RUN_TEST(test_auth_key_gen_full_flow);
    RUN_TEST(test_auth_key_gen_null_args);
}
