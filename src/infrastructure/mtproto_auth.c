/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file mtproto_auth.c
 * @brief MTProto DH auth key generation.
 *
 * Implements the 8-step DH key exchange to generate an auth_key.
 * Uses: PQ factorization (Pollard's rho), RSA_PAD, AES-IGE, SHA-1/SHA-256.
 */

#include "mtproto_auth.h"
#include "crypto.h"
#include "ige_aes.h"
#include "mtproto_rpc.h"
#include "tl_serial.h"
#include "telegram_server_key.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

/* ---- TL Constructor IDs ---- */
#define CRC_req_pq_multi          0xbe7e8ef1
#define CRC_resPQ                 0x05162463
#define CRC_p_q_inner_data_dc     0xa9f55f95
#define CRC_req_DH_params         0xd712e4be
#define CRC_server_DH_params_ok   0xd0e8075c
#define CRC_server_DH_inner_data  0xb5890dba
#define CRC_client_DH_inner_data  0x6643b654
#define CRC_set_client_DH_params  0xf5045f1f
#define CRC_dh_gen_ok             0x3bcbf734
#define CRC_dh_gen_retry          0x46dc1fb9
#define CRC_dh_gen_fail           0xa69dae02

/* Maximum number of RSA fingerprints accepted in a ResPQ vector.
 * Telegram uses ≤8 in practice; cap at 64 to reject DoS from untrusted servers
 * during the unauthenticated DH phase. */
#define MAX_FP_COUNT 64

/* ---- Big-endian byte helpers ---- */

/** Encode uint64 as big-endian bytes, stripping leading zeros. */
static size_t uint64_to_be(uint64_t val, uint8_t *out) {
    uint8_t tmp[8];
    for (int i = 7; i >= 0; i--) {
        tmp[i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    /* Skip leading zeros */
    size_t start = 0;
    while (start < 7 && tmp[start] == 0) start++;
    size_t len = 8 - start;
    memcpy(out, tmp + start, len);
    return len;
}

/** Encode uint32 as big-endian bytes, stripping leading zeros. */
static size_t uint32_to_be(uint32_t val, uint8_t *out) {
    uint8_t tmp[4];
    tmp[0] = (uint8_t)((val >> 24) & 0xFF);
    tmp[1] = (uint8_t)((val >> 16) & 0xFF);
    tmp[2] = (uint8_t)((val >>  8) & 0xFF);
    tmp[3] = (uint8_t)( val        & 0xFF);
    size_t start = 0;
    while (start < 3 && tmp[start] == 0) start++;
    size_t len = 4 - start;
    memcpy(out, tmp + start, len);
    return len;
}

/** Decode big-endian bytes to uint64. */
static uint64_t be_to_uint64(const uint8_t *data, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) | data[i];
    }
    return val;
}

/* ---- RSA_PAD (OAEP variant for Telegram) ---- */

static int rsa_pad_encrypt(CryptoRsaKey *rsa_key,
                           const uint8_t *data, size_t data_len,
                           uint8_t *out, size_t *out_len) {
    if (!rsa_key || !data || !out || !out_len) return -1;
    /* 32 SHA256 + data + padding = 192; data must fit: ≤ 160 bytes. */
    if (data_len > 160) return -1;

    /* Step 1: Build 192-byte block: SHA256(data) + data + random_padding */
    uint8_t sha256_data[32];
    crypto_sha256(data, data_len, sha256_data);

    uint8_t padded[192];
    memcpy(padded, sha256_data, 32);
    memcpy(padded + 32, data, data_len);
    size_t pad_start = 32 + data_len;
    if (pad_start < 192) {
        crypto_rand_bytes(padded + pad_start, 192 - pad_start);
    }

    /* Step 2: Reverse */
    uint8_t reversed[192];
    for (int i = 0; i < 192; i++) {
        reversed[i] = padded[191 - i];
    }

    /* The resulting `rsa_input` must be numerically < RSA modulus to be
     * a valid NO_PADDING input. Since temp_key (and hence the MSB of the
     * 256-byte buffer) is random, ~half of calls would hit >= modulus
     * and EVP_PKEY_encrypt would reject them. TDLib retries with fresh
     * temp_key until the result is valid — mirror that here. Bounded to
     * 32 attempts to guarantee forward progress in pathological cases
     * (e.g. a mock key with a very low-high-bit modulus). */
    for (int attempt = 0; attempt < 32; ++attempt) {
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

        if (crypto_rsa_public_encrypt(rsa_key, rsa_input, 256,
                                      out, out_len) == 0) {
            return 0;
        }
        /* Retry: RSA_NO_PADDING rejects inputs >= modulus, which
         * happens for roughly half of random 256-byte buffers. */
    }
    logger_log(LOG_ERROR,
               "auth: rsa_pad_encrypt exhausted 32 retries — RSA key anomaly");
    return -1;
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

    /* Try small primes first for quick factorization */
    if (pq % 2 == 0) {
        *p_out = 2;
        *q_out = (uint32_t)(pq / 2);
        if (*p_out > *q_out) {
            uint32_t tmp = *p_out; *p_out = *q_out; *q_out = tmp;
        }
        return 0;
    }

    /* Pollard's rho algorithm with multiple attempts.
     * Uses __uint128_t to avoid overflow in (x*x) for 64-bit pq. */
    for (uint64_t c = 1; c < 20; c++) {
        uint64_t x = 2, y = 2, d = 1;
        int steps = 0;

        while (d == 1 && steps < 1000000) {
            x = (uint64_t)(((__uint128_t)x * x + c) % pq);
            y = (uint64_t)(((__uint128_t)y * y + c) % pq);
            y = (uint64_t)(((__uint128_t)y * y + c) % pq);
            steps++;

            /* GCD(|x-y|, pq) */
            uint64_t a = x > y ? x - y : y - x;
            if (a == 0) break; /* x == y, try different c */
            uint64_t b = pq;
            while (b != 0) {
                uint64_t t = b;
                b = a % b;
                a = t;
            }
            d = a;
        }

        if (d != 1 && d != pq) {
            uint64_t p = d;
            uint64_t q = pq / d;
            if (p > q) { uint64_t tmp = p; p = q; q = tmp; }
            if (p > UINT32_MAX || q > UINT32_MAX) {
                logger_log(LOG_ERROR,
                           "pq_factorize: factor exceeds 2^32 (p=%llu q=%llu) — "
                           "server input is invalid",
                           (unsigned long long)p, (unsigned long long)q);
                return -1;
            }
            *p_out = (uint32_t)p;
            *q_out = (uint32_t)q;
            return 0;
        }
    }

    return -1;
}

/* ---- Step 1: req_pq_multi → ResPQ ---- */

int auth_step_req_pq(AuthKeyCtx *ctx) {
    if (!ctx || !ctx->transport || !ctx->session) return -1;

    /* Generate random nonce */
    crypto_rand_bytes(ctx->nonce, 16);

    /* Build req_pq_multi TL */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_req_pq_multi);
    tl_write_int128(&w, ctx->nonce);

    int rc = rpc_send_unencrypted(ctx->session, ctx->transport, w.data, w.len);
    tl_writer_free(&w);
    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: failed to send req_pq_multi");
        return -1;
    }

    /* Receive ResPQ */
    uint8_t buf[4096];
    size_t buf_len = 0;
    rc = rpc_recv_unencrypted(ctx->session, ctx->transport, buf, sizeof(buf), &buf_len);
    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: failed to receive ResPQ");
        return -1;
    }

    /* Parse ResPQ */
    TlReader r = tl_reader_init(buf, buf_len);

    uint32_t constructor = tl_read_uint32(&r);
    if (constructor != CRC_resPQ) {
        logger_log(LOG_ERROR, "auth: unexpected constructor 0x%08x", constructor);
        return -1;
    }

    /* Verify nonce */
    uint8_t recv_nonce[16];
    tl_read_int128(&r, recv_nonce);
    if (memcmp(recv_nonce, ctx->nonce, 16) != 0) {
        logger_log(LOG_ERROR, "auth: nonce mismatch in ResPQ");
        return -1;
    }

    /* Server nonce */
    tl_read_int128(&r, ctx->server_nonce);

    /* PQ as bytes (big-endian) */
    size_t pq_len = 0;
    RAII_STRING uint8_t *pq_bytes = tl_read_bytes(&r, &pq_len);
    if (!pq_bytes) {
        logger_log(LOG_ERROR, "auth: failed to read pq bytes");
        return -1;
    }
    ctx->pq = be_to_uint64(pq_bytes, pq_len);
    /* pq_bytes freed automatically by RAII_STRING */

    /* Vector of fingerprints */
    uint32_t vec_crc = tl_read_uint32(&r); /* vector constructor */
    (void)vec_crc;
    uint32_t fp_count = tl_read_uint32(&r);

    if (fp_count > MAX_FP_COUNT) {
        logger_log(LOG_ERROR, "auth: fp_count %u exceeds cap %u — rejecting",
                   fp_count, MAX_FP_COUNT);
        return -1;
    }

    int found_fp = 0;
    for (uint32_t i = 0; i < fp_count; i++) {
        uint64_t fp = tl_read_uint64(&r);
        if (fp == telegram_server_key_get_fingerprint()) {
            found_fp = 1;
        }
    }

    if (!found_fp) {
        logger_log(LOG_ERROR, "auth: no matching RSA fingerprint");
        return -1;
    }

    logger_log(LOG_INFO, "auth: ResPQ received, pq=%llu",
               (unsigned long long)ctx->pq);
    return 0;
}

