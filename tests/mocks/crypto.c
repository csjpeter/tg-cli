/**
 * @file crypto.c
 * @brief Mock implementation of crypto.h for unit testing.
 *
 * Provides deterministic outputs and call counters so tests can verify
 * crypto usage without depending on OpenSSL.
 */

#include "crypto.h"

#include <string.h>
#include <stdint.h>

/* ---- Internal mock state ---- */

static struct {
    int sha256_count;
    unsigned char sha256_output[32];

    int encrypt_block_count;
    int decrypt_block_count;

    int rand_bytes_count;
    unsigned char rand_bytes_buf[256];
    size_t rand_bytes_len;

    int set_encrypt_key_count;
    int set_decrypt_key_count;
} g_mock;

/* ---- Test accessor functions ---- */

void mock_crypto_reset(void) {
    memset(&g_mock, 0, sizeof(g_mock));
}

void mock_crypto_set_sha256_output(const unsigned char hash[32]) {
    memcpy(g_mock.sha256_output, hash, 32);
}

int mock_crypto_sha256_call_count(void) {
    return g_mock.sha256_count;
}

int mock_crypto_encrypt_block_call_count(void) {
    return g_mock.encrypt_block_count;
}

int mock_crypto_decrypt_block_call_count(void) {
    return g_mock.decrypt_block_count;
}

int mock_crypto_rand_bytes_call_count(void) {
    return g_mock.rand_bytes_count;
}

void mock_crypto_set_rand_bytes(const unsigned char *buf, size_t len) {
    if (len > sizeof(g_mock.rand_bytes_buf)) len = sizeof(g_mock.rand_bytes_buf);
    memcpy(g_mock.rand_bytes_buf, buf, len);
    g_mock.rand_bytes_len = len;
}

int mock_crypto_set_encrypt_key_call_count(void) {
    return g_mock.set_encrypt_key_count;
}

/* ---- crypto.h interface implementation ---- */

void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    (void)data;
    (void)len;
    g_mock.sha256_count++;
    memcpy(out, g_mock.sha256_output, 32);
}

int crypto_aes_set_encrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule) {
    (void)key;
    (void)bits;
    (void)schedule;
    g_mock.set_encrypt_key_count++;
    return 0;
}

int crypto_aes_set_decrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule) {
    (void)key;
    (void)bits;
    (void)schedule;
    g_mock.set_decrypt_key_count++;
    return 0;
}

void crypto_aes_encrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule) {
    (void)schedule;
    g_mock.encrypt_block_count++;
    /* Identity: copy input to output so tests can track block chaining */
    memcpy(out, in, 16);
}

void crypto_aes_decrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule) {
    (void)schedule;
    g_mock.decrypt_block_count++;
    memcpy(out, in, 16);
}

int crypto_rand_bytes(unsigned char *buf, size_t len) {
    g_mock.rand_bytes_count++;
    if (g_mock.rand_bytes_len > 0) {
        size_t copy = g_mock.rand_bytes_len < len
                    ? g_mock.rand_bytes_len : len;
        memcpy(buf, g_mock.rand_bytes_buf, copy);
        if (copy < len) memset(buf + copy, 0, len - copy);
    } else {
        memset(buf, 0xAA, len);
    }
    return 0;
}
