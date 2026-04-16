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
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    if (EVP_Digest(data, len, out, NULL, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "crypto: EVP_Digest(sha256) failed\n");
        abort();
    }
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
    if (!ctx) { fprintf(stderr, "OOM: EVP_CIPHER_CTX_new\n"); abort(); }
    EVP_EncryptInit_ex(ctx, aes_ecb_cipher(schedule->bits),
                       NULL, schedule->key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    int out_len = 0;
    if (EVP_EncryptUpdate(ctx, out, &out_len, in, 16) != 1) {
        fprintf(stderr, "crypto: EVP_EncryptUpdate failed\n");
        abort();
    }
    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx, out + out_len, &final_len) != 1) {
        fprintf(stderr, "crypto: EVP_EncryptFinal_ex failed\n");
        abort();
    }
    EVP_CIPHER_CTX_free(ctx);
}

void crypto_aes_decrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { fprintf(stderr, "OOM: EVP_CIPHER_CTX_new\n"); abort(); }
    EVP_DecryptInit_ex(ctx, aes_ecb_cipher(schedule->bits),
                       NULL, schedule->key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    int out_len = 0;
    if (EVP_DecryptUpdate(ctx, out, &out_len, in, 16) != 1) {
        fprintf(stderr, "crypto: EVP_DecryptUpdate failed\n");
        abort();
    }
    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx, out + out_len, &final_len) != 1) {
        fprintf(stderr, "crypto: EVP_DecryptFinal_ex failed\n");
        abort();
    }
    EVP_CIPHER_CTX_free(ctx);
}

int crypto_rand_bytes(unsigned char *buf, size_t len) {
    return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}

/* ---- SHA-1 ---- */

void crypto_sha1(const unsigned char *data, size_t len, unsigned char *out) {
    if (EVP_Digest(data, len, out, NULL, EVP_sha1(), NULL) != 1) {
        fprintf(stderr, "crypto: EVP_Digest(sha1) failed\n");
        abort();
    }
}

/* ---- RSA (OpenSSL 3.0 EVP API) ---- */

struct CryptoRsaKey {
    EVP_PKEY *pkey;
};

CryptoRsaKey *crypto_rsa_load_public(const char *pem) {
    if (!pem) return NULL;

    BIO *bio = BIO_new_mem_buf(pem, (int)strlen(pem));
    if (!bio) return NULL;

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!pkey) return NULL;

    CryptoRsaKey *key = (CryptoRsaKey *)calloc(1, sizeof(CryptoRsaKey));
    if (!key) { EVP_PKEY_free(pkey); return NULL; }
    key->pkey = pkey;
    return key;
}

void crypto_rsa_free(CryptoRsaKey *key) {
    if (key) {
        EVP_PKEY_free(key->pkey);
        free(key);
    }
}

int crypto_rsa_public_encrypt(CryptoRsaKey *key, const unsigned char *data,
                              size_t data_len, unsigned char *out, size_t *out_len) {
    if (!key || !data || !out || !out_len) return -1;

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, NULL);
    if (!ctx) return -1;

    if (EVP_PKEY_encrypt_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    /* RSA_NO_PADDING — Telegram uses its own RSA_PAD scheme on top */
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_NO_PADDING) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    if (EVP_PKEY_encrypt(ctx, out, out_len, data, data_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    EVP_PKEY_CTX_free(ctx);
    return 0;
}

/* ---- Big Number Arithmetic ---- */

struct CryptoBnCtx {
    BN_CTX *ctx;
};

CryptoBnCtx *crypto_bn_ctx_new(void) {
    CryptoBnCtx *c = (CryptoBnCtx *)calloc(1, sizeof(CryptoBnCtx));
    if (!c) return NULL;
    c->ctx = BN_CTX_new();
    if (!c->ctx) { free(c); return NULL; }
    return c;
}

void crypto_bn_ctx_free(CryptoBnCtx *ctx) {
    if (ctx) {
        BN_CTX_free(ctx->ctx);
        free(ctx);
    }
}

int crypto_bn_mod_exp(unsigned char *result, size_t *res_len,
                      const unsigned char *base, size_t base_len,
                      const unsigned char *exp, size_t exp_len,
                      const unsigned char *mod, size_t mod_len,
                      CryptoBnCtx *ctx) {
    if (!result || !res_len || !base || !exp || !mod || !ctx) return -1;

    BIGNUM *bn_base = BN_bin2bn(base, (int)base_len, NULL);
    BIGNUM *bn_exp  = BN_bin2bn(exp, (int)exp_len, NULL);
    BIGNUM *bn_mod  = BN_bin2bn(mod, (int)mod_len, NULL);
    BIGNUM *bn_res  = BN_new();

    if (!bn_base || !bn_exp || !bn_mod || !bn_res) {
        BN_free(bn_base); BN_free(bn_exp); BN_free(bn_mod); BN_free(bn_res);
        return -1;
    }

    int rc = BN_mod_exp(bn_res, bn_base, bn_exp, bn_mod, ctx->ctx);
    if (rc != 1) {
        BN_free(bn_base); BN_free(bn_exp); BN_free(bn_mod); BN_free(bn_res);
        return -1;
    }

    int bytes = BN_num_bytes(bn_res);
    if ((size_t)bytes > *res_len) {
        BN_free(bn_base); BN_free(bn_exp); BN_free(bn_mod); BN_free(bn_res);
        return -1;
    }

    /* Left-pad with zeros to fill mod_len */
    size_t actual = (size_t)bytes;
    if (actual < mod_len) {
        memset(result, 0, mod_len - actual);
    }
    BN_bn2bin(bn_res, result + (mod_len - actual));
    *res_len = mod_len;

    BN_free(bn_base); BN_free(bn_exp); BN_free(bn_mod); BN_free(bn_res);
    return 0;
}
