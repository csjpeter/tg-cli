/**
 * @file mock_crypto.h
 * @brief Test accessor functions for the mock crypto implementation.
 *
 * Include this in test files that link against tests/mocks/crypto.c.
 */

#ifndef MOCK_CRYPTO_H
#define MOCK_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/** Reset all mock counters and state. Call before each test. */
void mock_crypto_reset(void);

/** Set the deterministic SHA-256 output (32 bytes). */
void mock_crypto_set_sha256_output(const unsigned char hash[32]);

/** Get number of times crypto_sha256() was called. */
int mock_crypto_sha256_call_count(void);

/** Get number of times crypto_aes_encrypt_block() was called. */
int mock_crypto_encrypt_block_call_count(void);

/** Get number of times crypto_aes_decrypt_block() was called. */
int mock_crypto_decrypt_block_call_count(void);

/** Get number of times crypto_rand_bytes() was called. */
int mock_crypto_rand_bytes_call_count(void);

/** Set deterministic random bytes output. */
void mock_crypto_set_rand_bytes(const unsigned char *buf, size_t len);

/** Get number of times crypto_aes_set_encrypt_key() was called. */
int mock_crypto_set_encrypt_key_call_count(void);

/** Get number of times crypto_sha1() was called. */
int mock_crypto_sha1_call_count(void);

/** Get number of times crypto_rsa_public_encrypt() was called. */
int mock_crypto_rsa_encrypt_call_count(void);

/** Set deterministic RSA encrypt output. */
void mock_crypto_set_rsa_encrypt_result(const unsigned char *data, size_t len);

/** Get number of times crypto_bn_mod_exp() was called. */
int mock_crypto_bn_mod_exp_call_count(void);

/** Set deterministic BN mod exp result. */
void mock_crypto_set_bn_mod_exp_result(const unsigned char *data, size_t len);

/** Get number of times crypto_sha512() was called. */
int mock_crypto_sha512_call_count(void);

/** Set deterministic SHA-512 output (64 bytes). */
void mock_crypto_set_sha512_output(const unsigned char hash[64]);

/** Get number of times crypto_pbkdf2_hmac_sha512() was called. */
int mock_crypto_pbkdf2_call_count(void);

/** Set deterministic PBKDF2 output. */
void mock_crypto_set_pbkdf2_output(const unsigned char *buf, size_t len);

#endif /* MOCK_CRYPTO_H */
