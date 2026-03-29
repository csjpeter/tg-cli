/**
 * @file mtproto_auth.c
 * @brief MTProto DH auth key generation.
 *
 * Implements the 9-step DH key exchange to generate an auth_key.
 * Uses: PQ factorization (Pollard's rho), RSA_PAD, AES-IGE, SHA-1/SHA-256.
 */

#include "mtproto_auth.h"
#include "crypto.h"
#include "ige_aes.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

/* ---- TL Constructor IDs ---- */
#define CRC_req_pq_multi          0xbe7e8ef1
#define CRC_resPQ                 0x05162463
#define CRC_p_q_inner_data_dc     0xb936a01a
#define CRC_req_DH_params         0xd712e4be
#define CRC_server_DH_params_ok   0xd0e8075c
#define CRC_server_DH_inner_data  0xb5890dba
#define CRC_set_client_DH_params  0xf5045f1f
#define CRC_dh_gen_ok             0x3bcbf734
#define CRC_dh_gen_retry          0x46dc1fb9
#define CRC_dh_gen_fail           0xa69dae02

/* ---- RSA_PAD (OAEP variant for Telegram) ---- */

static int rsa_pad_encrypt(CryptoRsaKey *rsa_key,
                           const uint8_t *data, size_t data_len,
                           uint8_t *out, size_t *out_len) {
    if (!rsa_key || !data || !out || !out_len) return -1;
    if (data_len > 144) return -1;

    /* Step 1: Pad data to 192 bytes with random padding */
    uint8_t padded[192];
    memset(padded, 0, sizeof(padded));
    memcpy(padded, data, data_len);
    if (data_len < 192) {
        crypto_rand_bytes(padded + data_len, 192 - data_len);
    }

    /* Step 2: Reverse */
    uint8_t reversed[192];
    for (int i = 0; i < 192; i++) {
        reversed[i] = padded[191 - i];
    }

    /* Step 3: Random temp_key (32 bytes) */
    uint8_t temp_key[32];
    crypto_rand_bytes(temp_key, 32);

    /* Step 4: data_with_hash = reversed + SHA256(temp_key + reversed) */
    uint8_t temp_key_and_reversed[32 + 192];
    memcpy(temp_key_and_reversed, temp_key, 32);
    memcpy(temp_key_and_reversed + 32, reversed, 192);

    uint8_t hash[32];
    crypto_sha256(temp_key_and_reversed, sizeof(temp_key_and_reversed), hash);

    uint8_t data_with_hash[224]; /* 192 + 32 */
    memcpy(data_with_hash, reversed, 192);
    memcpy(data_with_hash + 192, hash, 32);

    /* Step 5: AES-256-IGE encrypt data_with_hash with temp_key, zero IV */
    uint8_t zero_iv[32];
    memset(zero_iv, 0, 32);

    uint8_t aes_encrypted[224];
    aes_ige_encrypt(data_with_hash, 224, temp_key, zero_iv, aes_encrypted);

    /* Step 6: temp_key_xor = temp_key XOR SHA256(aes_encrypted) */
    uint8_t aes_hash[32];
    crypto_sha256(aes_encrypted, 224, aes_hash);

    uint8_t temp_key_xor[32];
    for (int i = 0; i < 32; i++) {
        temp_key_xor[i] = temp_key[i] ^ aes_hash[i];
    }

    /* Step 7: RSA(temp_key_xor + aes_encrypted) = 32 + 224 = 256 bytes */
    uint8_t rsa_input[256];
    memcpy(rsa_input, temp_key_xor, 32);
    memcpy(rsa_input + 32, aes_encrypted, 224);

    return crypto_rsa_public_encrypt(rsa_key, rsa_input, 256, out, out_len);
}

/* ---- DH temp key derivation ---- */

static void dh_derive_temp_aes(const uint8_t new_nonce[32],
                               const uint8_t server_nonce[16],
                               uint8_t *tmp_aes_key,  /* 32 bytes */
                               uint8_t *tmp_aes_iv)   /* 32 bytes */
{
    uint8_t buf[64];

    /* SHA1(new_nonce + server_nonce) */
    memcpy(buf, new_nonce, 32);
    memcpy(buf + 32, server_nonce, 16);
    uint8_t sha1_a[20];
    crypto_sha1(buf, 48, sha1_a);

    /* SHA1(server_nonce + new_nonce) */
    memcpy(buf, server_nonce, 16);
    memcpy(buf + 16, new_nonce, 32);
    uint8_t sha1_b[20];
    crypto_sha1(buf, 48, sha1_b);

    /* tmp_aes_key = sha1_a(20) + sha1_b[0:12] = 32 bytes */
    memcpy(tmp_aes_key, sha1_a, 20);
    memcpy(tmp_aes_key + 20, sha1_b, 12);

    /* tmp_aes_iv = sha1_b[12:8] + SHA1(new_nonce+new_nonce) + new_nonce[0:4] */
    uint8_t sha1_c[20];
    memcpy(buf, new_nonce, 32);
    memcpy(buf + 32, new_nonce, 32);
    crypto_sha1(buf, 64, sha1_c);

    memcpy(tmp_aes_iv, sha1_b + 12, 8);
    memcpy(tmp_aes_iv + 8, sha1_c, 20);
    memcpy(tmp_aes_iv + 28, new_nonce, 4);
}

/* ---- PQ Factorization (Pollard's rho) ---- */

int pq_factorize(uint64_t pq, uint32_t *p_out, uint32_t *q_out) {
    if (pq < 2 || !p_out || !q_out) return -1;

    /* Pollard's rho algorithm */
    uint64_t x = 2, y = 2, d = 1;

    while (d == 1) {
        x = ((x * x) + 1) % pq;
        y = ((y * y) + 1) % pq;
        y = ((y * y) + 1) % pq;

        /* GCD(|x-y|, pq) */
        uint64_t a = x > y ? x - y : y - x;
        uint64_t b = pq;
        while (b != 0) {
            uint64_t t = b;
            b = a % b;
            a = t;
        }
        d = a;
    }

    if (d == pq) return -1;

    uint64_t p = d;
    uint64_t q = pq / d;

    if (p > q) { uint64_t tmp = p; p = q; q = tmp; }

    *p_out = (uint32_t)p;
    *q_out = (uint32_t)q;
    return 0;
}

/* ---- Auth Key Generation ---- */

int mtproto_auth_key_gen(Transport *t, MtProtoSession *s) {
    if (!t || !s) return -1;

    logger_log(LOG_INFO, "Starting DH auth key generation...");

    /*
     * Full DH key exchange (9 steps):
     *
     * 1. Send req_pq_multi(nonce)
     * 2. Receive ResPQ → extract pq, server_nonce, fingerprint
     * 3. Factorize pq → p, q
     * 4. Build P_Q_inner_data, RSA_PAD encrypt, send req_DH_params
     * 5. Receive Server_DH_Params, decrypt with temp AES key
     * 6. Generate b, compute g_b, send set_client_DH_params
     * 7. Receive dh_gen_ok → compute auth_key = pow(g_a, b) mod dh_prime
     * 8. Compute server_salt = new_nonce[0:8] XOR server_nonce[0:8]
     *
     * This requires careful TL message construction/parsing.
     * Skeleton implemented — full flow requires testing against real server.
     */

    (void)rsa_pad_encrypt;
    (void)dh_derive_temp_aes;

    logger_log(LOG_WARN, "Auth key generation: stub — not yet fully implemented");
    return -1;
}
