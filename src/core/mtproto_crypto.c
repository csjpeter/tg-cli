/**
 * @file mtproto_crypto.c
 * @brief MTProto 2.0 encryption layer implementation.
 *
 * Implements key derivation, message encryption/decryption per
 * https://core.telegram.org/mtproto/description#defining-aes-key-and-iv
 */

#include "mtproto_crypto.h"
#include "crypto.h"
#include "ige_aes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUTH_KEY_SIZE 256
#define MSG_KEY_SIZE  16
#define AES_KEY_SIZE  32

/**
 * @brief Derive AES key and IV from auth_key and msg_key.
 *
 * MTProto 2.0 spec:
 *   sha256_a = SHA256(msg_key + auth_key[x:x+36])
 *   sha256_b = SHA256(auth_key[x+40:x+76] + msg_key)
 *   aes_key  = sha256_a[0:8]  + sha256_b[8:16] + sha256_a[24:32]
 *   aes_iv   = sha256_b[0:8]  + sha256_a[8:16] + sha256_b[24:32]
 *
 * where x = 0 for client→server, x = 8 for server→client.
 *
 * @param auth_key  256-byte authorization key.
 * @param msg_key   16-byte message key.
 * @param x         Direction offset: 0 (client→server) or 8 (server→client).
 * @param aes_key   Output: 32-byte AES key.
 * @param aes_iv    Output: 32-byte AES IV.
 */
void mtproto_derive_keys(const uint8_t *auth_key, const uint8_t *msg_key,
                         int x,
                         uint8_t *aes_key, uint8_t *aes_iv) {
    if (!auth_key || !msg_key || !aes_key || !aes_iv) return;

    uint8_t sha256_a[32], sha256_b[32];

    /* sha256_a = SHA256(msg_key + auth_key[x:x+36]) */
    {
        uint8_t buf[16 + 36];
        memcpy(buf, msg_key, 16);
        memcpy(buf + 16, auth_key + x, 36);
        crypto_sha256(buf, sizeof(buf), sha256_a);
    }

    /* sha256_b = SHA256(auth_key[x+40:x+76] + msg_key) */
    {
        uint8_t buf[36 + 16];
        memcpy(buf, auth_key + x + 40, 36);
        memcpy(buf + 36, msg_key, 16);
        crypto_sha256(buf, sizeof(buf), sha256_b);
    }

    /* aes_key = sha256_a[0:8] + sha256_b[8:16] + sha256_a[24:32] = 32 bytes */
    memcpy(aes_key,      sha256_a,      8);
    memcpy(aes_key + 8,  sha256_b + 8,  8);
    memcpy(aes_key + 16, sha256_a + 24, 8);

    /* aes_iv = sha256_b[0:8] + sha256_a[8:16] + sha256_b[24:32] = 32 bytes */
    memcpy(aes_iv,      sha256_b,      8);
    memcpy(aes_iv + 8,  sha256_a + 8,  8);
    memcpy(aes_iv + 16, sha256_b + 24, 8);
}

/**
 * @brief Compute msg_key from auth_key and plaintext.
 *
 * MTProto 2.0 spec:
 *   msg_key_large = SHA256(auth_key[88+x:88+x+32] + plaintext)
 *   msg_key = msg_key_large[8:24]
 *
 * @param auth_key  256-byte authorization key.
 * @param plain     Plaintext payload.
 * @param plain_len Plaintext length.
 * @param x         Direction offset: 0 or 8.
 * @param msg_key   Output: 16-byte message key.
 */
void mtproto_compute_msg_key(const uint8_t *auth_key,
                             const uint8_t *plain, size_t plain_len,
                             int x,
                             uint8_t *msg_key) {
    if (!auth_key || !plain || !msg_key) return;

    /* auth_key slice: 32 bytes starting at offset 88+x */
    size_t offset = (size_t)(88 + x);
    size_t buf_len = 32 + plain_len;
    uint8_t *buf = (uint8_t *)malloc(buf_len);
    if (!buf) {
        fprintf(stderr, "OOM: mtproto_compute_msg_key\n");
        abort();
    }

    memcpy(buf, auth_key + offset, 32);
    memcpy(buf + 32, plain, plain_len);

    uint8_t hash[32];
    crypto_sha256(buf, buf_len, hash);

    /* msg_key = middle 16 bytes of hash = hash[8:24] */
    memcpy(msg_key, hash + 8, MSG_KEY_SIZE);

    free(buf);
}

