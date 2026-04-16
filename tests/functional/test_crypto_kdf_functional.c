/**
 * @file test_crypto_kdf_functional.c
 * @brief Functional (real crypto) tests for SHA-512 + PBKDF2-HMAC-SHA512.
 *
 * Known-answer tests so we catch divergence between our wrappers and
 * OpenSSL. Values are from public RFC/draft reference vectors.
 */

#include "test_helpers.h"
#include "crypto.h"

#include <stdint.h>
#include <string.h>

/* SHA-512("") - NIST/RFC 4634 */
static void test_sha512_empty_known(void) {
    unsigned char out[64];
    crypto_sha512((const unsigned char *)"", 0, out);

    static const unsigned char expected[64] = {
        0xcf,0x83,0xe1,0x35,0x7e,0xef,0xb8,0xbd,0xf1,0x54,0x28,0x50,0xd6,0x6d,0x80,0x07,
        0xd6,0x20,0xe4,0x05,0x0b,0x57,0x15,0xdc,0x83,0xf4,0xa9,0x21,0xd3,0x6c,0xe9,0xce,
        0x47,0xd0,0xd1,0x3c,0x5d,0x85,0xf2,0xb0,0xff,0x83,0x18,0xd2,0x87,0x7e,0xec,0x2f,
        0x63,0xb9,0x31,0xbd,0x47,0x41,0x7a,0x81,0xa5,0x38,0x32,0x7a,0xf9,0x27,0xda,0x3e
    };
    ASSERT(memcmp(out, expected, 64) == 0, "SHA-512(empty) matches NIST");
}

/* SHA-512("abc") - FIPS 180-4 */
static void test_sha512_abc_known(void) {
    unsigned char out[64];
    crypto_sha512((const unsigned char *)"abc", 3, out);

    static const unsigned char expected[64] = {
        0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
        0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
        0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
        0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f
    };
    ASSERT(memcmp(out, expected, 64) == 0, "SHA-512(abc) matches FIPS 180-4");
}

/* PBKDF2-HMAC-SHA512 reference vector (draft-josefsson-scrypt-kdf-01 §2):
 *   P = "password", S = "salt", c = 1, dkLen = 64 */
static void test_pbkdf2_sha512_known_1iter(void) {
    unsigned char out[64];
    int rc = crypto_pbkdf2_hmac_sha512(
        (const unsigned char *)"password", 8,
        (const unsigned char *)"salt", 4,
        1, out, sizeof(out));
    ASSERT(rc == 0, "PBKDF2 returns ok");

    static const unsigned char expected[64] = {
        0x86,0x7f,0x70,0xcf,0x1a,0xde,0x02,0xcf,0xf3,0x75,0x25,0x99,0xa3,0xa5,0x3d,0xc4,
        0xaf,0x34,0xc7,0xa6,0x69,0x81,0x5a,0xe5,0xd5,0x13,0x55,0x4e,0x1c,0x8c,0xf2,0x52,
        0xc0,0x2d,0x47,0x0a,0x28,0x5a,0x05,0x01,0xba,0xd9,0x99,0xbf,0xe9,0x43,0xc0,0x8f,
        0x05,0x02,0x35,0xd7,0xd6,0x8b,0x1d,0xa5,0x5e,0x63,0xf7,0x3b,0x60,0xa5,0x7f,0xce
    };
    ASSERT(memcmp(out, expected, 64) == 0, "PBKDF2-SHA512 known vector");
}

/* Smaller iteration count — still deterministic + length-varied output. */
static void test_pbkdf2_sha512_determinism(void) {
    unsigned char a[32], b[32];
    int rc1 = crypto_pbkdf2_hmac_sha512(
        (const unsigned char *)"secret", 6,
        (const unsigned char *)"NaCl", 4,
        100, a, sizeof(a));
    int rc2 = crypto_pbkdf2_hmac_sha512(
        (const unsigned char *)"secret", 6,
        (const unsigned char *)"NaCl", 4,
        100, b, sizeof(b));
    ASSERT(rc1 == 0 && rc2 == 0, "both PBKDF2 calls succeed");
    ASSERT(memcmp(a, b, 32) == 0, "PBKDF2 is deterministic");

    /* Different password → different key. */
    unsigned char c[32];
    crypto_pbkdf2_hmac_sha512(
        (const unsigned char *)"secre!", 6,
        (const unsigned char *)"NaCl", 4,
        100, c, sizeof(c));
    ASSERT(memcmp(a, c, 32) != 0, "changing password flips output");
}

static void test_pbkdf2_sha512_rejects_bad_args(void) {
    unsigned char out[32];
    ASSERT(crypto_pbkdf2_hmac_sha512(NULL, 0, NULL, 0, 1, out, sizeof(out)) == -1,
           "null password/salt rejected");
    ASSERT(crypto_pbkdf2_hmac_sha512(
              (const unsigned char *)"p", 1,
              (const unsigned char *)"s", 1,
              0, out, sizeof(out)) == -1,
           "iters <= 0 rejected");
    ASSERT(crypto_pbkdf2_hmac_sha512(
              (const unsigned char *)"p", 1,
              (const unsigned char *)"s", 1,
              1, out, 0) == -1,
           "out_len == 0 rejected");
}

void run_crypto_kdf_functional_tests(void) {
    RUN_TEST(test_sha512_empty_known);
    RUN_TEST(test_sha512_abc_known);
    RUN_TEST(test_pbkdf2_sha512_known_1iter);
    RUN_TEST(test_pbkdf2_sha512_determinism);
    RUN_TEST(test_pbkdf2_sha512_rejects_bad_args);
}
