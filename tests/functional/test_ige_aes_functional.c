/**
 * @file test_ige_aes_functional.c
 * @brief Functional (real crypto) tests for AES-256-IGE.
 *
 * Tests link against real crypto.c (OpenSSL) rather than the mock, so
 * these catch bugs that mock passthrough would hide.
 *
 * Test strategy:
 *   1. Round-trip: encrypt → decrypt → verify recovered plaintext.
 *   2. OpenSSL cross-check: manually implement IGE using raw OpenSSL AES ECB
 *      and compare against aes_ige_encrypt.  If they differ, our
 *      crypto_aes_encrypt_block wrapper is broken.
 *   3. Error-propagation: flip a byte in ciphertext, verify decrypted output
 *      differs (IGE forward error propagation).
 *   4. Different key / IV / length combinations.
 */

#include "test_helpers.h"
#include "ige_aes.h"

/* Reference implementation uses OpenSSL's low-level AES API (deprecated in
 * OpenSSL 3.0 but still functional).  Suppress deprecation warnings only for
 * this reference block — production code uses crypto.h wrappers instead. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/aes.h>
#pragma GCC diagnostic pop

#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---- Reference IGE using raw OpenSSL (cross-check) ---- */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static void ref_ige_encrypt(const uint8_t *plain, size_t len,
                              const uint8_t *key, const uint8_t *iv,
                              uint8_t *cipher) {
    AES_KEY sched;
    AES_set_encrypt_key(key, 256, &sched);

    uint8_t iv_c[16], iv_p[16];
    memcpy(iv_c, iv,      16);
    memcpy(iv_p, iv + 16, 16);

    for (size_t off = 0; off < len; off += 16) {
        uint8_t buf[16];
        for (int j = 0; j < 16; j++)
            buf[j] = plain[off + j] ^ iv_c[j];
        AES_encrypt(buf, cipher + off, &sched);
        for (int j = 0; j < 16; j++)
            cipher[off + j] ^= iv_p[j];
        memcpy(iv_c, cipher + off,   16);
        memcpy(iv_p, plain  + off,   16);
    }
}

static void ref_ige_decrypt(const uint8_t *cipher, size_t len,
                              const uint8_t *key, const uint8_t *iv,
                              uint8_t *plain) {
    AES_KEY sched;
    AES_set_decrypt_key(key, 256, &sched);

    uint8_t iv_c[16], iv_p[16];
    memcpy(iv_c, iv,      16);
    memcpy(iv_p, iv + 16, 16);

    for (size_t off = 0; off < len; off += 16) {
        uint8_t buf[16];
        for (int j = 0; j < 16; j++)
            buf[j] = cipher[off + j] ^ iv_p[j];
        AES_decrypt(buf, plain + off, &sched);
        for (int j = 0; j < 16; j++)
            plain[off + j] ^= iv_c[j];
        memcpy(iv_p, plain  + off,   16);
        memcpy(iv_c, cipher + off,   16);
    }
}

#pragma GCC diagnostic pop

/* ---- Helper: all-zero buffers of given size ---- */
static void fill_pattern(uint8_t *buf, size_t len, uint8_t pattern) {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(pattern + i);
}

/* ---- Test: single-block round-trip ---- */
static void test_ige_single_block_roundtrip(void) {
    uint8_t key[32], iv[32], plain[16], cipher[16], recovered[16];
    fill_pattern(key,   32, 0x01);
    fill_pattern(iv,    32, 0xAA);
    fill_pattern(plain, 16, 0x55);

    aes_ige_encrypt(plain,  16, key, iv, cipher);
    aes_ige_decrypt(cipher, 16, key, iv, recovered);

    ASSERT(memcmp(plain, recovered, 16) == 0,
           "single-block: decrypt(encrypt(p)) != p");
    ASSERT(memcmp(plain, cipher, 16) != 0,
           "single-block: ciphertext must differ from plaintext");
}

/* ---- Test: multi-block round-trip ---- */
static void test_ige_multi_block_roundtrip(void) {
    uint8_t key[32], iv[32], plain[128], cipher[128], recovered[128];
    fill_pattern(key,   32,  0x13);
    fill_pattern(iv,    32,  0x7F);
    fill_pattern(plain, 128, 0x00);

    aes_ige_encrypt(plain,  128, key, iv, cipher);
    aes_ige_decrypt(cipher, 128, key, iv, recovered);

    ASSERT(memcmp(plain, recovered, 128) == 0,
           "multi-block: decrypt(encrypt(p)) != p");
}

