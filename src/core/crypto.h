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

#endif /* CRYPTO_H */