/**
 * @brief Generate random padding for MTProto 2.0 message.
 *
 * Padding is 12-1024 bytes, aligned so that (plain_len + padding) % 16 == 0.
 *
 * @param plain_len Plaintext length (before padding).
 * @param padding_out Output buffer for padding bytes (must be >= 1024 bytes).
 * @return Number of padding bytes generated.
 */
size_t mtproto_gen_padding(size_t plain_len, uint8_t *padding_out) {
    size_t min_pad = 12;
    size_t total = plain_len + min_pad;
    /* Align to 16-byte boundary */
    total = (total + 15) & ~(size_t)15;

    size_t pad_len = total - plain_len;
    if (pad_len > 0 && padding_out) {
        crypto_rand_bytes(padding_out, pad_len);
    }
    return pad_len;
}

/**
 * @brief Encrypt plaintext with MTProto 2.0.
 *
 * Computes msg_key, generates padding, derives AES key/IV,
 * then encrypts (plaintext + padding) with AES-256-IGE.
 *
 * @param plain     Plaintext payload.
 * @param plain_len Plaintext length.
 * @param auth_key  256-byte authorization key.
 * @param x         Direction offset: 0 or 8.
 * @param out       Output: encrypted data.
 * @param out_len   Output: encrypted length.
 */
void mtproto_encrypt(const uint8_t *plain, size_t plain_len,
                     const uint8_t *auth_key, int x,
                     uint8_t *out, size_t *out_len) {
    if (!plain || !auth_key || !out || !out_len) return;

    /* Generate padding */
    uint8_t padding[1024];
    size_t pad_len = mtproto_gen_padding(plain_len, padding);

    /* Build padded plaintext: plain + padding */
    size_t padded_len = plain_len + pad_len;
    uint8_t *padded = (uint8_t *)malloc(padded_len);
    if (!padded) {
        fprintf(stderr, "OOM: mtproto_encrypt\n");
        abort();
    }
    memcpy(padded, plain, plain_len);
    if (pad_len > 0) memcpy(padded + plain_len, padding, pad_len);

    /* Compute msg_key from padded plaintext (spec: includes padding) */
    uint8_t msg_key[MSG_KEY_SIZE];
    mtproto_compute_msg_key(auth_key, padded, padded_len, x, msg_key);

    /* Derive AES key + IV */
    uint8_t aes_key[AES_KEY_SIZE], aes_iv[AES_KEY_SIZE];
    mtproto_derive_keys(auth_key, msg_key, x, aes_key, aes_iv);

    /* AES-IGE encrypt */
    aes_ige_encrypt(padded, padded_len, aes_key, aes_iv, out);

    *out_len = padded_len;

    memset(padded, 0, padded_len);
    free(padded);
}

/**
 * @brief Decrypt ciphertext with MTProto 2.0.
 *
 * Derives AES key/IV, decrypts with AES-256-IGE, verifies msg_key.
 *
 * @param cipher     Ciphertext.
 * @param cipher_len Ciphertext length (must be multiple of 16).
 * @param auth_key   256-byte authorization key.
 * @param msg_key    16-byte message key (from the wire).
 * @param x          Direction offset: 0 or 8.
 * @param plain      Output: decrypted plaintext.
 * @param plain_len  Output: decrypted length.
 * @return 0 on success (msg_key verified), -1 on error.
 */
int mtproto_decrypt(const uint8_t *cipher, size_t cipher_len,
                    const uint8_t *auth_key, const uint8_t *msg_key,
                    int x,
                    uint8_t *plain, size_t *plain_len) {
    if (!cipher || !auth_key || !msg_key || !plain || !plain_len) return -1;
    if (cipher_len < 16 || cipher_len % 16 != 0) return -1;

    /* Derive AES key + IV */
    uint8_t aes_key[AES_KEY_SIZE], aes_iv[AES_KEY_SIZE];
    mtproto_derive_keys(auth_key, msg_key, x, aes_key, aes_iv);

    /* AES-IGE decrypt */
    aes_ige_decrypt(cipher, cipher_len, aes_key, aes_iv, plain);

    /* Verify msg_key: recompute from decrypted plaintext */
    uint8_t verify_key[MSG_KEY_SIZE];
    mtproto_compute_msg_key(auth_key, plain, cipher_len, x, verify_key);

    if (memcmp(msg_key, verify_key, MSG_KEY_SIZE) != 0) {
        return -1; /* msg_key mismatch — wrong auth_key or corrupted data */
    }

    *plain_len = cipher_len;
    return 0;
}