/* ---- Test: cross-check against raw OpenSSL ---- */
static void test_ige_cross_check_openssl(void) {
    uint8_t key[32], iv[32], plain[64];
    fill_pattern(key,   32, 0xDE);
    fill_pattern(iv,    32, 0xAD);
    fill_pattern(plain, 64, 0xBE);

    uint8_t cipher_ours[64], cipher_ref[64];
    aes_ige_encrypt(plain, 64, key, iv, cipher_ours);
    ref_ige_encrypt(plain, 64, key, iv, cipher_ref);

    ASSERT(memcmp(cipher_ours, cipher_ref, 64) == 0,
           "encrypt: our IGE differs from reference OpenSSL IGE");

    uint8_t plain_ours[64], plain_ref[64];
    aes_ige_decrypt(cipher_ours, 64, key, iv, plain_ours);
    ref_ige_decrypt(cipher_ref,  64, key, iv, plain_ref);

    ASSERT(memcmp(plain_ours, plain_ref, 64) == 0,
           "decrypt: our IGE differs from reference OpenSSL IGE");
    ASSERT(memcmp(plain_ours, plain, 64) == 0,
           "decrypt: recovered plaintext differs from original");
}

/* ---- Test: all-zero key and IV ---- */
static void test_ige_zero_key_iv(void) {
    uint8_t key[32] = {0}, iv[32] = {0};
    uint8_t plain[32], cipher[32], cipher_ref[32], recovered[32];
    fill_pattern(plain, 32, 0xAB);

    aes_ige_encrypt(plain, 32, key, iv, cipher);
    ref_ige_encrypt(plain, 32, key, iv, cipher_ref);

    ASSERT(memcmp(cipher, cipher_ref, 32) == 0,
           "zero key/iv: our IGE differs from reference");

    aes_ige_decrypt(cipher, 32, key, iv, recovered);
    ASSERT(memcmp(plain, recovered, 32) == 0,
           "zero key/iv: round-trip failed");
}

/* ---- Test: error propagation (corrupt ciphertext affects 2 blocks) ---- */
static void test_ige_error_propagation(void) {
    uint8_t key[32], iv[32], plain[48], cipher[48], recovered[48];
    fill_pattern(key,   32, 0x11);
    fill_pattern(iv,    32, 0x22);
    fill_pattern(plain, 48, 0x33);

    aes_ige_encrypt(plain, 48, key, iv, cipher);

    /* Corrupt byte 0 in block 1 (offset 16) */
    uint8_t corrupted[48];
    memcpy(corrupted, cipher, 48);
    corrupted[16] ^= 0xFF;

    aes_ige_decrypt(corrupted, 48, key, iv, recovered);

    /* Block 1 and block 2 should differ from original (IGE propagates) */
    ASSERT(memcmp(plain + 16, recovered + 16, 16) != 0,
           "error propagation: block 1 should differ after corruption");
    ASSERT(memcmp(plain + 32, recovered + 32, 16) != 0,
           "error propagation: block 2 should differ after corruption");
}

/* ---- Test: different key changes ciphertext ---- */
static void test_ige_key_sensitivity(void) {
    uint8_t key1[32], key2[32], iv[32], plain[32];
    uint8_t cipher1[32], cipher2[32];
    fill_pattern(key1,  32, 0x01);
    fill_pattern(key2,  32, 0x01);
    key2[0] ^= 0x01; /* one bit difference */
    fill_pattern(iv,    32, 0xFF);
    fill_pattern(plain, 32, 0x00);

    aes_ige_encrypt(plain, 32, key1, iv, cipher1);
    aes_ige_encrypt(plain, 32, key2, iv, cipher2);

    ASSERT(memcmp(cipher1, cipher2, 32) != 0,
           "key sensitivity: single key bit change should change ciphertext");
}

/* ---- Test: IV sensitivity ---- */
static void test_ige_iv_sensitivity(void) {
    uint8_t key[32], iv1[32], iv2[32], plain[32];
    uint8_t cipher1[32], cipher2[32];
    fill_pattern(key,   32, 0xAA);
    fill_pattern(iv1,   32, 0x11);
    fill_pattern(iv2,   32, 0x11);
    iv2[0] ^= 0x01;
    fill_pattern(plain, 32, 0x55);

    aes_ige_encrypt(plain, 32, key, iv1, cipher1);
    aes_ige_encrypt(plain, 32, key, iv2, cipher2);

    ASSERT(memcmp(cipher1, cipher2, 32) != 0,
           "IV sensitivity: single IV bit change should change ciphertext");
}

void run_ige_aes_functional_tests(void) {
    RUN_TEST(test_ige_single_block_roundtrip);
    RUN_TEST(test_ige_multi_block_roundtrip);
    RUN_TEST(test_ige_cross_check_openssl);
    RUN_TEST(test_ige_zero_key_iv);
    RUN_TEST(test_ige_error_propagation);
    RUN_TEST(test_ige_key_sensitivity);
    RUN_TEST(test_ige_iv_sensitivity);
}
