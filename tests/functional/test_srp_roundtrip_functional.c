/**
 * @file test_srp_roundtrip_functional.c
 * @brief End-to-end SRP verification under real OpenSSL.
 *
 * The client-side implementation lives in
 * src/infrastructure/auth_2fa.c::srp_compute and derives (A, M1) from
 * the user's password plus server parameters (g, p, B, salts). These
 * tests rebuild an independent "server side" SRP using OpenSSL BN ops
 * directly, then assert that the math invariant
 *
 *     S_client == S_server
 *
 * holds — i.e. both ends arrive at the same shared secret. That's the
 * key correctness property of SRP-6a as adapted by Telegram; if it
 * holds, the client M1 and the server's own expected M1 will match
 * too (both hash the same K = H(S)).
 *
 * Because `crypto_rand_bytes` is real in this binary we pin the
 * client's `a` (private exponent) through the new public
 * auth_2fa_srp_compute(a_priv_in, ...) entry point so the test is
 * deterministic.
 */

#include "test_helpers.h"
#include "infrastructure/auth_2fa.h"
#include "crypto.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/bn.h>
#include <openssl/sha.h>
#pragma GCC diagnostic pop

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* MTProto uses this fixed 2048-bit safe prime. We embed it directly so
 * the test doesn't need a live server. */
static const uint8_t MTPROTO_P[256] = {
    0xC7,0x1C,0xAE,0xB9,0xC6,0xB1,0xC9,0x04,0x8E,0x6C,0x52,0x2F,0x70,0xF1,0x3F,0x73,
    0x98,0x0D,0x40,0x23,0x8E,0x3E,0x21,0xC1,0x49,0x34,0xD0,0x37,0x56,0x3D,0x93,0x0F,
    0x48,0x19,0x8A,0x0A,0xA7,0xC1,0x40,0x58,0x22,0x94,0x93,0xD2,0x25,0x30,0xF4,0xDB,
    0xFA,0x33,0x6F,0x6E,0x0A,0xC9,0x25,0x13,0x95,0x43,0xAE,0xD4,0x4C,0xCE,0x7C,0x37,
    0x20,0xFD,0x51,0xF6,0x94,0x58,0x70,0x5A,0xC6,0x8C,0xD4,0xFE,0x6B,0x6B,0x13,0xAB,
    0xDC,0x97,0x46,0x51,0x29,0x69,0x32,0x84,0x54,0xF1,0x8F,0xAF,0x8C,0x59,0x5F,0x64,
    0x24,0x77,0xFE,0x96,0xBB,0x2A,0x94,0x1D,0x5B,0xCD,0x1D,0x4A,0xC8,0xCC,0x49,0x88,
    0x07,0x08,0xFA,0x9B,0x37,0x8E,0x3C,0x4F,0x3A,0x90,0x60,0xBE,0xE6,0x7C,0xF9,0xA4,
    0xA4,0xA6,0x95,0x81,0x10,0x51,0x90,0x7E,0x16,0x27,0x53,0xB5,0x6B,0x0F,0x6B,0x41,
    0x0D,0xBA,0x74,0xD8,0xA8,0x4B,0x2A,0x14,0xB3,0x14,0x4E,0x0E,0xF1,0x28,0x47,0x54,
    0xFD,0x17,0xED,0x95,0x0D,0x59,0x65,0xB4,0xB9,0xDD,0x46,0x58,0x2D,0xB1,0x17,0x8D,
    0x16,0x9C,0x6B,0xC4,0x65,0xB0,0xD6,0xFF,0x9C,0xA3,0x92,0x8F,0xEF,0x5B,0x9A,0xE4,
    0xE4,0x18,0xFC,0x15,0xE8,0x3E,0xBE,0xA0,0xF8,0x7F,0xA9,0xFF,0x5E,0xED,0x70,0x05,
    0x0D,0xED,0x28,0x49,0xF4,0x7B,0xF9,0x59,0xD9,0x56,0x85,0x0C,0xE9,0x29,0x85,0x1F,
    0x0D,0x81,0x15,0xF6,0x35,0xB1,0x05,0xEE,0x2E,0x4E,0x15,0xD0,0x4B,0x24,0x54,0xBF,
    0x6F,0x4F,0xAD,0xF0,0x34,0xB1,0x04,0x03,0x11,0x9C,0xD8,0xE3,0xB9,0x2F,0xCC,0x5B
};

/* Compute PH2(password, salt1, salt2) following the Telegram spec:
 *   PH1 = SH(SH(password, salt1), salt2)
 *        where SH(data, salt) = SHA256(salt | data | salt)
 *   PH2 = SH(pbkdf2-sha512(PH1, salt1, 100000, 64), salt2) */
