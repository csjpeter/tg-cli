/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file auth_session.h
 * @brief Telegram login flow: auth.sendCode + auth.signIn.
 *
 * Implements the phone-number / SMS-code login sequence:
 *   1. auth_send_code()  — sends phone number, receives phone_code_hash
 *   2. auth_sign_in()    — sends received code, receives User on success
 */

#ifndef AUTH_SESSION_H
#define AUTH_SESSION_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"

#include <stdint.h>

/* ---- auth.sendCode TL constructor IDs ---- */
#define CRC_auth_sendCode          0xa677244f
#define CRC_codeSettings           0xad253d78
#define CRC_auth_sentCode          0x5e002502
/* auth.SentCodeType variants */
#define CRC_auth_sentCodeTypeApp       0x3dbb5986
#define CRC_auth_sentCodeTypeSms       0xc000bba2
#define CRC_auth_sentCodeTypeCall      0x5353e5a7
#define CRC_auth_sentCodeTypeFlashCall 0xab03c6d9

/* ---- auth.signIn TL constructor IDs ---- */
#define CRC_auth_signIn            0x8d52a951
#define CRC_auth_authorization     0x2ea2c0d4

/** Maximum length for phone_code_hash string. */
#define AUTH_CODE_HASH_MAX 128

/**
 * @brief Result of auth.sendCode — carries the phone_code_hash for signIn.
 */
typedef struct {
    char phone_code_hash[AUTH_CODE_HASH_MAX]; /**< Required for auth.signIn. */
    int  timeout;                             /**< Code expiry seconds, or 0. */
} AuthSentCode;

/**
 * @brief Send auth.sendCode to request an SMS or app notification.
 *
 * Wraps the call in invokeWithLayer + initConnection via api_call().
 *
 * @param cfg    API configuration (api_id, api_hash).
 * @param s      MTProto session (must have auth_key).
 * @param t      Connected transport.
 * @param phone  Phone number in international format (e.g. "+15551234567").
 * @param out    Receives phone_code_hash and timeout on success.
 * @param err    Optional; populated with RPC error info on failure (NULL ok).
 * @return 0 on success, -1 on error.
 */
int auth_send_code(const ApiConfig *cfg,
                   MtProtoSession *s, Transport *t,
                   const char *phone,
                   AuthSentCode *out,
                   RpcError *err);

/**
 * @brief Send auth.signIn with the code received from auth.sendCode.
 *
 * @param cfg              API configuration.
 * @param s                MTProto session.
 * @param t                Connected transport.
 * @param phone            Phone number (same as passed to auth_send_code).
 * @param phone_code_hash  Hash received in AuthSentCode.
 * @param code             Code entered by user.
 * @param user_id_out      Receives the authenticated user's ID on success.
 * @param err              Optional RPC error output (NULL ok).
 * @return 0 on success, -1 on error (wrong code, flood wait, etc.).
 */
int auth_sign_in(const ApiConfig *cfg,
                 MtProtoSession *s, Transport *t,
                 const char *phone,
                 const char *phone_code_hash,
                 const char *code,
                 int64_t *user_id_out,
                 RpcError *err);

#endif /* AUTH_SESSION_H */