/* ---- Step 2: req_DH_params ---- */

int auth_step_req_dh(AuthKeyCtx *ctx) {
    if (!ctx || !ctx->transport || !ctx->session) return -1;

    /* Factorize PQ */
    if (pq_factorize(ctx->pq, &ctx->p, &ctx->q) != 0) {
        logger_log(LOG_ERROR, "auth: PQ factorization failed");
        return -1;
    }
    /* Generate new_nonce */
    crypto_rand_bytes(ctx->new_nonce, 32);

    /* Encode p, q, pq as big-endian bytes */
    uint8_t pq_be[8];
    size_t pq_be_len = uint64_to_be(ctx->pq, pq_be);
    uint8_t p_be[4];
    size_t p_be_len = uint32_to_be(ctx->p, p_be);
    uint8_t q_be[4];
    size_t q_be_len = uint32_to_be(ctx->q, q_be);

    /* Build p_q_inner_data_dc */
    TlWriter inner;
    tl_writer_init(&inner);
    tl_write_uint32(&inner, CRC_p_q_inner_data_dc);
    tl_write_bytes(&inner, pq_be, pq_be_len);
    tl_write_bytes(&inner, p_be, p_be_len);
    tl_write_bytes(&inner, q_be, q_be_len);
    tl_write_int128(&inner, ctx->nonce);
    tl_write_int128(&inner, ctx->server_nonce);
    tl_write_int256(&inner, ctx->new_nonce);
    tl_write_int32(&inner, ctx->dc_id);

    /* RSA_PAD encrypt */
    CryptoRsaKey *rsa_key = crypto_rsa_load_public(telegram_server_key_get_pem());
    if (!rsa_key) {
        tl_writer_free(&inner);
        logger_log(LOG_ERROR, "auth: failed to load RSA key");
        return -1;
    }

    uint8_t encrypted[256];
    size_t enc_len = 0;
    int rc = rsa_pad_encrypt(rsa_key, inner.data, inner.len, encrypted, &enc_len);
    crypto_rsa_free(rsa_key);
    tl_writer_free(&inner);

    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: RSA_PAD encrypt failed");
        return -1;
    }
    /* Build req_DH_params */
    uint64_t fp_val = telegram_server_key_get_fingerprint();

    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_req_DH_params);
    tl_write_int128(&w, ctx->nonce);
    tl_write_int128(&w, ctx->server_nonce);
    tl_write_bytes(&w, p_be, p_be_len);
    tl_write_bytes(&w, q_be, q_be_len);
    tl_write_uint64(&w, fp_val);
    tl_write_bytes(&w, encrypted, enc_len);

    rc = rpc_send_unencrypted(ctx->session, ctx->transport, w.data, w.len);
    tl_writer_free(&w);

    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: failed to send req_DH_params");
        return -1;
    }

    logger_log(LOG_INFO, "auth: req_DH_params sent, p=%u q=%u", ctx->p, ctx->q);
    return 0;
}

