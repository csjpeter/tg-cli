/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/auth_flow.h
 * @brief High-level login flow orchestration.
 *
 * Wraps transport connect + DH auth key generation + auth.sendCode +
 * auth.signIn into one call. Handles:
 *   - DC migration (PHONE_MIGRATE_X, USER_MIGRATE_X, NETWORK_MIGRATE_X)
 *   - persistent auth key storage (TODO)
 *   - 2FA password challenge (TODO — P3-03)
 */

#ifndef APP_AUTH_FLOW_H
#define APP_AUTH_FLOW_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

typedef struct AuthFlowCallbacks AuthFlowCallbacks;

/**
 * @brief Callbacks that supply user input during the interactive login.
 *
 * Implementations may read from a terminal (tg-tui), from command-line
 * flags (e.g. tg-cli-ro --phone/--code), or from a test double.
 */
struct AuthFlowCallbacks {
    /** @brief Provide the phone number (international format, e.g. "+15551234567"). */
    int (*get_phone)(void *user, char *out, size_t out_cap);
    /** @brief Provide the SMS/app code after it arrives on the phone. */
    int (*get_code)(void *user, char *out, size_t out_cap);
    /** @brief Provide the 2FA password when the server asks for it (optional). */
    int (*get_password)(void *user, char *out, size_t out_cap);
    void *user;
};

/** @brief Result of a completed login. */
typedef struct {
    int     dc_id;           /**< Final DC the session is pinned to.      */
    int64_t user_id;         /**< Authenticated user id, 0 if unknown.   */
    int     needs_password;  /**< Non-zero if 2FA is required (TODO).    */
} AuthFlowResult;

/**
 * @brief Perform full login on a fresh transport+session.
 *
 * Connects to the default DC, runs DH auth key generation, then drives the
 * phone/code login. Retries after migration if the server redirects.
 *
 * The caller owns @p t and @p s and must close @p t when done.
 *
 * @param cfg ApiConfig (api_id/api_hash pre-populated).
 * @param cb  User-input callbacks.
 * @param t   Transport — must be initialised (not connected).
 * @param s   Session — must be initialised.
 * @param out Optional result summary (may be NULL).
 * @return 0 on success, -1 on unrecoverable error.
 */
int auth_flow_login(const ApiConfig *cfg,
                    const AuthFlowCallbacks *cb,
                    Transport *t, MtProtoSession *s,
                    AuthFlowResult *out);

/**
 * @brief Connect to a specific DC and bring up a fresh auth key.
 *
 * Shared helper used by auth_flow_login and by migration retries.
 *
 * @param dc_id Target DC id (1..5).
 * @param t     Transport to connect.
 * @param s     Session to initialise.
 * @return 0 on success, -1 on error.
 */
int auth_flow_connect_dc(int dc_id, Transport *t, MtProtoSession *s);

#endif /* APP_AUTH_FLOW_H */
