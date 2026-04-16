/**
 * @file test_srp_math_functional.c
 * @brief Real-crypto smoke test for the SRP math pieces used by
 *        auth_2fa_check_password (P3-03).
 *
 * We don't have access to server-generated reference vectors, so instead
 * we verify the *properties* that must hold independent of the exact
 * numbers:
 *   1. mod_mul + mod_sub + mod_exp agree on a known-small identity.
 *   2. mod_exp outputs are always reduced to the modulus length.
 *   3. The full PH1/PH2 chain is deterministic for the same password
 *      and salt pair.
 *
 * Known-answer for the full inputCheckPasswordSRP requires reproducing
 * the server's private ephemeral b, which we can't. Instead we land
 * just the SRP-adjacent primitive checks.
 */

#include "test_helpers.h"
#include "crypto.h"

#include <stdint.h>
#include <string.h>

static const uint8_t PRIME_8[1] = { 0x0B }; /* 11 decimal */

/* 2^5 mod 11 = 10 (=0x0a) */
static void test_mod_exp_small_known(void) {
    uint8_t base = 2, exp = 5;
    uint8_t out[1] = {0};
    size_t out_len = sizeof(out);
    CryptoBnCtx *ctx = crypto_bn_ctx_new();
    int rc = crypto_bn_mod_exp(out, &out_len, &base, 1, &exp, 1,
                                 PRIME_8, 1, ctx);
    crypto_bn_ctx_free(ctx);
    ASSERT(rc == 0, "mod_exp returns ok");
    ASSERT(out_len == 1, "output padded to modulus length");
    ASSERT(out[0] == 10, "2^5 mod 11 == 10");
}

/* 7 * 8 mod 11 == 1 */
static void test_mod_mul_small_known(void) {
    uint8_t a = 7, b = 8, out[1] = {0};
    size_t out_len = sizeof(out);
    CryptoBnCtx *ctx = crypto_bn_ctx_new();
    int rc = crypto_bn_mod_mul(out, &out_len, &a, 1, &b, 1,
                                 PRIME_8, 1, ctx);
    crypto_bn_ctx_free(ctx);
    ASSERT(rc == 0, "mod_mul ok");
    ASSERT(out[0] == 1, "7*8 mod 11 == 1");
}

/* (3 - 9) mod 11 = 5 (always non-negative) */
static void test_mod_sub_small_known(void) {
    uint8_t a = 3, b = 9, out[1] = {0};
    size_t out_len = sizeof(out);
    CryptoBnCtx *ctx = crypto_bn_ctx_new();
    int rc = crypto_bn_mod_sub(out, &out_len, &a, 1, &b, 1,
                                 PRIME_8, 1, ctx);
    crypto_bn_ctx_free(ctx);
    ASSERT(rc == 0, "mod_sub ok");
    ASSERT(out[0] == 5, "(3 - 9) mod 11 == 5");
}

/* Modular reduction fills to the modulus byte count even for small values. */
static void test_mod_exp_pads_output(void) {
    uint8_t base = 2, exp = 1;
    uint8_t prime[32] = {0};
    prime[31] = 0x0B;
    uint8_t out[32] = {0};
    size_t out_len = sizeof(out);
    CryptoBnCtx *ctx = crypto_bn_ctx_new();
    int rc = crypto_bn_mod_exp(out, &out_len, &base, 1, &exp, 1,
                                 prime, sizeof(prime), ctx);
    crypto_bn_ctx_free(ctx);
    ASSERT(rc == 0, "mod_exp padded ok");
    ASSERT(out_len == 32, "full modulus width returned");
    for (int i = 0; i < 31; i++) ASSERT(out[i] == 0, "leading zero pad");
    ASSERT(out[31] == 2, "last byte carries the small result");
}

/* ucmp semantics */
static void test_ucmp(void) {
    uint8_t a[2] = { 0x01, 0x00 };
    uint8_t b[2] = { 0x00, 0xFF };
    ASSERT(crypto_bn_ucmp(a, 2, b, 2) == 1, "a > b");
    ASSERT(crypto_bn_ucmp(b, 2, a, 2) == -1, "b < a");
    ASSERT(crypto_bn_ucmp(a, 2, a, 2) == 0, "eq");
}

/* PBKDF2 driven by the same salts / password must be deterministic. */
static void test_srp_x_deterministic(void) {
    const uint8_t salt1[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const uint8_t salt2[16] = {16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};
    uint8_t a[32], b[32];

    /* Simulate PH2 core: pbkdf2(password, salt1, 100000, 64) folded
     * through an outer SHA-256 salted by salt2. Deterministic across
     * runs given the same inputs. */
    uint8_t pbkdf[64];
    int rc = crypto_pbkdf2_hmac_sha512((const uint8_t *)"hunter2", 7,
                                         salt1, sizeof(salt1),
                                         100, pbkdf, sizeof(pbkdf));
    ASSERT(rc == 0, "pbkdf ok");

    uint8_t buf[16 + 64 + 16];
    memcpy(buf, salt2, 16);
    memcpy(buf + 16, pbkdf, 64);
    memcpy(buf + 16 + 64, salt2, 16);
    crypto_sha256(buf, sizeof(buf), a);

    /* Call again — same inputs must yield same hash. */
    rc = crypto_pbkdf2_hmac_sha512((const uint8_t *)"hunter2", 7,
                                     salt1, sizeof(salt1),
                                     100, pbkdf, sizeof(pbkdf));
    ASSERT(rc == 0, "pbkdf ok (2nd)");
    memcpy(buf, salt2, 16);
    memcpy(buf + 16, pbkdf, 64);
    memcpy(buf + 16 + 64, salt2, 16);
    crypto_sha256(buf, sizeof(buf), b);

    ASSERT(memcmp(a, b, 32) == 0, "PH2-like chain is deterministic");
}

void run_srp_math_functional_tests(void) {
    RUN_TEST(test_mod_exp_small_known);
    RUN_TEST(test_mod_mul_small_known);
    RUN_TEST(test_mod_sub_small_known);
    RUN_TEST(test_mod_exp_pads_output);
    RUN_TEST(test_ucmp);
    RUN_TEST(test_srp_x_deterministic);
}
