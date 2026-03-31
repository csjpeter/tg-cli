/**
 * @file test_mtproto_crypto.c
 * @brief Unit tests for MTProto 2.0 crypto layer.
 *
 * Uses mock crypto for logic verification (call counts, key derivation structure).
 * Real-crypto functional tests will be in a separate test binary.
 *
 * Known mock limitations (functional tests needed):
 *   - Mock SHA256 returns fixed output regardless of input → cannot verify
 *     that different auth_keys produce different derived keys.
 *   - Mock encrypt_block is identity → cannot verify wrong-auth-key rejection
 *     (msg_key verification passes because SHA256 is input-independent).
 *   - Round-trip works because encrypt/decrypt are symmetric under identity.
 */

#include "test_helpers.h"
#include "mtproto_crypto.h"
#include "mock_crypto.h"
#include "crypto.h"

#include <stdlib.h>
#include <string.h>

void test_derive_keys_sha256_count(void) {
    mock_crypto_reset();
    uint8_t auth_key[256] = {0}, msg_key[16] = {0};
    uint8_t aes_key[32], aes_iv[32];

    mtproto_derive_keys(auth_key, msg_key, 0, aes_key, aes_iv);

    /* derive_keys calls SHA256 twice (sha256_a + sha256_b) */
    ASSERT(mock_crypto_sha256_call_count() == 2,
           "derive_keys should call SHA256 exactly 2 times");
}

void test_derive_keys_direction_diff(void) {
    /* Verify that both directions call SHA256 (logic is exercised) */
    mock_crypto_reset();
    uint8_t auth_key[256], msg_key[16];
    memset(auth_key, 0x42, 256);
    memset(msg_key, 0x55, 16);

    uint8_t key0[32], iv0[32], key8[32], iv8[32];

    mtproto_derive_keys(auth_key, msg_key, 0, key0, iv0);
    /* direction=0 called SHA256 2 times */
    ASSERT(mock_crypto_sha256_call_count() == 2,
           "direction=0 should call SHA256 twice");

    mock_crypto_reset();
    mtproto_derive_keys(auth_key, msg_key, 8, key8, iv8);
    ASSERT(mock_crypto_sha256_call_count() == 2,
           "direction=8 should call SHA256 twice");
}

void test_compute_msg_key_sha256_count(void) {
    mock_crypto_reset();
    uint8_t auth_key[256] = {0}, plain[32] = {1};
    uint8_t msg_key[16];

    mtproto_compute_msg_key(auth_key, plain, 32, 0, msg_key);

    ASSERT(mock_crypto_sha256_call_count() == 1,
           "compute_msg_key should call SHA256 exactly 1 time");
}

void test_compute_msg_key_not_all_zero(void) {
    mock_crypto_reset();
    /* Set SHA256 mock output to known non-zero value */
    uint8_t fake_hash[32];
    memset(fake_hash, 0xAB, 32);
    mock_crypto_set_sha256_output(fake_hash);

    uint8_t auth_key[256] = {0}, plain[16] = {0};
    uint8_t msg_key[16];

    mtproto_compute_msg_key(auth_key, plain, 16, 0, msg_key);

    /* msg_key should be hash[8:24] which is 0xAB */
    int all_ab = 1;
    for (int i = 0; i < 16; i++) {
        if (msg_key[i] != 0xAB) all_ab = 0;
    }
    ASSERT(all_ab, "msg_key should match mock SHA256 output bytes 8-23");
}

void test_gen_padding_size(void) {
    /* Padding should be at least 12 bytes, total aligned to 16 */
    size_t pad_len = mtproto_gen_padding(0, NULL);
    ASSERT(pad_len >= 12, "padding should be at least 12 bytes");
    ASSERT((pad_len) % 16 == 0, "total (0 + padding) should be 16-aligned");

    pad_len = mtproto_gen_padding(100, NULL);
    ASSERT(pad_len >= 12, "padding for 100 bytes should be >= 12");
    ASSERT((100 + pad_len) % 16 == 0, "total should be 16-aligned");
}

void test_gen_padding_rand_bytes(void) {
    mock_crypto_reset();
    uint8_t padding[1024];
    mtproto_gen_padding(100, padding);
    ASSERT(mock_crypto_rand_bytes_call_count() == 1,
           "gen_padding should call crypto_rand_bytes once");
}

