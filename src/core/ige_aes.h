/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file ige_aes.h
 * @brief AES-256-IGE (Infinite Garble Extension) encryption mode.
 *
 * IGE is a block cipher mode used by MTProto 2.0.  It guarantees that any
 * change to the ciphertext affects the decryption of all subsequent blocks
 * (forward propagation).  Not available in OpenSSL directly — built on top
 * of single-block ECB via crypto_aes_encrypt_block / crypto_aes_decrypt_block.
 *
 * IV is 32 bytes: the first 16 bytes are the "previous ciphertext" IV,
 * the second 16 bytes are the "previous plaintext" IV.
 */

#ifndef IGE_AES_H
#define IGE_AES_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encrypt data using AES-256-IGE mode.
 * @param plain  Plaintext (must be multiple of 16 bytes).
 * @param len    Length of plaintext in bytes.
 * @param key    AES-256 key (32 bytes).
 * @param iv     IV (32 bytes: first 16 = iv_c, second 16 = iv_p).
 * @param cipher Output ciphertext (same length as plaintext).
 */
void aes_ige_encrypt(const uint8_t *plain, size_t len,
                     const uint8_t *key, const uint8_t *iv,
                     uint8_t *cipher);

/**
 * @brief Decrypt data using AES-256-IGE mode.
 * @param cipher Ciphertext (must be multiple of 16 bytes).
 * @param len    Length of ciphertext in bytes.
 * @param key    AES-256 key (32 bytes).
 * @param iv     IV (32 bytes: first 16 = iv_c, second 16 = iv_p).
 * @param plain  Output plaintext (same length as ciphertext).
 */
void aes_ige_decrypt(const uint8_t *cipher, size_t len,
                     const uint8_t *key, const uint8_t *iv,
                     uint8_t *plain);

#endif /* IGE_AES_H */