static void ph2(const char *password,
                 const uint8_t *salt1, size_t s1,
                 const uint8_t *salt2, size_t s2,
                 uint8_t out_x[32]) {
    size_t plen = strlen(password);
    /* SH(password, salt1) */
    size_t buf_cap = (s1 * 2 + plen > s2 * 2 + 64 ? s1 * 2 + plen : s2 * 2 + 64);
    uint8_t *buf = (uint8_t *)malloc(buf_cap);
    uint8_t inner[32];
    memcpy(buf, salt1, s1);
    memcpy(buf + s1, password, plen);
    memcpy(buf + s1 + plen, salt1, s1);
    crypto_sha256(buf, s1 * 2 + plen, inner);

    /* SH(inner, salt2) = PH1 */
    uint8_t ph1[32];
    memcpy(buf, salt2, s2);
    memcpy(buf + s2, inner, 32);
    memcpy(buf + s2 + 32, salt2, s2);
    crypto_sha256(buf, s2 * 2 + 32, ph1);

    /* PBKDF2 */
    uint8_t pb[64];
    crypto_pbkdf2_hmac_sha512(ph1, 32, salt1, s1, 100000, pb, 64);

    /* SH(pb, salt2) = PH2 */
    memcpy(buf, salt2, s2);
    memcpy(buf + s2, pb, 64);
    memcpy(buf + s2 + 64, salt2, s2);
    crypto_sha256(buf, s2 * 2 + 64, out_x);

    free(buf);
}

/* S_server = (A * v^u)^b mod p, where v = g^x mod p.
 * This is the standard SRP-6a server derivation. */
static void server_compute_S(const uint8_t A[256], const uint8_t *b, size_t blen,
                              const uint8_t *u32, const uint8_t x32[32],
                              int32_t g_int,
                              uint8_t S_out[256]) {
    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *p = BN_bin2bn(MTPROTO_P, 256, NULL);
    BIGNUM *g = BN_new();
    BN_set_word(g, (BN_ULONG)g_int);
    BIGNUM *A_bn = BN_bin2bn(A, 256, NULL);
    BIGNUM *x_bn = BN_bin2bn(x32, 32, NULL);
    BIGNUM *u_bn = BN_bin2bn(u32, 32, NULL);
    BIGNUM *b_bn = BN_bin2bn(b, blen, NULL);

    /* v = g^x mod p */
    BIGNUM *v = BN_new();
    BN_mod_exp(v, g, x_bn, p, ctx);

    /* tmp = v^u mod p */
    BIGNUM *tmp = BN_new();
    BN_mod_exp(tmp, v, u_bn, p, ctx);

    /* tmp = (A * tmp) mod p */
    BN_mod_mul(tmp, A_bn, tmp, p, ctx);

    /* S = tmp^b mod p */
    BIGNUM *S = BN_new();
    BN_mod_exp(S, tmp, b_bn, p, ctx);

    int sz = BN_num_bytes(S);
    memset(S_out, 0, 256);
    BN_bn2bin(S, S_out + (256 - sz));

    BN_free(p); BN_free(g); BN_free(A_bn); BN_free(x_bn);
    BN_free(u_bn); BN_free(b_bn); BN_free(v); BN_free(tmp); BN_free(S);
    BN_CTX_free(ctx);
}

/* Fill the salt/g/p parts of an Account2faPassword, leave srp_B empty. */
static void init_params_shell(Account2faPassword *p, int32_t g) {
    memset(p, 0, sizeof(*p));
    p->has_password = 1;
    p->srp_id = 0xD00DEDBEEFBADF00LL;
    p->g = g;
    memcpy(p->p, MTPROTO_P, 256);
    p->salt1_len = 16; p->salt2_len = 16;
    for (int i = 0; i < 16; i++) {
        p->salt1[i] = (uint8_t)(0xA0 + i);
        p->salt2[i] = (uint8_t)(0x50 + i);
    }
}

/* Compute B = g^b mod p using OpenSSL so the test has a valid server
 * ephemeral; Telegram clients don't care where B came from as long
 * as the math works out. The "k" step in SRP-6a is subsumed on the
 * server side when we recompute S with (A * v^u)^b so we don't need
 * to emit k*v from the server. */
static void server_B(int32_t g_int, const uint8_t *b, size_t blen,
                      uint8_t B_out[256]) {
    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *p = BN_bin2bn(MTPROTO_P, 256, NULL);
    BIGNUM *g = BN_new(); BN_set_word(g, (BN_ULONG)g_int);
    BIGNUM *b_bn = BN_bin2bn(b, blen, NULL);
    BIGNUM *B = BN_new();
    BN_mod_exp(B, g, b_bn, p, ctx);
    int sz = BN_num_bytes(B);
    memset(B_out, 0, 256);
    BN_bn2bin(B, B_out + (256 - sz));
    BN_free(p); BN_free(g); BN_free(b_bn); BN_free(B);
    BN_CTX_free(ctx);
}

