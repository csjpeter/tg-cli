/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

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
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <limits.h>
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
    /* QA-18: guard against size_t → int truncation that would leave the
     * tail of `buf` uninitialized. The project never asks for > INT_MAX
     * bytes in practice, so treat it as an impossible condition and abort,
     * matching the project's abort-on-impossible policy. */
    if (len > INT_MAX) {
        fprintf(stderr, "crypto_rand_bytes: len too large\n");
        abort();
    }
    return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}

/* ---- SHA-1 ---- */

void crypto_sha1(const unsigned char *data, size_t len, unsigned char *out) {
    if (EVP_Digest(data, len, out, NULL, EVP_sha1(), NULL) != 1) {
        fprintf(stderr, "crypto: EVP_Digest(sha1) failed\n");
        abort();
    }
}

void crypto_sha512(const unsigned char *data, size_t len, unsigned char *out) {
    if (EVP_Digest(data, len, out, NULL, EVP_sha512(), NULL) != 1) {
        fprintf(stderr, "crypto: EVP_Digest(sha512) failed\n");
        abort();
    }
}

int crypto_pbkdf2_hmac_sha512(const unsigned char *password, size_t password_len,
                              const unsigned char *salt, size_t salt_len,
                              int iters,
                              unsigned char *out, size_t out_len) {
    if (!password || !salt || !out || out_len == 0 || iters <= 0) return -1;
    if (password_len > INT_MAX || salt_len > INT_MAX || out_len > INT_MAX)
        return -1;
    int rc = PKCS5_PBKDF2_HMAC((const char *)password, (int)password_len,
                                salt, (int)salt_len, iters,
                                EVP_sha512(), (int)out_len, out);
    return rc == 1 ? 0 : -1;
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

CryptoRsaKey *crypto_rsa_load_private(const char *pem) {
    if (!pem) return NULL;

    BIO *bio = BIO_new_mem_buf(pem, (int)strlen(pem));
    if (!bio) return NULL;

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!pkey) return NULL;

    CryptoRsaKey *key = (CryptoRsaKey *)calloc(1, sizeof(CryptoRsaKey));
    if (!key) { EVP_PKEY_free(pkey); return NULL; }
    key->pkey = pkey;
    return key;
}

int crypto_rsa_private_decrypt(CryptoRsaKey *key, const unsigned char *data,
                               size_t data_len, unsigned char *out, size_t *out_len) {
    if (!key || !data || !out || !out_len) return -1;

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key->pkey, NULL);
    if (!ctx) return -1;

    if (EVP_PKEY_decrypt_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    /* RSA_NO_PADDING — mirrors the RSA_PAD scheme used by the client */
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_NO_PADDING) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    if (EVP_PKEY_decrypt(ctx, out, out_len, data, data_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    EVP_PKEY_CTX_free(ctx);
    return 0;
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

/* ---- RSA fingerprint ---- */

/*
 * Telegram fingerprint: lower 64 bits (little-endian) of
 *   SHA1( LE32(n_len) || n_BE || LE32(e_len) || e_BE )
 *
 * Supports both PKCS#1 ("BEGIN RSA PUBLIC KEY") and
 * PKCS#8 ("BEGIN PUBLIC KEY") PEM formats.
 * Uses the OpenSSL 3.0 EVP / OSSL_PARAM API throughout — no deprecated calls.
 */
int crypto_rsa_fingerprint(const char *pem, uint64_t *out) {
    if (!pem || !out) return -1;

    /* Load the public key via EVP (works for both PEM subtypes). */
    BIO *bio = BIO_new_mem_buf(pem, (int)strlen(pem));
    if (!bio) return -1;
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return -1;

    /* Extract n and e as BIGNUMs using the provider-neutral OSSL_PARAM API. */
    BIGNUM *n = NULL, *e = NULL;
    if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n) != 1 ||
        EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e) != 1) {
        BN_free(n);
        BN_free(e);
        EVP_PKEY_free(pkey);
        return -1;
    }
    EVP_PKEY_free(pkey);

    int n_len = BN_num_bytes(n);
    int e_len = BN_num_bytes(e);
    if (n_len <= 0 || e_len <= 0) {
        BN_free(n); BN_free(e);
        return -1;
    }

    /* Build TL-serialized blob: LE32(n_len) + n_BE + LE32(e_len) + e_BE */
    size_t buf_len = 4 + (size_t)n_len + 4 + (size_t)e_len;
    unsigned char *buf = malloc(buf_len);
    if (!buf) { BN_free(n); BN_free(e); return -1; }

    /* LE32 for n_len */
    buf[0] = (unsigned char)(n_len & 0xFF);
    buf[1] = (unsigned char)((n_len >> 8) & 0xFF);
    buf[2] = (unsigned char)((n_len >> 16) & 0xFF);
    buf[3] = (unsigned char)((n_len >> 24) & 0xFF);
    BN_bn2bin(n, buf + 4);

    /* LE32 for e_len */
    size_t off = 4 + (size_t)n_len;
    buf[off + 0] = (unsigned char)(e_len & 0xFF);
    buf[off + 1] = (unsigned char)((e_len >> 8) & 0xFF);
    buf[off + 2] = (unsigned char)((e_len >> 16) & 0xFF);
    buf[off + 3] = (unsigned char)((e_len >> 24) & 0xFF);
    BN_bn2bin(e, buf + off + 4);

    BN_free(n);
    BN_free(e);

    /* SHA1 → take lower 64 bits (last 8 bytes of 20-byte digest). */
    unsigned char sha1_out[20];
    crypto_sha1(buf, buf_len, sha1_out);
    free(buf);

    /* Lower 64 bits = bytes [12..19] interpreted as little-endian. */
    uint64_t fp = 0;
    for (int i = 0; i < 8; i++) {
        fp |= ((uint64_t)sha1_out[12 + i]) << (8 * i);
    }
    *out = fp;
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