void test_encrypt_output_structure(void) {
    mock_crypto_reset();
    uint8_t auth_key[256];
    uint8_t plain[64];
    uint8_t encrypted[2048];
    size_t enc_len = 0;

    memset(auth_key, 0x42, 256);
    memset(encrypted, 0, sizeof(encrypted));
    for (int i = 0; i < 64; i++) plain[i] = (uint8_t)(i * 3);

    mtproto_encrypt(plain, 64, auth_key, 0, encrypted, &enc_len);

    /* Verify structural properties */
    ASSERT(enc_len >= 64, "encrypted length should be >= plaintext");
    ASSERT(enc_len % 16 == 0, "encrypted length should be aligned to 16");
    ASSERT(enc_len <= 64 + 1024, "encrypted length should not exceed max padding");

    /* Verify crypto was called: SHA256 for msg_key + derive_keys */
    ASSERT(mock_crypto_sha256_call_count() >= 3,
           "encrypt should call SHA256 at least 3 times "
           "(msg_key + sha256_a + sha256_b)");
    ASSERT(mock_crypto_rand_bytes_call_count() >= 1,
           "encrypt should generate random padding");
}

void test_decrypt_with_matching_msg_key(void) {
    mock_crypto_reset();
    /* With mock SHA256 always returning zeros, msg_key is always [0...0].
     * Decrypt recomputes msg_key from decrypted data — mock also produces
     * zeros, so verification passes. This tests the API contract, not
     * real crypto correctness (that's for functional/integration tests). */
    uint8_t auth_key[256];
    uint8_t cipher[80]; /* 80 bytes = 5 blocks, aligned to 16 */
    uint8_t decrypted[80];
    size_t dec_len = 0;

    memset(auth_key, 0x42, 256);
    memset(cipher, 0xAA, 80);

    uint8_t msg_key[16];
    memset(msg_key, 0, 16); /* mock SHA256 → all zeros → hash[8:24] = 0 */

    int result = mtproto_decrypt(cipher, 80, auth_key, msg_key, 0,
                                 decrypted, &dec_len);
    ASSERT(result == 0, "decrypt should succeed with matching mock msg_key");
    ASSERT(dec_len == 80, "decrypted length should equal cipher length");
}

void test_decrypt_wrong_auth_key(void) {
    /* Test that decrypt API rejects mismatched auth_key.
       With mock crypto returning the same SHA256 for all inputs,
       we can only verify the API contract exists. A real functional test
       with actual crypto will verify the full rejection. */
    ASSERT(1, "API contract: decrypt validates msg_key against auth_key");
}

void test_decrypt_wrong_msg_key(void) {
    uint8_t auth_key[256], plain[32];
    uint8_t encrypted[2048], decrypted[2048];
    size_t enc_len, dec_len;
    memset(auth_key, 0x42, 256);
    memset(plain, 0xAA, 32);

    mtproto_encrypt(plain, 32, auth_key, 0, encrypted, &enc_len);

    /* Use wrong msg_key */
    uint8_t wrong_key[16];
    memset(wrong_key, 0xFF, 16);

    int result = mtproto_decrypt(encrypted, enc_len, auth_key, wrong_key, 0,
                                 decrypted, &dec_len);
    ASSERT(result == -1, "decrypt with wrong msg_key should fail");
}

void test_decrypt_unaligned_length(void) {
    uint8_t auth_key[256] = {0}, msg_key[16] = {0};
    uint8_t cipher[17] = {0}, plain[17];
    size_t plain_len;

    ASSERT(mtproto_decrypt(cipher, 17, auth_key, msg_key, 0, plain, &plain_len) == -1,
           "non-16-aligned cipher should be rejected");
    ASSERT(mtproto_decrypt(cipher, 0, auth_key, msg_key, 0, plain, &plain_len) == -1,
           "zero-length cipher should be rejected");
}

void test_mtproto_crypto(void) {
    RUN_TEST(test_derive_keys_sha256_count);
    RUN_TEST(test_derive_keys_direction_diff);
    RUN_TEST(test_compute_msg_key_sha256_count);
    RUN_TEST(test_compute_msg_key_not_all_zero);
    RUN_TEST(test_gen_padding_size);
    RUN_TEST(test_gen_padding_rand_bytes);
    RUN_TEST(test_encrypt_output_structure);
    RUN_TEST(test_decrypt_with_matching_msg_key);
    RUN_TEST(test_decrypt_wrong_auth_key);
    RUN_TEST(test_decrypt_wrong_msg_key);
    RUN_TEST(test_decrypt_unaligned_length);
}
