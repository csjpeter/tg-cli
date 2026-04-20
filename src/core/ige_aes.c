/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file ige_aes.c
 * @brief AES-256-IGE implementation built on single-block ECB primitives.
 *
 * IGE mode for each 16-byte block:
 *   encrypt: c[i] = aes_encrypt(p[i] XOR iv_prev_c) XOR iv_prev_p
 *            iv_prev_c = c[i],  iv_prev_p = p[i]
 *   decrypt: p[i] = aes_decrypt(c[i] XOR iv_prev_p) XOR iv_prev_c
 *            iv_prev_p = p[i],  iv_prev_c = c[i]
 */

#include "ige_aes.h"
#include "crypto.h"

#include <string.h>

#define BLOCK_SIZE 16

void aes_ige_encrypt(const uint8_t *plain, size_t len,
                     const uint8_t *key, const uint8_t *iv,
                     uint8_t *cipher) {
    if (!plain || !key || !iv || !cipher || len == 0) return;
    if (len % 16 != 0) return;

    CryptoAesKey schedule;
    crypto_aes_set_encrypt_key(key, 256, &schedule);

    /* Split IV: first 16 = iv_c, second 16 = iv_p */
    uint8_t iv_c[BLOCK_SIZE], iv_p[BLOCK_SIZE];
    memcpy(iv_c, iv, BLOCK_SIZE);
    memcpy(iv_p, iv + BLOCK_SIZE, BLOCK_SIZE);

    uint8_t buf[BLOCK_SIZE];

    for (size_t off = 0; off < len; off += BLOCK_SIZE) {
        /* buf = plain[i] XOR iv_c */
        for (int j = 0; j < BLOCK_SIZE; j++)
            buf[j] = plain[off + j] ^ iv_c[j];

        /* encrypt block */
        crypto_aes_encrypt_block(buf, cipher + off, &schedule);

        /* cipher[i] ^= iv_p */
        for (int j = 0; j < BLOCK_SIZE; j++)
            cipher[off + j] ^= iv_p[j];

        /* update IVs */
        memcpy(iv_c, cipher + off, BLOCK_SIZE);
        memcpy(iv_p, plain + off, BLOCK_SIZE);
    }
}

void aes_ige_decrypt(const uint8_t *cipher, size_t len,
                     const uint8_t *key, const uint8_t *iv,
                     uint8_t *plain) {
    if (!cipher || !key || !iv || !plain || len == 0) return;
    if (len % 16 != 0) return;

    CryptoAesKey schedule;
    crypto_aes_set_decrypt_key(key, 256, &schedule);

    uint8_t iv_c[BLOCK_SIZE], iv_p[BLOCK_SIZE];
    memcpy(iv_c, iv, BLOCK_SIZE);
    memcpy(iv_p, iv + BLOCK_SIZE, BLOCK_SIZE);

    uint8_t buf[BLOCK_SIZE];

    for (size_t off = 0; off < len; off += BLOCK_SIZE) {
        /* buf = cipher[i] XOR iv_p */
        for (int j = 0; j < BLOCK_SIZE; j++)
            buf[j] = cipher[off + j] ^ iv_p[j];

        /* decrypt block */
        crypto_aes_decrypt_block(buf, plain + off, &schedule);

        /* plain[i] ^= iv_c */
        for (int j = 0; j < BLOCK_SIZE; j++)
            plain[off + j] ^= iv_c[j];

        /* update IVs */
        memcpy(iv_p, plain + off, BLOCK_SIZE);
        memcpy(iv_c, cipher + off, BLOCK_SIZE);
    }
}