/* Shared epilogue for mod_mul/add/sub: BN_bn2bin() into left-padded out. */
static int bn_op_finalize(BIGNUM *bn_res,
                           unsigned char *out, size_t *out_len,
                           size_t pad_len) {
    int bytes = BN_num_bytes(bn_res);
    if ((size_t)bytes > *out_len) return -1;
    size_t actual = (size_t)bytes;
    if (actual < pad_len) memset(out, 0, pad_len - actual);
    BN_bn2bin(bn_res, out + (pad_len - actual));
    *out_len = pad_len;
    return 0;
}

typedef int (*bn_bin_op)(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
                          const BIGNUM *m, BN_CTX *ctx);

static int bn_mod_op(bn_bin_op op,
                      unsigned char *result, size_t *res_len,
                      const unsigned char *a, size_t a_len,
                      const unsigned char *b, size_t b_len,
                      const unsigned char *m, size_t m_len,
                      CryptoBnCtx *ctx) {
    if (!result || !res_len || !a || !b || !m || !ctx) return -1;
    BIGNUM *ba = BN_bin2bn(a, (int)a_len, NULL);
    BIGNUM *bb = BN_bin2bn(b, (int)b_len, NULL);
    BIGNUM *bm = BN_bin2bn(m, (int)m_len, NULL);
    BIGNUM *br = BN_new();
    int rc = -1;
    if (ba && bb && bm && br && op(br, ba, bb, bm, ctx->ctx) == 1) {
        rc = bn_op_finalize(br, result, res_len, m_len);
    }
    BN_free(ba); BN_free(bb); BN_free(bm); BN_free(br);
    return rc;
}

int crypto_bn_mod_mul(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx) {
    return bn_mod_op(BN_mod_mul, result, res_len,
                      a, a_len, b, b_len, m, m_len, ctx);
}

int crypto_bn_mod_add(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx) {
    return bn_mod_op(BN_mod_add, result, res_len,
                      a, a_len, b, b_len, m, m_len, ctx);
}

int crypto_bn_mod_sub(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx) {
    return bn_mod_op(BN_mod_sub, result, res_len,
                      a, a_len, b, b_len, m, m_len, ctx);
}

int crypto_bn_ucmp(const unsigned char *a, size_t a_len,
                    const unsigned char *b, size_t b_len) {
    BIGNUM *ba = BN_bin2bn(a, (int)a_len, NULL);
    BIGNUM *bb = BN_bin2bn(b, (int)b_len, NULL);
    int r = 0;
    if (ba && bb) r = BN_ucmp(ba, bb);
    BN_free(ba); BN_free(bb);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}