/* Telegram's "real" server additionally folds k*v into B before
 * sending it. Reproduce that here so the test matches production
 * semantics: B_wire = (k*v + g^b) mod p. */
static void server_B_with_kv(int32_t g_int, const uint8_t *b, size_t blen,
                              const char *password,
                              const uint8_t *salt1, size_t s1,
                              const uint8_t *salt2, size_t s2,
                              uint8_t B_wire_out[256]) {
    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *p = BN_bin2bn(MTPROTO_P, 256, NULL);
    BIGNUM *g = BN_new(); BN_set_word(g, (BN_ULONG)g_int);
    BIGNUM *b_bn = BN_bin2bn(b, blen, NULL);

    /* x = PH2, v = g^x mod p */
    uint8_t x32[32];
    ph2(password, salt1, s1, salt2, s2, x32);
    BIGNUM *x_bn = BN_bin2bn(x32, 32, NULL);
    BIGNUM *v_bn = BN_new();
    BN_mod_exp(v_bn, g, x_bn, p, ctx);

    /* g_bytes: pad g to 256 bytes (big-endian) */
    uint8_t g_bytes[256]; memset(g_bytes, 0, 256);
    uint32_t gu = (uint32_t)g_int;
    g_bytes[252] = (uint8_t)(gu >> 24);
    g_bytes[253] = (uint8_t)(gu >> 16);
    g_bytes[254] = (uint8_t)(gu >> 8);
    g_bytes[255] = (uint8_t)(gu);
    /* k = SHA256(p | g_bytes) */
    uint8_t k_buf[512]; memcpy(k_buf, MTPROTO_P, 256); memcpy(k_buf + 256, g_bytes, 256);
    uint8_t k32[32]; crypto_sha256(k_buf, sizeof(k_buf), k32);
    BIGNUM *k_bn = BN_bin2bn(k32, 32, NULL);

    /* kv = k*v mod p */
    BIGNUM *kv = BN_new();
    BN_mod_mul(kv, k_bn, v_bn, p, ctx);

    /* gb = g^b mod p */
    BIGNUM *gb = BN_new();
    BN_mod_exp(gb, g, b_bn, p, ctx);

    /* B_wire = (kv + gb) mod p */
    BIGNUM *B = BN_new();
    BN_mod_add(B, kv, gb, p, ctx);

    int sz = BN_num_bytes(B);
    memset(B_wire_out, 0, 256);
    BN_bn2bin(B, B_wire_out + (256 - sz));

    BN_free(p); BN_free(g); BN_free(b_bn); BN_free(x_bn);
    BN_free(v_bn); BN_free(k_bn); BN_free(kv);
    BN_free(gb); BN_free(B);
    BN_CTX_free(ctx);
}

/* Full round-trip: client computes (A, M1). Server, holding its own
 * private b, derives its own S_server using (A * v^u)^b mod p, and
 * we assert that S_client (derived by the client through the chained
 * identity base^a * base^(u*x) mod p) also equals that value by
 * reconstructing K / M1 on the server side. */
static void test_srp_roundtrip_math(void) {
    const char *password = "correct horse battery staple";

    /* Deterministic client private a and server private b. */
    uint8_t a[256]; for (int i = 0; i < 256; i++) a[i] = (uint8_t)(i ^ 0x11);
    uint8_t b[256]; for (int i = 0; i < 256; i++) b[i] = (uint8_t)(i ^ 0x55);

    Account2faPassword params;
    init_params_shell(&params, 3);
    /* Server ephemeral: Telegram convention is B_wire = (k*v + g^b) mod p. */
    server_B_with_kv(3, b, sizeof(b), password,
                      params.salt1, params.salt1_len,
                      params.salt2, params.salt2_len,
                      params.srp_B);
    uint8_t B[256]; memcpy(B, params.srp_B, 256);

    /* Client side — our project's srp_compute. */
    uint8_t A[256], M1[32];
    int rc = auth_2fa_srp_compute(&params, password, a, A, M1);
    ASSERT(rc == 0, "client srp_compute ok");

    /* u = SHA256(A | B) */
    uint8_t ubuf[512]; memcpy(ubuf, A, 256); memcpy(ubuf + 256, B, 256);
    uint8_t u32[32]; crypto_sha256(ubuf, 512, u32);

    /* x = PH2 */
    uint8_t x32[32];
    ph2(password, params.salt1, params.salt1_len,
         params.salt2, params.salt2_len, x32);

    /* Server S = (A * v^u)^b mod p. */
    uint8_t S_server[256];
    server_compute_S(A, b, sizeof(b), u32, x32, params.g, S_server);

    /* Rebuild server-side M1 = H(H(p) XOR H(g) | H(s1) | H(s2) | A | B | H(S)). */
    uint8_t g_bytes[256]; memset(g_bytes, 0, 256); g_bytes[255] = 3;
    uint8_t h_p[32]; crypto_sha256(params.p, 256, h_p);
    uint8_t h_g[32]; crypto_sha256(g_bytes, 256, h_g);
    for (int i = 0; i < 32; i++) h_p[i] ^= h_g[i];
    uint8_t h_s1[32]; crypto_sha256(params.salt1, params.salt1_len, h_s1);
    uint8_t h_s2[32]; crypto_sha256(params.salt2, params.salt2_len, h_s2);
    uint8_t K[32];    crypto_sha256(S_server, 256, K);

    uint8_t m1buf[32 + 32 + 32 + 256 + 256 + 32]; size_t off = 0;
    memcpy(m1buf + off, h_p, 32);  off += 32;
    memcpy(m1buf + off, h_s1, 32); off += 32;
    memcpy(m1buf + off, h_s2, 32); off += 32;
    memcpy(m1buf + off, A, 256);   off += 256;
    memcpy(m1buf + off, B, 256);   off += 256;
    memcpy(m1buf + off, K, 32);    off += 32;
    uint8_t M1_expected[32]; crypto_sha256(m1buf, off, M1_expected);

    ASSERT(memcmp(M1, M1_expected, 32) == 0,
           "client M1 matches independently derived server M1 — "
           "SRP math invariant holds");
}