/* ---- Step 3: Parse server_DH_params_ok ---- */

int auth_step_parse_dh(AuthKeyCtx *ctx) {
    if (!ctx || !ctx->transport || !ctx->session) return -1;

    /* Receive server_DH_params */
    uint8_t buf[4096];
    size_t buf_len = 0;
    int rc = rpc_recv_unencrypted(ctx->session, ctx->transport,
                                  buf, sizeof(buf), &buf_len);
    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: failed to receive server_DH_params");
        return -1;
    }

    TlReader r = tl_reader_init(buf, buf_len);

    uint32_t constructor = tl_read_uint32(&r);
    if (constructor != CRC_server_DH_params_ok) {
        logger_log(LOG_ERROR, "auth: unexpected constructor 0x%08x", constructor);
        return -1;
    }

    /* Verify nonces */
    uint8_t recv_nonce[16];
    tl_read_int128(&r, recv_nonce);
    if (memcmp(recv_nonce, ctx->nonce, 16) != 0) {
        logger_log(LOG_ERROR, "auth: nonce mismatch in server_DH_params");
        return -1;
    }

    uint8_t recv_server_nonce[16];
    tl_read_int128(&r, recv_server_nonce);
    if (memcmp(recv_server_nonce, ctx->server_nonce, 16) != 0) {
        logger_log(LOG_ERROR, "auth: server_nonce mismatch");
        return -1;
    }

    /* Read encrypted_answer */
    size_t enc_answer_len = 0;
    RAII_STRING uint8_t *enc_answer = tl_read_bytes(&r, &enc_answer_len);
    if (!enc_answer || enc_answer_len == 0) {
        logger_log(LOG_ERROR, "auth: failed to read encrypted_answer");
        return -1;
    }

    /* Derive temp AES key/IV */
    dh_derive_temp_aes(ctx->new_nonce, ctx->server_nonce,
                       ctx->tmp_aes_key, ctx->tmp_aes_iv);

    /* Decrypt answer */
    RAII_STRING uint8_t *decrypted = (uint8_t *)malloc(enc_answer_len);
    if (!decrypted) return -1;
    aes_ige_decrypt(enc_answer, enc_answer_len,
                    ctx->tmp_aes_key, ctx->tmp_aes_iv, decrypted);
    /* enc_answer freed automatically by RAII_STRING */

    /* Parse decrypted: skip 20-byte SHA1 hash, then server_DH_inner_data */
    if (enc_answer_len < 20 + 4) {
        return -1;
    }

    TlReader inner = tl_reader_init(decrypted + 20, enc_answer_len - 20);

    uint32_t inner_crc = tl_read_uint32(&inner);
    if (inner_crc != CRC_server_DH_inner_data) {
        logger_log(LOG_ERROR, "auth: wrong inner constructor 0x%08x", inner_crc);
        return -1;
    }

    /* Verify inner nonces */
    uint8_t inner_nonce[16];
    tl_read_int128(&inner, inner_nonce);
    if (memcmp(inner_nonce, ctx->nonce, 16) != 0) {
        return -1;
    }

    uint8_t inner_sn[16];
    tl_read_int128(&inner, inner_sn);
    if (memcmp(inner_sn, ctx->server_nonce, 16) != 0) {
        return -1;
    }

    ctx->g = tl_read_int32(&inner);

    /* MTProto spec: g must be one of {2, 3, 4, 5, 6, 7}. */
    if (ctx->g < 2 || ctx->g > 7) {
        logger_log(LOG_ERROR, "auth: invalid DH g=%d (must be 2–7)", ctx->g);
        return -1;
    }

    /* dh_prime as bytes */
    size_t prime_len = 0;
    RAII_STRING uint8_t *prime_bytes = tl_read_bytes(&inner, &prime_len);
    if (!prime_bytes || prime_len > sizeof(ctx->dh_prime)) return -1;
    memcpy(ctx->dh_prime, prime_bytes, prime_len);
    ctx->dh_prime_len = prime_len;
    /* prime_bytes freed automatically by RAII_STRING */

    /* g_a as bytes */
    size_t ga_len = 0;
    RAII_STRING uint8_t *ga_bytes = tl_read_bytes(&inner, &ga_len);
    if (!ga_bytes || ga_len > sizeof(ctx->g_a)) return -1;
    memcpy(ctx->g_a, ga_bytes, ga_len);
    ctx->g_a_len = ga_len;
    /* ga_bytes freed automatically by RAII_STRING */

    ctx->server_time = tl_read_int32(&inner);

    /* decrypted is freed automatically by RAII_STRING */
    logger_log(LOG_INFO, "auth: DH params parsed, g=%d, prime_len=%zu",
               ctx->g, ctx->dh_prime_len);
    return 0;
}

