/**
 * @file mtproto_crypto.c
 * @brief MTProto 2.0 encryption layer implementation.
 */

#include "mtproto_crypto.h"
#include "crypto.h"
#include "ige_aes.h"

#include <stdlib.h>
#include <string.h>

#define AUTH_KEY_SIZE 256
#define MSG_KEY_SIZE  16
#define AES_KEY_SIZE  32

void mtproto_derive_keys(const uint8_t *auth_key, const uint8_t *msg_key,
                         int direction,
                         uint8_t *aes_key, uint8_t *aes_iv) {
    if (!auth_key || !msg_key || !aes_key || !aes_iv) return;

    uint8_t sha256_a[32], sha256_b[32];

    /* sha256_a = SHA256(auth_key[direction..direction+96] + msg_key) */
    {
        uint8_t buf[96 + 16];
        memcpy(buf, auth_key + direction, 96);
        memcpy(buf + 96, msg_key, 16);
        crypto_sha256(buf, 112, sha256_a);
    }

    /* sha256_b = SHA256(msg_key + auth_key[direction..direction+48]) */
    {
        uint8_t buf[16 + 48];
        memcpy(buf, msg_key, 16);
        memcpy(buf + 16, auth_key + direction, 48);
        crypto_sha256(buf, 64, sha256_b);
    }

    /* aes_key = sha256_a[0:8] + sha256_b[8:24] */
    memcpy(aes_key, sha256_a, 8);
    memcpy(aes_key + 8, sha256_b + 8, 24);

    /* aes_iv = sha256_a[8:24] + sha256_b[0:8] + auth_key[direction+16..direction+24] */
    memcpy(aes_iv, sha256_a + 8, 24);
    memcpy(aes_iv + 24, sha256_b, 8);
}

void mtproto_compute_msg_key(const uint8_t *auth_key,
                             const uint8_t *plain, size_t plain_len,
                             int direction,
                             uint8_t *msg_key) {
    if (!auth_key || !plain || !msg_key) return;

    /* SHA256(auth_key[direction..direction+plain_len] + plain) */
    /* Wait, that's wrong. Let me check the spec again.
       MTProto 2.0: msg_key = SHA256(auth_key[88+direction..] + plaintext)[8:24]
       So it's auth_key starting from offset (88+direction), not direction.
       Let me use the correct offset. */
    size_t offset = (size_t)(88 + direction);

    /* buf = auth_key[offset..256] + plain */
    size_t auth_tail = AUTH_KEY_SIZE - offset;
    size_t buf_len = auth_tail + plain_len;
    uint8_t *buf = (uint8_t *)malloc(buf_len);
    if (!buf) return;

    memcpy(buf, auth_key + offset, auth_tail);
    memcpy(buf + auth_tail, plain, plain_len);

    uint8_t hash[32];
    crypto_sha256(buf, buf_len, hash);

    /* msg_key = middle 16 bytes of hash = hash[8:24] */
    memcpy(msg_key, hash + 8, MSG_KEY_SIZE);

    free(buf);
}

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

void mtproto_encrypt(const uint8_t *plain, size_t plain_len,
                     const uint8_t *auth_key, int direction,
                     uint8_t *out, size_t *out_len) {
    if (!plain || !auth_key || !out || !out_len) return;

    /* Compute msg_key from plaintext */
    uint8_t msg_key[MSG_KEY_SIZE];
    mtproto_compute_msg_key(auth_key, plain, plain_len, direction, msg_key);

    /* Generate padding */
    uint8_t padding[1024];
    size_t pad_len = mtproto_gen_padding(plain_len, padding);

    /* Build padded plaintext: plain + padding */
    size_t padded_len = plain_len + pad_len;
    uint8_t *padded = (uint8_t *)malloc(padded_len);
    if (!padded) return;
    memcpy(padded, plain, plain_len);
    if (pad_len > 0) memcpy(padded + plain_len, padding, pad_len);

    /* Derive AES key + IV */
    uint8_t aes_key[AES_KEY_SIZE], aes_iv[AES_KEY_SIZE];
    mtproto_derive_keys(auth_key, msg_key, direction, aes_key, aes_iv);

    /* AES-IGE encrypt */
    aes_ige_encrypt(padded, padded_len, aes_key, aes_iv, out);

    *out_len = padded_len;

    memset(padded, 0, padded_len);
    free(padded);
}

int mtproto_decrypt(const uint8_t *cipher, size_t cipher_len,
                    const uint8_t *auth_key, const uint8_t *msg_key,
                    int direction,
                    uint8_t *plain, size_t *plain_len) {
    if (!cipher || !auth_key || !msg_key || !plain || !plain_len) return -1;
    if (cipher_len < 16 || cipher_len % 16 != 0) return -1;

    /* Derive AES key + IV */
    uint8_t aes_key[AES_KEY_SIZE], aes_iv[AES_KEY_SIZE];
    mtproto_derive_keys(auth_key, msg_key, direction, aes_key, aes_iv);

    /* AES-IGE decrypt */
    aes_ige_decrypt(cipher, cipher_len, aes_key, aes_iv, plain);

    /* Verify msg_key: recompute from decrypted plaintext */
    uint8_t verify_key[MSG_KEY_SIZE];
    mtproto_compute_msg_key(auth_key, plain, cipher_len, direction, verify_key);

    if (memcmp(msg_key, verify_key, MSG_KEY_SIZE) != 0) {
        return -1; /* msg_key mismatch — wrong auth_key or corrupted data */
    }

    *plain_len = cipher_len;
    return 0;
}
