/**
 * @file test_auth_2fa.c
 * @brief Unit tests for P3-03 2FA login (account.getPassword +
 *        auth.checkPassword SRP proof).
 *
 * Uses the mock crypto/socket backends. SRP math is only exercised for
 * call-count / argument-shape correctness here — known-answer verification
 * against OpenSSL lives in tests/functional/test_srp_functional.c.
 */

#include "test_helpers.h"
#include "infrastructure/auth_2fa.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t zeros24[24] = {0}; tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);
    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0}; tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    /* abridged transport framing: length in dwords. If < 0x7F we emit one
     * byte; otherwise 0x7F + 3-byte LE dword count (matches transport.c). */
    size_t dwords = w.len / 4;
    size_t off = 0;
    if (dwords < 0x7F) {
        out[0] = (uint8_t)dwords;
        off = 1;
    } else {
        out[0] = 0x7F;
        out[1] = (uint8_t)(dwords);
        out[2] = (uint8_t)(dwords >> 8);
        out[3] = (uint8_t)(dwords >> 16);
        off = 4;
    }
    memcpy(out + off, w.data, w.len);
    *out_len = off + w.len;
    tl_writer_free(&w);
}

static void fix_session(MtProtoSession *s) {
    mtproto_session_init(s);
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
    uint8_t k[256] = {0}; mtproto_session_set_auth_key(s, k);
    mtproto_session_set_salt(s, 0xBADCAFEDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* Build an account.password payload with has_password set. */
#define CRC_account_password TL_account_password
#define CRC_KdfAlgo TL_passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow
static size_t make_account_password(uint8_t *buf, size_t max,
                                    int has_pw, int64_t srp_id) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_account_password);
    uint32_t flags = has_pw ? (1u << 2) : 0;
    tl_write_uint32(&w, flags);

    if (has_pw) {
        /* current_algo: passwordKdfAlgoSHA... salt1:bytes salt2:bytes g:int p:bytes */
        tl_write_uint32(&w, CRC_KdfAlgo);
        uint8_t salt1[16] = {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                             0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20};
        uint8_t salt2[16] = {0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
                             0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30};
        tl_write_bytes(&w, salt1, sizeof(salt1));
        tl_write_bytes(&w, salt2, sizeof(salt2));
        tl_write_int32(&w, 2); /* g */
        uint8_t p[256];
        for (int i = 0; i < 256; i++) p[i] = (uint8_t)(0x80 + (i & 0x7f));
        tl_write_bytes(&w, p, sizeof(p));

        /* srp_B:bytes */
        uint8_t srpB[256];
        for (int i = 0; i < 256; i++) srpB[i] = (uint8_t)(i ^ 0x5A);
        tl_write_bytes(&w, srpB, sizeof(srpB));

        /* srp_id:long */
        tl_write_int64(&w, srp_id);
    }

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_get_password_parses_srp_params(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[1024];
    size_t plen = make_account_password(payload, sizeof(payload), 1,
                                         0x1122334455667788LL);
    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    RpcError err = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, &err);
    ASSERT(rc == 0, "getPassword parses ok");
    ASSERT(pw.has_password == 1, "has_password flag set");
    ASSERT(pw.srp_id == 0x1122334455667788LL, "srp_id captured");
    ASSERT(pw.g == 2, "g captured");
    ASSERT(pw.salt1_len == 16, "salt1 length");
    ASSERT(pw.salt2_len == 16, "salt2 length");
    ASSERT(pw.p[0] == 0x80, "prime first byte captured");
    ASSERT(pw.srp_B[0] == 0x5A, "srp_B first byte captured (0 ^ 0x5A)");
}

static void test_get_password_no_2fa(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[128];
    size_t plen = make_account_password(payload, sizeof(payload), 0, 0);
    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, NULL);
    ASSERT(rc == 0, "no-2FA getPassword parses");
    ASSERT(pw.has_password == 0, "has_password not set");
}

static void test_get_password_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 500);
    tl_write_string(&w, "PASSWORD_TOO_FRESH_3600");
    uint8_t payload[128]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    RpcError err = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, &err);
    ASSERT(rc != 0, "RPC error propagates");
    ASSERT(err.error_code == 500, "error code captured");
}

static void test_check_password_rejects_missing_password_flag(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0}; pw.has_password = 0;
    RpcError err = {0};
    int rc = auth_2fa_check_password(&cfg, &s, &t, &pw, "hunter2",
                                      NULL, &err);
    ASSERT(rc == -1, "check rejects has_password=0");
}

static void test_check_password_uses_pbkdf2_and_bn(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* Mock auth.authorization response so the round-trip succeeds and we
     * can count how many times the mocked primitives were invoked. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);                  /* flags */
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 0);                  /* user flags */
    tl_write_int64 (&w, 42LL);               /* user id */
    uint8_t payload[128]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    pw.has_password = 1;
    pw.srp_id = 0xAABBCCDDEEFF0011LL;
    pw.g = 2;
    pw.salt1_len = 4; pw.salt2_len = 4;
    memcpy(pw.salt1, "s1s1", 4);
    memcpy(pw.salt2, "s2s2", 4);
    for (int i = 0; i < 256; i++) { pw.p[i] = 0xFF; pw.srp_B[i] = 0x7F; }

    int64_t uid = 0;
    RpcError err = {0};
    int rc = auth_2fa_check_password(&cfg, &s, &t, &pw, "hunter2",
                                      &uid, &err);
    ASSERT(rc == 0, "checkPassword returns ok with mocked auth.authorization");
    ASSERT(uid == 42, "user id extracted");
    ASSERT(mock_crypto_pbkdf2_call_count() == 1,
           "PBKDF2 invoked exactly once during x derivation");
    ASSERT(mock_crypto_bn_mod_exp_call_count() >= 3,
           "mod_exp used for A, v and the base^a/x chain");
}