/* ---- Step 4: set_client_DH_params → dh_gen_ok ---- */

int auth_step_set_client_dh(AuthKeyCtx *ctx) {
    if (!ctx || !ctx->transport || !ctx->session) return -1;

    /* Generate random b (256 bytes) */
    crypto_rand_bytes(ctx->b, 256);

    /* Compute g_b = pow(g, b) mod dh_prime */
    uint8_t g_be[4];
    size_t g_be_len = uint32_to_be((uint32_t)ctx->g, g_be);

    uint8_t g_b[256];
    size_t g_b_len = sizeof(g_b);
    CryptoBnCtx *bn_ctx = crypto_bn_ctx_new();
    if (!bn_ctx) return -1;

    int rc = crypto_bn_mod_exp(g_b, &g_b_len, g_be, g_be_len,
                                ctx->b, 256,
                                ctx->dh_prime, ctx->dh_prime_len, bn_ctx);
    if (rc != 0) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_ERROR, "auth: g_b computation failed");
        return -1;
    }

    /* Build client_DH_inner_data */
    TlWriter inner;
    tl_writer_init(&inner);
    tl_write_uint32(&inner, CRC_client_DH_inner_data);
    tl_write_int128(&inner, ctx->nonce);
    tl_write_int128(&inner, ctx->server_nonce);
    tl_write_int64(&inner, 0); /* retry_id = 0 */
    tl_write_bytes(&inner, g_b, g_b_len);

    /* Prepend SHA1 hash + pad to multiple of 16 */
    uint8_t sha1_hash[20];
    crypto_sha1(inner.data, inner.len, sha1_hash);

    size_t data_with_hash_len = 20 + inner.len;
    /* Pad to 16-byte boundary */
    size_t padded_len = data_with_hash_len;
    if (padded_len % 16 != 0) {
        padded_len += 16 - (padded_len % 16);
    }

    RAII_STRING uint8_t *padded = (uint8_t *)calloc(1, padded_len);
    if (!padded) {
        tl_writer_free(&inner);
        crypto_bn_ctx_free(bn_ctx);
        return -1;
    }
    memcpy(padded, sha1_hash, 20);
    memcpy(padded + 20, inner.data, inner.len);
    /* Fill padding with random bytes */
    if (padded_len > data_with_hash_len) {
        crypto_rand_bytes(padded + data_with_hash_len,
                          padded_len - data_with_hash_len);
    }
    tl_writer_free(&inner);

    /* Encrypt with temp AES key/IV */
    RAII_STRING uint8_t *encrypted = (uint8_t *)malloc(padded_len);
    if (!encrypted) {
        crypto_bn_ctx_free(bn_ctx);
        return -1;
    }
    aes_ige_encrypt(padded, padded_len, ctx->tmp_aes_key, ctx->tmp_aes_iv,
                    encrypted);

    /* Build set_client_DH_params */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_set_client_DH_params);
    tl_write_int128(&w, ctx->nonce);
    tl_write_int128(&w, ctx->server_nonce);
    tl_write_bytes(&w, encrypted, padded_len);
    /* encrypted and padded freed automatically by RAII_STRING */

    rc = rpc_send_unencrypted(ctx->session, ctx->transport, w.data, w.len);
    tl_writer_free(&w);
    if (rc != 0) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_ERROR, "auth: failed to send set_client_DH_params");
        return -1;
    }

    /* Receive response */
    uint8_t buf[4096];
    size_t buf_len = 0;
    rc = rpc_recv_unencrypted(ctx->session, ctx->transport,
                              buf, sizeof(buf), &buf_len);
    if (rc != 0) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_ERROR, "auth: failed to receive DH gen response");
        return -1;
    }

    TlReader r = tl_reader_init(buf, buf_len);
    uint32_t constructor = tl_read_uint32(&r);

    if (constructor == CRC_dh_gen_retry) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_WARN, "auth: dh_gen_retry");
        return -1;
    }
    if (constructor == CRC_dh_gen_fail) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_ERROR, "auth: dh_gen_fail");
        return -1;
    }
    if (constructor != CRC_dh_gen_ok) {
        crypto_bn_ctx_free(bn_ctx);
        logger_log(LOG_ERROR, "auth: unexpected constructor 0x%08x", constructor);
        return -1;
    }

    /* Verify nonce in dh_gen_ok */
    uint8_t recv_nonce[16];
    tl_read_int128(&r, recv_nonce);
    if (memcmp(recv_nonce, ctx->nonce, 16) != 0) {
        crypto_bn_ctx_free(bn_ctx);
        return -1;
    }

    /* Read server_nonce and new_nonce_hash1 (to be verified below). */
    uint8_t recv_sn[16];
    tl_read_int128(&r, recv_sn);
    uint8_t new_nonce_hash[16];
    tl_read_int128(&r, new_nonce_hash);

    /* Compute auth_key = pow(g_a, b) mod dh_prime */
    uint8_t auth_key[256];
    size_t ak_len = sizeof(auth_key);
    rc = crypto_bn_mod_exp(auth_key, &ak_len, ctx->g_a, ctx->g_a_len,
                            ctx->b, 256,
                            ctx->dh_prime, ctx->dh_prime_len, bn_ctx);
    crypto_bn_ctx_free(bn_ctx);

    if (rc != 0) {
        logger_log(LOG_ERROR, "auth: auth_key computation failed");
        return -1;
    }

    /* Set auth_key on session (pad to 256 bytes if needed) */
    uint8_t auth_key_padded[256];
    memset(auth_key_padded, 0, sizeof(auth_key_padded));
    if (ak_len <= 256) {
        memcpy(auth_key_padded + (256 - ak_len), auth_key, ak_len);
    }

    /* QA-12: verify new_nonce_hash1 =
     *   last 128 bits of SHA1(new_nonce[32] || 0x01 || auth_key_aux_hash[8])
     * where auth_key_aux_hash = SHA1(auth_key)[0:8].
     * Without this check a MITM could substitute auth_key during DH
     * exchange without detection. */
    {
        uint8_t ak_full_hash[20];
        crypto_sha1(auth_key_padded, 256, ak_full_hash);
        /* auth_key_aux_hash = ak_full_hash[0:8] */

        uint8_t nonce_hash_input[32 + 1 + 8];
        memcpy(nonce_hash_input,          ctx->new_nonce, 32);
        nonce_hash_input[32] = 0x01; /* dh_gen_ok marker */
        memcpy(nonce_hash_input + 33,     ak_full_hash, 8);

        uint8_t expected_full[20];
        crypto_sha1(nonce_hash_input, sizeof(nonce_hash_input), expected_full);
        /* last 16 bytes of the SHA1 result */
        if (memcmp(expected_full + 4, new_nonce_hash, 16) != 0) {
            logger_log(LOG_ERROR,
                       "auth: new_nonce_hash1 mismatch — possible MITM");
            return -1;
        }
    }

    mtproto_session_set_auth_key(ctx->session, auth_key_padded);

    /* Compute server_salt = new_nonce[0:8] XOR server_nonce[0:8] */
    uint64_t salt = 0;
    for (int i = 0; i < 8; i++) {
        ((uint8_t *)&salt)[i] = ctx->new_nonce[i] ^ ctx->server_nonce[i];
    }
    mtproto_session_set_salt(ctx->session, salt);

    logger_log(LOG_INFO, "auth: DH key exchange complete, auth_key set");
    return 0;
}

/* ---- Auth Key Generation (orchestrator) ---- */

int mtproto_auth_key_gen(Transport *t, MtProtoSession *s) {
    if (!t || !s) return -1;

    logger_log(LOG_INFO, "Starting DH auth key generation...");

    AuthKeyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = t;
    ctx.session = s;
    ctx.dc_id = t->dc_id;

    if (auth_step_req_pq(&ctx) != 0) return -1;
    if (auth_step_req_dh(&ctx) != 0) return -1;
    if (auth_step_parse_dh(&ctx) != 0) return -1;
    if (auth_step_set_client_dh(&ctx) != 0) return -1;

    logger_log(LOG_INFO, "Auth key generation complete");
    return 0;
}
