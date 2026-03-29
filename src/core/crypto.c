/**
 * @file crypto.c
 * @brief Production OpenSSL implementation of crypto.h wrappers.
 *
 * Uses EVP interfaces for OpenSSL 3.0+ compatibility.
 * AES block operations use EVP with ECB mode and no padding.
 */

#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    EVP_Digest(data, len, out, NULL, EVP_sha256(), NULL);
}

int crypto_aes_set_encrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule) {
    size_t key_len = (size_t)bits / 8;
    memcpy(schedule->key, key, key_len);
    schedule->bits = bits;
    return 0;
}

int crypto_aes_set_decrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule) {
    size_t key_len = (size_t)bits / 8;
    memcpy(schedule->key, key, key_len);
    schedule->bits = bits;
    return 0;
}

static const EVP_CIPHER *aes_ecb_cipher(int bits) {
    if (bits == 128)      return EVP_aes_128_ecb();
    else if (bits == 192) return EVP_aes_192_ecb();
    else                  return EVP_aes_256_ecb();
}

void crypto_aes_encrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, aes_ecb_cipher(schedule->bits),
                       NULL, schedule->key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    int out_len = 0;
    EVP_EncryptUpdate(ctx, out, &out_len, in, 16);
    EVP_EncryptFinal_ex(ctx, out + out_len, &out_len);
    EVP_CIPHER_CTX_free(ctx);
}

void crypto_aes_decrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, aes_ecb_cipher(schedule->bits),
                       NULL, schedule->key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    int out_len = 0;
    EVP_DecryptUpdate(ctx, out, &out_len, in, 16);
    EVP_DecryptFinal_ex(ctx, out + out_len, &out_len);
    EVP_CIPHER_CTX_free(ctx);
}

int crypto_rand_bytes(unsigned char *buf, size_t len) {
    return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}
