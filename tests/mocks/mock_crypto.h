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

#endif /* MOCK_CRYPTO_H */
