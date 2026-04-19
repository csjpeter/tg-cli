/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file auth_2fa.h
 * @brief 2FA login completion: account.getPassword + auth.checkPassword (SRP).
 *
 * Invoked when auth.signIn returns SESSION_PASSWORD_NEEDED. The server's
 * account.password response carries the SRP parameters; we respond with
 * inputCheckPasswordSRP carrying a client-computed A + M1 proof.
 *
 * Spec: https://core.telegram.org/api/srp
 */

#ifndef AUTH_2FA_H
#define AUTH_2FA_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"

#include <stdint.h>
#include <stddef.h>

/** Telegram SRP uses a fixed 2048-bit prime. */
#define SRP_PRIME_LEN 256
/** Max expected salt length from account.password. */
#define SRP_SALT_MAX  128

/** @brief Parsed account.password we need for SRP. */
typedef struct {
    int     has_password;              /**< Non-zero if 2FA is currently set. */
    int64_t srp_id;                    /**< Opaque identifier passed back.    */
    unsigned char srp_B[SRP_PRIME_LEN];/**< Server ephemeral, left-padded.    */
    int32_t g;                         /**< SRP generator (usually 2 or 3).   */
    unsigned char p[SRP_PRIME_LEN];    /**< 2048-bit prime.                   */
    unsigned char salt1[SRP_SALT_MAX];
    size_t  salt1_len;
    unsigned char salt2[SRP_SALT_MAX];
    size_t  salt2_len;
} Account2faPassword;

/**
 * @brief Fetch account.password — the server-side SRP parameters.
 *
 * @param cfg  API config.
 * @param s    Session.
 * @param t    Connected transport.
 * @param out  Receives SRP params; .has_password == 0 means no 2FA.
 * @param err  Optional RPC error.
 * @return 0 on success, -1 on error.
 */
int auth_2fa_get_password(const ApiConfig *cfg,
                           MtProtoSession *s, Transport *t,
                           Account2faPassword *out, RpcError *err);

/**
 * @brief Complete 2FA login by submitting an SRP proof.
 *
 * Computes A + M1 from the password and @p params, then calls
 * auth.checkPassword. On success the server returns auth.authorization
 * and the session is fully logged in.
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Connected transport.
 * @param params    Parameters previously fetched by auth_2fa_get_password.
 * @param password  User's plaintext 2FA password (UTF-8).
 * @param user_id_out  Receives user id on success (optional).
 * @param err       Optional RPC error.
 * @return 0 on success, -1 on error (bad password surfaces as
 *         PASSWORD_HASH_INVALID via @p err).
 */
int auth_2fa_check_password(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const Account2faPassword *params,
                             const char *password,
                             int64_t *user_id_out, RpcError *err);

/**
 * @brief Build the SRP proof (A + M1) from @p params + @p password.
 *
 * Normally the caller lets auth_2fa_check_password generate the
 * client's private exponent via crypto_rand_bytes. Functional tests
 * (and SRP known-answer checks) can pin @p a_priv_in to a known
 * 256-byte value so the computation becomes deterministic.
 *
 * @param params      SRP parameters from auth_2fa_get_password.
 * @param password    Plaintext 2FA password.
 * @param a_priv_in   Optional 256-byte client private exponent.
 *                    NULL = generate fresh via crypto_rand_bytes.
 * @param A_out       Receives 256-byte A = g^a mod p.
 * @param M1_out      Receives 32-byte M1 proof.
 * @return 0 on success, -1 on error.
 */
int auth_2fa_srp_compute(const Account2faPassword *params,
                          const char *password,
                          const unsigned char *a_priv_in,
                          unsigned char A_out[SRP_PRIME_LEN],
                          unsigned char M1_out[32]);

#endif /* AUTH_2FA_H */
