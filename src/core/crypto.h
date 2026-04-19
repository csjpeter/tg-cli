/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file crypto.h
 * @brief Thin wrappers around OpenSSL crypto primitives.
 *
 * All crypto operations go through these wrappers so that tests can link
 * a mock implementation (tests/mocks/crypto.c) instead of OpenSSL.
 * See ADR-0004 (Dependency Inversion).
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* ---- AES ---- */

/** AES key schedule storage (opaque — do not access fields directly). */
typedef struct {
    unsigned char key[32]; /**< Raw key bytes. */
    int bits;              /**< Key size in bits (128, 192, or 256). */
} CryptoAesKey;

int  crypto_aes_set_encrypt_key(const unsigned char *key, int bits,
                                CryptoAesKey *schedule);
int  crypto_aes_set_decrypt_key(const unsigned char *key, int bits,
                                CryptoAesKey *schedule);
void crypto_aes_encrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule);
void crypto_aes_decrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule);

/* ---- Hash ---- */

/** Compute SHA-256 hash (32 bytes output). */
void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out);

/** Compute SHA-1 hash (20 bytes output). */
void crypto_sha1(const unsigned char *data, size_t len, unsigned char *out);

/** Compute SHA-512 hash (64 bytes output). */
void crypto_sha512(const unsigned char *data, size_t len, unsigned char *out);

/**
 * @brief PBKDF2 with HMAC-SHA-512.
 *
 * Used by Telegram's 2FA SRP: PH2 = pbkdf2(PH1, salt1, 100000, 64) where
 * PH1 = SHA-256(salt1 || password || salt1) already happened in the caller.
 *
 * @param password      Input password bytes (not NUL-terminated).
 * @param password_len  Password length.
 * @param salt          Salt bytes.
 * @param salt_len      Salt length.
 * @param iters         Iteration count (Telegram uses 100000).
 * @param out           Output buffer of @p out_len bytes.
 * @param out_len       Output length (64 for SRP PH2).
 * @return 0 on success, -1 on error.
 */
int crypto_pbkdf2_hmac_sha512(const unsigned char *password, size_t password_len,
                              const unsigned char *salt, size_t salt_len,
                              int iters,
                              unsigned char *out, size_t out_len);

/* ---- Random ---- */

/** Fill buffer with cryptographically secure random bytes. */
int crypto_rand_bytes(unsigned char *buf, size_t len);

/* ---- RSA ---- */

/** RSA public key object (opaque). */
typedef struct CryptoRsaKey CryptoRsaKey;

/** Load RSA public key from PEM string. Returns NULL on error. */
CryptoRsaKey *crypto_rsa_load_public(const char *pem);

/** Free RSA key. */
void crypto_rsa_free(CryptoRsaKey *key);

/**
 * RSA-encrypt with OAEP-like padding (Telegram's RSA_PAD).
 * @param key        RSA public key.
 * @param data       Data to encrypt.
 * @param data_len   Data length.
 * @param out        Output buffer (must hold key size bytes, e.g. 256).
 * @param out_len    Receives output length.
 * @return 0 on success, -1 on error.
 */
int crypto_rsa_public_encrypt(CryptoRsaKey *key, const unsigned char *data,
                              size_t data_len, unsigned char *out, size_t *out_len);

/* ---- Big Number Arithmetic (for DH) ---- */

/** Big number context (opaque). */
typedef struct CryptoBnCtx CryptoBnCtx;

/** Create BN context. */
CryptoBnCtx *crypto_bn_ctx_new(void);

/** Free BN context. */
void crypto_bn_ctx_free(CryptoBnCtx *ctx);

/**
 * Modular exponentiation: result = (base ^ exp) mod modulus.
 * @param result  Output big number (bytes, big-endian).
 * @param res_len Output buffer size / actual length.
 * @param base    Base (big-endian bytes).
 * @param base_len Base length.
 * @param exp     Exponent (big-endian bytes).
 * @param exp_len Exponent length.
 * @param mod     Modulus (big-endian bytes).
 * @param mod_len Modulus length.
 * @param ctx     BN context.
 * @return 0 on success, -1 on error.
 */
int crypto_bn_mod_exp(unsigned char *result, size_t *res_len,
                       const unsigned char *base, size_t base_len,
                       const unsigned char *exp, size_t exp_len,
                       const unsigned char *mod, size_t mod_len,
                       CryptoBnCtx *ctx);

/**
 * Modular multiplication: result = (a * b) mod m. */
int crypto_bn_mod_mul(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx);

/**
 * Modular addition: result = (a + b) mod m. */
int crypto_bn_mod_add(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx);

/**
 * Modular subtraction: result = (a - b) mod m, always non-negative. */
int crypto_bn_mod_sub(unsigned char *result, size_t *res_len,
                       const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       const unsigned char *m, size_t m_len,
                       CryptoBnCtx *ctx);

/**
 * Compare a and b (big-endian, unsigned). Returns -1, 0, or 1. */
int crypto_bn_ucmp(const unsigned char *a, size_t a_len,
                    const unsigned char *b, size_t b_len);

#endif /* CRYPTO_H */
