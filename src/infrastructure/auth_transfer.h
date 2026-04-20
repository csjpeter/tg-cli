/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file auth_transfer.h
 * @brief auth.exportAuthorization / auth.importAuthorization — cross-DC
 *        authorization transfer (P10-04).
 *
 * To drive an authorized RPC on a non-home DC, the client:
 *   1. Calls auth.exportAuthorization(dc_id) on the home DC.
 *      Response: auth.exportedAuthorization { id:long, bytes:bytes }.
 *   2. Calls auth.importAuthorization(id, bytes) on the secondary DC
 *      (after DH handshake). Response: auth.authorization { user:User }.
 *
 * The exported token is short-lived (seconds) so the pair must be issued
 * back-to-back. Once imported, the secondary DC session is authorized for
 * the same user as the home DC, and remains so as long as the auth_key is
 * valid — no re-import is needed for later runs that reuse the cached key.
 */

#ifndef INFRASTRUCTURE_AUTH_TRANSFER_H
#define INFRASTRUCTURE_AUTH_TRANSFER_H

#include "api_call.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

/** @brief Maximum size of the opaque auth-transfer `bytes` token. */
#define AUTH_TRANSFER_BYTES_MAX 1024

/** @brief Result of auth.exportAuthorization. */
typedef struct {
    int64_t id;
    uint8_t bytes[AUTH_TRANSFER_BYTES_MAX];
    size_t  bytes_len;
} AuthExported;

/**
 * @brief Call auth.exportAuthorization(dc_id) on the home DC.
 *
 * @param cfg          Api config.
 * @param home_s       Session on the home DC (must be authorized).
 * @param home_t       Transport on the home DC.
 * @param target_dc_id DC the caller is about to import into.
 * @param out          Receives (id, bytes) on success.
 * @param err          Optional RPC error detail on failure.
 * @return 0 on success, -1 on RPC error or malformed response.
 */
int auth_transfer_export(const ApiConfig *cfg,
                          MtProtoSession *home_s, Transport *home_t,
                          int target_dc_id,
                          AuthExported *out,
                          RpcError *err);

/**
 * @brief Call auth.importAuthorization(id, bytes) on the foreign DC.
 *
 * @param cfg    Api config.
 * @param s      Foreign-DC session (fresh DH handshake done).
 * @param t      Foreign-DC transport.
 * @param in     Token issued by auth_transfer_export().
 * @param err    Optional RPC error detail on failure.
 * @return 0 on success (auth.authorization response), -1 on error.
 */
int auth_transfer_import(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const AuthExported *in,
                          RpcError *err);

#endif /* INFRASTRUCTURE_AUTH_TRANSFER_H */
