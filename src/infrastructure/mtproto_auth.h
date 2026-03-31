/**
 * @file mtproto_auth.h
 * @brief MTProto 2.0 auth key generation via DH key exchange.
 *
 * 9-step process:
 *   1. req_pq_multi     → ResPQ (nonce, server_nonce, pq, fingerprints)
 *   2. PQ factorization  → p, q (Pollard's rho)
 *   3. req_DH_params     → Server_DH_Params (encrypted with RSA_PAD)
 *   4. Decrypt DH params → g, dh_prime, g_a, server_time
 *   5. set_client_DH     → dh_gen_ok / dh_gen_retry / dh_gen_fail
 *   6. Compute auth_key   = pow(g_a, b) mod dh_prime
 *   7. Compute server_salt = new_nonce[0:8] XOR server_nonce[0:8]
 */

#ifndef MTPROTO_AUTH_H
#define MTPROTO_AUTH_H

#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief DH key exchange context — shared state between steps.
 *
 * Exported for testing. Not part of the public API.
 */
typedef struct {
    Transport      *transport;
    MtProtoSession *session;
    uint8_t         nonce[16];
    uint8_t         server_nonce[16];
    uint8_t         new_nonce[32];
    uint64_t        pq;
    uint32_t        p;
    uint32_t        q;
    int32_t         dc_id;
    int32_t         g;
    uint8_t         dh_prime[256];
    size_t          dh_prime_len;
    uint8_t         g_a[256];
    size_t          g_a_len;
    int32_t         server_time;
    uint8_t         b[256];
    uint8_t         tmp_aes_key[32];
    uint8_t         tmp_aes_iv[32];
} AuthKeyCtx;

/**
 * @brief Generate a new auth key via DH key exchange.
 *
 * Sends unencrypted messages via transport to Telegram DC.
 * On success, sets auth_key and server_salt in the session.
 *
 * @param t Transport (must be connected).
 * @param s Session (auth_key and salt will be set).
 * @return 0 on success, -1 on error.
 */
int mtproto_auth_key_gen(Transport *t, MtProtoSession *s);

/**
 * @brief Factorize a 64-bit pq into two primes p < q.
 *
 * Uses Pollard's rho algorithm. Deterministic for valid inputs.
 * Exported for testing.
 *
 * @param pq  Product of two primes (max 2^63).
 * @param p   Output: smaller prime.
 * @param q   Output: larger prime.
 * @return 0 on success, -1 if factorization fails.
 */
int pq_factorize(uint64_t pq, uint32_t *p, uint32_t *q);

/* ---- Step functions (exported for testing) ---- */

/**
 * @brief Step 1: Send req_pq_multi, receive and parse ResPQ.
 *
 * Generates random nonce, sends req_pq_multi via unencrypted RPC,
 * receives ResPQ and extracts server_nonce, pq, and verifies fingerprint.
 *
 * @param ctx DH context (nonce, server_nonce, pq will be set).
 * @return 0 on success, -1 on error.
 */
int auth_step_req_pq(AuthKeyCtx *ctx);

/**
 * @brief Step 2: Factorize PQ, RSA_PAD encrypt, send req_DH_params.
 *
 * Factorizes pq into p*q, builds p_q_inner_data_dc, encrypts with RSA_PAD,
 * and sends req_DH_params via unencrypted RPC.
 *
 * @param ctx DH context (p, q, new_nonce will be set).
 * @return 0 on success, -1 on error.
 */
int auth_step_req_dh(AuthKeyCtx *ctx);

/**
 * @brief Step 3: Receive server_DH_params_ok, decrypt, parse.
 *
 * Receives server_DH_params_ok, derives temp AES key/IV, decrypts the
 * encrypted answer, and extracts g, dh_prime, g_a, server_time.
 *
 * @param ctx DH context (g, dh_prime, g_a, server_time, tmp_aes_key/iv set).
 * @return 0 on success, -1 on error.
 */
int auth_step_parse_dh(AuthKeyCtx *ctx);

/**
 * @brief Step 4: Compute g_b, send client DH, receive dh_gen_ok.
 *
 * Generates random b, computes g_b = pow(g, b) mod dh_prime, sends
 * set_client_DH_params, receives dh_gen_ok, computes auth_key and salt,
 * and sets them on the session.
 *
 * @param ctx DH context (session auth_key and salt will be set on success).
 * @return 0 on success, -1 on error.
 */
int auth_step_set_client_dh(AuthKeyCtx *ctx);

#endif /* MTPROTO_AUTH_H */
