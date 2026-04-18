/**
 * @file mtproto_crypto.h
 * @brief MTProto 2.0 message encryption / decryption.
 *
 * Implements the crypto scheme from:
 *   https://core.telegram.org/mtproto#protocol-description
 *
 * direction:  0 = client→server (encrypt),  8 = server→client (decrypt)
 */

#ifndef MTPROTO_CRYPTO_H
#define MTPROTO_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Derive AES-256 key + IV from auth_key and msg_key.
 * @param auth_key  256-byte auth key.
 * @param msg_key   16-byte message key.
 * @param direction 0 (client→server) or 8 (server→client).
 * @param aes_key   Output 32-byte AES key.
 * @param aes_iv    Output 32-byte AES IV.
 */
void mtproto_derive_keys(const uint8_t *auth_key, const uint8_t *msg_key,
                         int direction,
                         uint8_t *aes_key, uint8_t *aes_iv);

/**
 * @brief Compute msg_key = middle 16 bytes of SHA256(auth_key_slice + plaintext).
 * @param auth_key  256-byte auth key.
 * @param plain     Plaintext data.
 * @param plain_len Length of plaintext.
 * @param direction 0 or 8.
 * @param msg_key   Output 16-byte message key.
 */
void mtproto_compute_msg_key(const uint8_t *auth_key,
                             const uint8_t *plain, size_t plain_len,
                             int direction,
                             uint8_t *msg_key);

/**
 * @brief Generate random padding bytes (12..1024, aligned to 16).
 * @param plain_len  Length of plaintext.
 * @param padding_out Output buffer (at least 1024 bytes).
 * @return Number of padding bytes generated.
 */
size_t mtproto_gen_padding(size_t plain_len, uint8_t *padding_out);

/**
 * @brief Encrypt a plaintext message (MTProto 2.0).
 *
 * Pads plaintext → computes msg_key over (plain + padding) → derives AES
 * keys → AES-IGE encrypts. The msg_key the caller must put on the wire is
 * returned in @p msg_key_out — the spec requires it be computed over the
 * padded plaintext (which this function synthesises internally), so the
 * caller cannot derive it independently.
 *
 * @param plain       Plaintext.
 * @param plain_len   Plaintext length.
 * @param auth_key    256-byte auth key.
 * @param direction   0 or 8.
 * @param out         Output buffer (must hold plain_len + 1024 bytes).
 * @param out_len     Receives total output length.
 * @param msg_key_out Receives the 16-byte msg_key used to encrypt.
 */
void mtproto_encrypt(const uint8_t *plain, size_t plain_len,
                     const uint8_t *auth_key, int direction,
                     uint8_t *out, size_t *out_len,
                     uint8_t msg_key_out[16]);

/**
 * @brief Decrypt an MTProto 2.0 message.
 *
 * Derives AES keys → AES-IGE decrypt → verifies msg_key.
 *
 * @param cipher     Ciphertext.
 * @param cipher_len Ciphertext length (must be multiple of 16).
 * @param auth_key   256-byte auth key.
 * @param msg_key    16-byte message key.
 * @param direction  0 or 8.
 * @param plain      Output buffer (same size as cipher).
 * @param plain_len  Receives plaintext length (equals cipher_len).
 * @return 0 on success (msg_key verified), -1 on error.
 */
int mtproto_decrypt(const uint8_t *cipher, size_t cipher_len,
                    const uint8_t *auth_key, const uint8_t *msg_key,
                    int direction,
                    uint8_t *plain, size_t *plain_len);

#endif /* MTPROTO_CRYPTO_H */
