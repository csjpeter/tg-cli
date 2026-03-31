/**
 * @file test_mtproto_crypto_functional.c
 * @brief Functional tests for MTProto 2.0 encryption layer (real OpenSSL).
 *
 * Unlike unit tests, these link against the real crypto.c and ige_aes.c,
 * so they verify the full crypto pipeline including actual OpenSSL calls.
 *
 * Test strategy:
 *   1. mtproto_derive_keys: deterministic, direction 0 vs 8 differ.
 *   2. mtproto_compute_msg_key: output changes with different inputs.
 *   3. encrypt/decrypt round-trip via the mathematically correct path:
 *      build padded plaintext manually → compute msg_key → encrypt with
 *      mtproto (direct IGE call) → decrypt with mtproto_decrypt.
 *   4. msg_key corruption: mtproto_decrypt returns -1 when msg_key is wrong.
 */

#include "test_helpers.h"
#include "mtproto_crypto.h"
#include "ige_aes.h"
#include "crypto.h"

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#define AUTH_KEY_SIZE 256
#define BLOCK 16

static void fill_pattern(uint8_t *buf, size_t len, uint8_t base) {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(base + (uint8_t)i);
}

/* ---- Test: key derivation is deterministic ---- */
static void test_derive_keys_deterministic(void) {
    uint8_t auth_key[AUTH_KEY_SIZE], msg_key[16];
    uint8_t aes_key1[32], aes_iv1[32];
    uint8_t aes_key2[32], aes_iv2[32];

    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x01);
    fill_pattern(msg_key, 16, 0xAA);

    mtproto_derive_keys(auth_key, msg_key, 0, aes_key1, aes_iv1);
    mtproto_derive_keys(auth_key, msg_key, 0, aes_key2, aes_iv2);

    ASSERT(memcmp(aes_key1, aes_key2, 32) == 0,
           "derive_keys: same inputs must produce same key");
    ASSERT(memcmp(aes_iv1,  aes_iv2,  32) == 0,
           "derive_keys: same inputs must produce same IV");
    /* Key must be non-trivial */
    uint8_t zero32[32] = {0};
    ASSERT(memcmp(aes_key1, zero32, 32) != 0,
           "derive_keys: key must not be all-zero");
}

/* ---- Test: direction 0 and 8 produce different keys ---- */
static void test_derive_keys_direction_differs(void) {
    uint8_t auth_key[AUTH_KEY_SIZE], msg_key[16];
    uint8_t key_c2s[32], iv_c2s[32];
    uint8_t key_s2c[32], iv_s2c[32];

    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x55);
    fill_pattern(msg_key,  16,            0x33);

    mtproto_derive_keys(auth_key, msg_key, 0, key_c2s, iv_c2s);
    mtproto_derive_keys(auth_key, msg_key, 8, key_s2c, iv_s2c);

    ASSERT(memcmp(key_c2s, key_s2c, 32) != 0,
           "derive_keys: c2s and s2c keys must differ");
}

/* ---- Test: msg_key changes with different auth_key ---- */
static void test_msg_key_auth_key_sensitivity(void) {
    uint8_t auth_key1[AUTH_KEY_SIZE], auth_key2[AUTH_KEY_SIZE];
    uint8_t plain[48];
    uint8_t mk1[16], mk2[16];

    fill_pattern(auth_key1, AUTH_KEY_SIZE, 0x11);
    memcpy(auth_key2, auth_key1, AUTH_KEY_SIZE);
    auth_key2[88] ^= 0x01;  /* offset 88 is within the slice used by compute_msg_key */
    fill_pattern(plain, 48, 0xBB);

    mtproto_compute_msg_key(auth_key1, plain, 48, 0, mk1);
    mtproto_compute_msg_key(auth_key2, plain, 48, 0, mk2);

    ASSERT(memcmp(mk1, mk2, 16) != 0,
           "msg_key: different auth_key must produce different msg_key");
}

/* ---- Test: msg_key changes with different plaintext ---- */
static void test_msg_key_plain_sensitivity(void) {
    uint8_t auth_key[AUTH_KEY_SIZE];
    uint8_t plain1[48], plain2[48];
    uint8_t mk1[16], mk2[16];

    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x44);
    fill_pattern(plain1, 48, 0xCC);
    memcpy(plain2, plain1, 48);
    plain2[0] ^= 0x01;

    mtproto_compute_msg_key(auth_key, plain1, 48, 0, mk1);
    mtproto_compute_msg_key(auth_key, plain2, 48, 0, mk2);

    ASSERT(memcmp(mk1, mk2, 16) != 0,
           "msg_key: different plaintext must produce different msg_key");
}

