/**
 * @file crypto.c
 * @brief Mock implementation of crypto.h for unit testing.
 *
 * Provides deterministic outputs and call counters so tests can verify
 * crypto usage without depending on OpenSSL.
 */

#include "crypto.h"

#include <stdlib.h>
#include <string.h>

/* ---- Internal mock state ---- */

static struct {
    int sha256_count;
    unsigned char sha256_output[32];

    int sha1_count;
    unsigned char sha1_output[20];

    int encrypt_block_count;
    int decrypt_block_count;

    int rand_bytes_count;
    unsigned char rand_bytes_buf[256];
    size_t rand_bytes_len;

    int set_encrypt_key_count;
    int set_decrypt_key_count;

    int rsa_encrypt_count;
    int rsa_encrypt_result_len;
    unsigned char rsa_encrypt_result[512];

    int bn_mod_exp_count;
    unsigned char bn_mod_exp_result[512];
    size_t bn_mod_exp_result_len;
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

int mock_crypto_sha1_call_count(void) {
    return g_mock.sha1_count;
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

int mock_crypto_rsa_encrypt_call_count(void) {
    return g_mock.rsa_encrypt_count;
}

void mock_crypto_set_rsa_encrypt_result(const unsigned char *data, size_t len) {
    if (len > sizeof(g_mock.rsa_encrypt_result)) len = sizeof(g_mock.rsa_encrypt_result);
    memcpy(g_mock.rsa_encrypt_result, data, len);
    g_mock.rsa_encrypt_result_len = (int)len;
}

int mock_crypto_bn_mod_exp_call_count(void) {
    return g_mock.bn_mod_exp_count;
}

void mock_crypto_set_bn_mod_exp_result(const unsigned char *data, size_t len) {
    if (len > sizeof(g_mock.bn_mod_exp_result)) len = sizeof(g_mock.bn_mod_exp_result);
    memcpy(g_mock.bn_mod_exp_result, data, len);
    g_mock.bn_mod_exp_result_len = len;
}

/* ---- crypto.h interface implementation ---- */

void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    (void)data;
    (void)len;
    g_mock.sha256_count++;
    memcpy(out, g_mock.sha256_output, 32);
}

void crypto_sha1(const unsigned char *data, size_t len, unsigned char *out) {
    (void)data;
    (void)len;
    g_mock.sha1_count++;
    memcpy(out, g_mock.sha1_output, 20);
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
        size_t copy = g_mock.rand_bytes_len < len ? g_mock.rand_bytes_len : len;
        memcpy(buf, g_mock.rand_bytes_buf, copy);
        if (copy < len) memset(buf + copy, 0, len - copy);
    } else {
        memset(buf, 0xAA, len);
    }
    return 0;
}

/* ---- RSA mock ---- */

struct CryptoRsaKey { int dummy; };

CryptoRsaKey *crypto_rsa_load_public(const char *pem) {
    (void)pem;
    CryptoRsaKey *key = (CryptoRsaKey *)calloc(1, sizeof(CryptoRsaKey));
    return key;
}

void crypto_rsa_free(CryptoRsaKey *key) {
    free(key);
}

int crypto_rsa_public_encrypt(CryptoRsaKey *key, const unsigned char *data,
                              size_t data_len, unsigned char *out, size_t *out_len) {
    (void)key;
    (void)data;
    (void)data_len;
    g_mock.rsa_encrypt_count++;
    if (g_mock.rsa_encrypt_result_len > 0) {
        memcpy(out, g_mock.rsa_encrypt_result, (size_t)g_mock.rsa_encrypt_result_len);
        *out_len = (size_t)g_mock.rsa_encrypt_result_len;
    } else {
        memset(out, 0xBB, 256);
        *out_len = 256;
    }
    return 0;
}

/* ---- BN mock ---- */

struct CryptoBnCtx { int dummy; };

CryptoBnCtx *crypto_bn_ctx_new(void) {
    return (CryptoBnCtx *)calloc(1, sizeof(CryptoBnCtx));
}

void crypto_bn_ctx_free(CryptoBnCtx *ctx) {
    free(ctx);
}

int crypto_bn_mod_exp(unsigned char *result, size_t *res_len,
                      const unsigned char *base, size_t base_len,
                      const unsigned char *exp, size_t exp_len,
                      const unsigned char *mod, size_t mod_len,
                      CryptoBnCtx *ctx) {
    (void)base;
    (void)exp;
    (void)mod;
    (void)ctx;
    g_mock.bn_mod_exp_count++;

    /* Mock: use configured result or fill with 0xCC */
    size_t out_len = mod_len;
    if (out_len > *res_len) return -1;
    if (g_mock.bn_mod_exp_result_len > 0) {
        size_t copy = g_mock.bn_mod_exp_result_len < out_len
                    ? g_mock.bn_mod_exp_result_len : out_len;
        memset(result, 0, out_len);
        memcpy(result + out_len - copy, g_mock.bn_mod_exp_result, copy);
    } else {
        memset(result, 0xCC, out_len);
    }
    *res_len = out_len;

    (void)base_len;
    (void)exp_len;
    return 0;
}