/* Changing the password must break the M1 match. */
static void test_srp_wrong_password_breaks_M1(void) {
    uint8_t a[256]; for (int i = 0; i < 256; i++) a[i] = (uint8_t)(i ^ 0x11);
    uint8_t b[256]; for (int i = 0; i < 256; i++) b[i] = (uint8_t)(i ^ 0x55);

    Account2faPassword params;
    init_params_shell(&params, 3);
    server_B_with_kv(3, b, sizeof(b), "the right one",
                      params.salt1, params.salt1_len,
                      params.salt2, params.salt2_len,
                      params.srp_B);

    uint8_t A_right[256], M1_right[32];
    uint8_t A_wrong[256], M1_wrong[32];
    ASSERT(auth_2fa_srp_compute(&params, "the right one", a,
                                  A_right, M1_right) == 0, "right ok");
    ASSERT(auth_2fa_srp_compute(&params, "the wrong one", a,
                                  A_wrong, M1_wrong) == 0, "wrong ok");

    ASSERT(memcmp(A_right, A_wrong, 256) == 0,
           "A depends only on a, not on password");
    ASSERT(memcmp(M1_right, M1_wrong, 32) != 0,
           "M1 must differ for different passwords");
}

/* Deterministic: same inputs → same outputs. */
static void test_srp_deterministic(void) {
    uint8_t a[256]; for (int i = 0; i < 256; i++) a[i] = (uint8_t)(i ^ 0x33);
    uint8_t b[256]; for (int i = 0; i < 256; i++) b[i] = (uint8_t)(i ^ 0x77);

    Account2faPassword params;
    init_params_shell(&params, 3);
    server_B_with_kv(3, b, sizeof(b), "hunter2",
                      params.salt1, params.salt1_len,
                      params.salt2, params.salt2_len, params.srp_B);

    uint8_t A1[256], M1_1[32], A2[256], M1_2[32];
    ASSERT(auth_2fa_srp_compute(&params, "hunter2", a, A1, M1_1) == 0, "#1");
    ASSERT(auth_2fa_srp_compute(&params, "hunter2", a, A2, M1_2) == 0, "#2");
    ASSERT(memcmp(A1, A2, 256) == 0, "A deterministic");
    ASSERT(memcmp(M1_1, M1_2, 32) == 0, "M1 deterministic");
}

/* Bail on missing password flag / NULL arguments. */
static void test_srp_bad_args(void) {
    uint8_t A[256], M1[32];
    Account2faPassword p = {0};
    ASSERT(auth_2fa_srp_compute(NULL, "x", NULL, A, M1) == -1, "null params");
    ASSERT(auth_2fa_srp_compute(&p, NULL, NULL, A, M1) == -1, "null password");
    /* has_password == 0 */
    ASSERT(auth_2fa_srp_compute(&p, "x", NULL, A, M1) == -1, "no 2FA set");
}

/* Suppress unused-warning for the helper that we keep around for
 * documentation / future vectors. */
static void suppress_unused(void) { uint8_t b[8]; uint8_t out[256];
    for (int i = 0; i < 8; i++) b[i] = 0;
    server_B(2, b, 1, out); (void)out; }

void run_srp_roundtrip_functional_tests(void) {
    RUN_TEST(test_srp_roundtrip_math);
    RUN_TEST(test_srp_wrong_password_breaks_M1);
    RUN_TEST(test_srp_deterministic);
    RUN_TEST(test_srp_bad_args);
    (void)suppress_unused;
}
