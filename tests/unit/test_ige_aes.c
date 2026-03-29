/**
 * @file test_ige_aes.c
 * @brief Unit tests for AES-256-IGE (using mock crypto).
 *
 * Verifies IGE block chaining logic and call counts with mock crypto.
 * Real-crypto functional tests (known-answer, IV propagation) belong in
 * a separate functional test binary linked with production crypto.c.
 *
 * Known mock limitations (functional tests needed):
 *   - Mock encrypt_block is identity → identical plaintext blocks may produce
 *     identical ciphertext (IV XOR cancellation when iv_c == iv_p).
 *   - Cannot verify that ciphertext actually differs from plaintext with
 *     uniform IVs (XOR symmetry with identity encrypt).
 *   - Round-trip works because encrypt/decrypt are symmetric under identity.
 */

#include "test_helpers.h"
#include "ige_aes.h"
#include "mock_crypto.h"
#include "crypto.h"

#include <stdlib.h>
#include <string.h>

void test_ige_encrypt_1block(void) {
    mock_crypto_reset();
    uint8_t key[32] = {0}, iv[32] = {0};
    uint8_t plain[16] = {1}, cipher[16];

    aes_ige_encrypt(plain, 16, key, iv, cipher);
    ASSERT(mock_crypto_encrypt_block_call_count() == 1,
           "1 block → 1 encrypt_block call");
}

void test_ige_encrypt_3blocks(void) {
    mock_crypto_reset();
    uint8_t key[32] = {0}, iv[32] = {0};
    uint8_t plain[48] = {0}, cipher[48];

    aes_ige_encrypt(plain, 48, key, iv, cipher);
    ASSERT(mock_crypto_encrypt_block_call_count() == 3,
           "3 blocks → 3 encrypt_block calls");
}

void test_ige_decrypt_2blocks(void) {
    mock_crypto_reset();
    uint8_t key[32] = {0}, iv[32] = {0};
    uint8_t cipher[32] = {0}, plain[32];

    aes_ige_decrypt(cipher, 32, key, iv, plain);
    ASSERT(mock_crypto_decrypt_block_call_count() == 2,
           "2 blocks → 2 decrypt_block calls");
}

void test_ige_set_encrypt_key(void) {
    mock_crypto_reset();
    uint8_t key[32] = {0}, iv[32] = {0};
    uint8_t plain[16] = {0}, cipher[16];

    aes_ige_encrypt(plain, 16, key, iv, cipher);
    ASSERT(mock_crypto_set_encrypt_key_call_count() == 1,
           "set_encrypt_key should be called once");
}

void test_ige_roundtrip_1block(void) {
    /* With identity mock, IGE is XOR-symmetric → round-trip works */
    uint8_t key[32], iv[32], plain[16], cipher[16], dec[16];
    memset(key, 0x42, 32);
    memset(iv, 0x55, 32);
    for (int i = 0; i < 16; i++) plain[i] = (uint8_t)i;

    aes_ige_encrypt(plain, 16, key, iv, cipher);
    aes_ige_decrypt(cipher, 16, key, iv, dec);
    ASSERT(memcmp(plain, dec, 16) == 0, "round-trip 1 block");
}

void test_ige_roundtrip_4blocks(void) {
    uint8_t key[32], iv[32], plain[64], cipher[64], dec[64];
    memset(key, 0xAB, 32);
    memset(iv, 0xCD, 32);
    for (int i = 0; i < 64; i++) plain[i] = (uint8_t)(i * 7);

    aes_ige_encrypt(plain, 64, key, iv, cipher);
    aes_ige_decrypt(cipher, 64, key, iv, dec);
    ASSERT(memcmp(plain, dec, 64) == 0, "round-trip 4 blocks");
}

void test_ige_null_safe(void) {
    uint8_t buf[16] = {0};
    aes_ige_encrypt(NULL, 16, buf, buf, buf);
    aes_ige_encrypt(buf, 0, buf, buf, buf);
    aes_ige_decrypt(NULL, 16, buf, buf, buf);
    aes_ige_decrypt(buf, 0, buf, buf, buf);
    ASSERT(1, "null/zero inputs should not crash");
}

void test_ige(void) {
    RUN_TEST(test_ige_encrypt_1block);
    RUN_TEST(test_ige_encrypt_3blocks);
    RUN_TEST(test_ige_decrypt_2blocks);
    RUN_TEST(test_ige_set_encrypt_key);
    RUN_TEST(test_ige_roundtrip_1block);
    RUN_TEST(test_ige_roundtrip_4blocks);
    RUN_TEST(test_ige_null_safe);
}
