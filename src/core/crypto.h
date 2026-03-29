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

/** AES key schedule storage (opaque — do not access fields directly). */
typedef struct {
    unsigned char key[32]; /**< Raw key bytes. */
    int bits;              /**< Key size in bits (128, 192, or 256). */
} CryptoAesKey;

/**
 * @brief Compute SHA-256 hash.
 * @param data Input data.
 * @param len  Length of input.
 * @param out  Output buffer (32 bytes).
 */
void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out);

/**
 * @brief Set up AES encryption key schedule.
 * @param key      AES key (16, 24, or 32 bytes).
 * @param bits     Key size in bits (128, 192, or 256).
 * @param schedule Output key schedule.
 * @return 0 on success, non-zero on error.
 */
int crypto_aes_set_encrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule);

/**
 * @brief Set up AES decryption key schedule.
 * @param key      AES key.
 * @param bits     Key size in bits.
 * @param schedule Output key schedule.
 * @return 0 on success, non-zero on error.
 */
int crypto_aes_set_decrypt_key(const unsigned char *key, int bits,
                               CryptoAesKey *schedule);

/**
 * @brief Encrypt a single 16-byte AES block (ECB mode).
 * @param in       Input block (16 bytes).
 * @param out      Output block (16 bytes).
 * @param schedule Key schedule from crypto_aes_set_encrypt_key().
 */
void crypto_aes_encrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule);

/**
 * @brief Decrypt a single 16-byte AES block (ECB mode).
 * @param in       Input block (16 bytes).
 * @param out      Output block (16 bytes).
 * @param schedule Key schedule from crypto_aes_set_decrypt_key().
 */
void crypto_aes_decrypt_block(const unsigned char *in, unsigned char *out,
                              const CryptoAesKey *schedule);

/**
 * @brief Fill buffer with cryptographically secure random bytes.
 * @param buf Output buffer.
 * @param len Number of bytes to generate.
 * @return 0 on success, non-zero on error.
 */
int crypto_rand_bytes(unsigned char *buf, size_t len);

#endif /* CRYPTO_H */
