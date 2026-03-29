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

#endif /* MTPROTO_AUTH_H */