static void test_null_args(void) {
    ASSERT(auth_2fa_get_password(NULL, NULL, NULL, NULL, NULL) == -1,
           "getPassword null args");
    ASSERT(auth_2fa_check_password(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == -1,
           "checkPassword null args");
}

/* Build account.password with salt1 length = SRP_SALT_MAX + 1 (too large). */
static size_t make_account_password_bad_salt(uint8_t *buf, size_t max) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_account_password);
    tl_write_uint32(&w, (1u << 2)); /* has_password */

    tl_write_uint32(&w, CRC_KdfAlgo);
    /* salt1 length = SRP_SALT_MAX + 1 — should trigger the guard. */
    uint8_t big_salt[SRP_SALT_MAX + 1];
    memset(big_salt, 0xAB, sizeof(big_salt));
    tl_write_bytes(&w, big_salt, sizeof(big_salt));
    uint8_t salt2[16] = {0};
    tl_write_bytes(&w, salt2, sizeof(salt2));
    tl_write_int32(&w, 2); /* g */
    uint8_t p[256]; memset(p, 0x80, sizeof(p));
    tl_write_bytes(&w, p, sizeof(p));
    uint8_t srpB[256]; memset(srpB, 0x5A, sizeof(srpB));
    tl_write_bytes(&w, srpB, sizeof(srpB));
    tl_write_int64(&w, 0x1234567890ABCDEFLL);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

/* Build account.password with p length != 256 (e.g. 128 bytes). */
static size_t make_account_password_bad_prime_len(uint8_t *buf, size_t max) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_account_password);
    tl_write_uint32(&w, (1u << 2)); /* has_password */

    tl_write_uint32(&w, CRC_KdfAlgo);
    uint8_t salt1[16] = {0}; tl_write_bytes(&w, salt1, sizeof(salt1));
    uint8_t salt2[16] = {0}; tl_write_bytes(&w, salt2, sizeof(salt2));
    tl_write_int32(&w, 2); /* g */
    /* prime is only 128 bytes — wrong length; must be SRP_PRIME_LEN (256). */
    uint8_t p[128]; memset(p, 0x80, sizeof(p));
    tl_write_bytes(&w, p, sizeof(p));
    uint8_t srpB[256]; memset(srpB, 0x5A, sizeof(srpB));
    tl_write_bytes(&w, srpB, sizeof(srpB));
    tl_write_int64(&w, 0x1234567890ABCDEFLL);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

/* Build account.password with has_password=true but zero-length srp_B. */
static size_t make_account_password_empty_srpB(uint8_t *buf, size_t max) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_account_password);
    tl_write_uint32(&w, (1u << 2)); /* has_password */

    tl_write_uint32(&w, CRC_KdfAlgo);
    uint8_t salt1[16] = {0}; tl_write_bytes(&w, salt1, sizeof(salt1));
    uint8_t salt2[16] = {0}; tl_write_bytes(&w, salt2, sizeof(salt2));
    tl_write_int32(&w, 2); /* g */
    uint8_t p[256]; memset(p, 0x80, sizeof(p));
    tl_write_bytes(&w, p, sizeof(p));
    /* srp_B is empty (zero-length bytes). */
    tl_write_bytes(&w, NULL, 0);
    tl_write_int64(&w, 0x1234567890ABCDEFLL);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_get_password_rejects_oversized_salt(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[2048];
    size_t plen = make_account_password_bad_salt(payload, sizeof(payload));
    uint8_t resp[4096]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, NULL);
    ASSERT(rc == -1, "getPassword rejects salt1 > SRP_SALT_MAX");
}

static void test_get_password_rejects_wrong_prime_len(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[1024];
    size_t plen = make_account_password_bad_prime_len(payload, sizeof(payload));
    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, NULL);
    ASSERT(rc == -1, "getPassword rejects p length != SRP_PRIME_LEN");
}

static void test_get_password_rejects_empty_srpB(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[1024];
    size_t plen = make_account_password_empty_srpB(payload, sizeof(payload));
    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    Account2faPassword pw = {0};
    int rc = auth_2fa_get_password(&cfg, &s, &t, &pw, NULL);
    ASSERT(rc == -1, "getPassword rejects zero-length srp_B");
}

void run_auth_2fa_tests(void) {
    RUN_TEST(test_get_password_parses_srp_params);
    RUN_TEST(test_get_password_no_2fa);
    RUN_TEST(test_get_password_rpc_error);
    RUN_TEST(test_check_password_rejects_missing_password_flag);
    RUN_TEST(test_check_password_uses_pbkdf2_and_bn);
    RUN_TEST(test_null_args);
    RUN_TEST(test_get_password_rejects_oversized_salt);
    RUN_TEST(test_get_password_rejects_wrong_prime_len);
    RUN_TEST(test_get_password_rejects_empty_srpB);
}