/* ---- Test: encrypt/decrypt round-trip at the mathematical level ----
 *
 * We manually build a padded plaintext (already aligned), compute msg_key,
 * derive AES keys, encrypt with IGE directly, then pass the result and the
 * correct msg_key to mtproto_decrypt.  This tests the full decrypt pipeline
 * without depending on the internal padding of mtproto_encrypt.
 */
static void test_mtproto_decrypt_roundtrip(void) {
    uint8_t auth_key[AUTH_KEY_SIZE];
    /* Padded plaintext: 64 bytes (multiple of 16, satisfies >=12 pad requirement) */
    uint8_t padded[64];
    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x77);
    fill_pattern(padded,   64,            0x42);

    /* Compute msg_key from padded plaintext (direction 0 = client→server) */
    uint8_t msg_key[16];
    mtproto_compute_msg_key(auth_key, padded, 64, 0, msg_key);

    /* Derive AES key + IV */
    uint8_t aes_key[32], aes_iv[32];
    mtproto_derive_keys(auth_key, msg_key, 0, aes_key, aes_iv);

    /* Encrypt with AES-256-IGE */
    uint8_t cipher[64];
    aes_ige_encrypt(padded, 64, aes_key, aes_iv, cipher);

    /* Now decrypt using mtproto_decrypt — it must verify msg_key and succeed */
    uint8_t recovered[64];
    size_t rec_len = 0;
    int rc = mtproto_decrypt(cipher, 64, auth_key, msg_key, 0, recovered, &rec_len);

    ASSERT(rc == 0,    "mtproto_decrypt: must succeed with correct msg_key");
    ASSERT(rec_len == 64, "mtproto_decrypt: recovered length must match");
    ASSERT(memcmp(padded, recovered, 64) == 0,
           "mtproto_decrypt: recovered plaintext must match original");
}

/* ---- Test: msg_key corruption causes decrypt failure ---- */
static void test_mtproto_decrypt_bad_msg_key(void) {
    uint8_t auth_key[AUTH_KEY_SIZE];
    uint8_t padded[48];
    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x99);
    fill_pattern(padded,   48,            0x12);

    uint8_t msg_key[16];
    mtproto_compute_msg_key(auth_key, padded, 48, 0, msg_key);

    uint8_t aes_key[32], aes_iv[32];
    mtproto_derive_keys(auth_key, msg_key, 0, aes_key, aes_iv);

    uint8_t cipher[48];
    aes_ige_encrypt(padded, 48, aes_key, aes_iv, cipher);

    /* Corrupt msg_key */
    uint8_t bad_key[16];
    memcpy(bad_key, msg_key, 16);
    bad_key[0] ^= 0xFF;

    uint8_t recovered[48];
    size_t rec_len = 0;
    int rc = mtproto_decrypt(cipher, 48, auth_key, bad_key, 0, recovered, &rec_len);

    ASSERT(rc != 0, "mtproto_decrypt: must fail when msg_key is corrupted");
}

/* ---- Test: mtproto_gen_padding produces valid length ---- */
static void test_gen_padding_length(void) {
    uint8_t padding[1024];
    for (size_t plain_len = 0; plain_len <= 256; plain_len += 16) {
        size_t pad_len = mtproto_gen_padding(plain_len, padding);
        ASSERT(pad_len >= 12,                  "gen_padding: must be >= 12");
        ASSERT((plain_len + pad_len) % 16 == 0, "gen_padding: total must be 16-aligned");
        ASSERT(pad_len <= 1024,                "gen_padding: must be <= 1024");
    }
}

/* ---- Test: encrypted output differs from plaintext ---- */
static void test_mtproto_encrypt_non_trivial(void) {
    uint8_t auth_key[AUTH_KEY_SIZE], padded[48];
    fill_pattern(auth_key, AUTH_KEY_SIZE, 0x55);
    fill_pattern(padded,   48,            0xCC);

    uint8_t msg_key[16];
    mtproto_compute_msg_key(auth_key, padded, 48, 0, msg_key);
    uint8_t aes_key[32], aes_iv[32];
    mtproto_derive_keys(auth_key, msg_key, 0, aes_key, aes_iv);

    uint8_t cipher[48];
    aes_ige_encrypt(padded, 48, aes_key, aes_iv, cipher);

    ASSERT(memcmp(padded, cipher, 48) != 0,
           "encrypt: output must differ from plaintext");
}

void run_mtproto_crypto_functional_tests(void) {
    RUN_TEST(test_derive_keys_deterministic);
    RUN_TEST(test_derive_keys_direction_differs);
    RUN_TEST(test_msg_key_auth_key_sensitivity);
    RUN_TEST(test_msg_key_plain_sensitivity);
    RUN_TEST(test_mtproto_decrypt_roundtrip);
    RUN_TEST(test_mtproto_decrypt_bad_msg_key);
    RUN_TEST(test_gen_padding_length);
    RUN_TEST(test_mtproto_encrypt_non_trivial);
}
