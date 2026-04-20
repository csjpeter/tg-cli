/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file api_call.h
 * @brief Telegram API call wrapper — initConnection + invokeWithLayer.
 *
 * Every Telegram API method must be wrapped in:
 *   invokeWithLayer(layer, initConnection(api_id, ..., query))
 *
 * This module handles the wrapping and provides a simple interface
 * for making API calls over an authenticated MTProto session.
 */

#ifndef API_CALL_H
#define API_CALL_H

#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

/** Current TL schema layer version. */
#define TL_LAYER 185

/* TL constructors for API wrapping */
#define CRC_invokeWithLayer 0xda9b0d0d
#define CRC_initConnection  0xc1cd5ea9

/**
 * @brief API connection parameters.
 */
typedef struct {
    int32_t     api_id;
    const char *api_hash;
    const char *device_model;
    const char *system_version;
    const char *app_version;
    const char *system_lang_code;
    const char *lang_pack;
    const char *lang_code;
} ApiConfig;

/**
 * @brief Initialize ApiConfig with defaults.
 *
 * Sets device_model, system_version, etc. to sensible defaults.
 * api_id and api_hash must be set by the caller.
 */
void api_config_init(ApiConfig *cfg);

/**
 * @brief Wrap a TL query in invokeWithLayer + initConnection.
 *
 * Writes the wrapped query to a TlWriter. The caller is responsible
 * for freeing the writer.
 *
 * @param cfg   API connection parameters.
 * @param query Inner TL query bytes.
 * @param qlen  Query length.
 * @param out   Output buffer.
 * @param max_len Output buffer capacity.
 * @param out_len Receives wrapped query length.
 * @return 0 on success, -1 on error.
 */
int api_wrap_query(const ApiConfig *cfg,
                   const uint8_t *query, size_t qlen,
                   uint8_t *out, size_t max_len, size_t *out_len);

/**
 * @brief Send an API call (wrapped in initConnection) and receive response.
 *
 * Wraps the query, sends it encrypted via RPC, receives the response,
 * and unwraps gzip_packed if present.
 *
 * @param cfg     API config.
 * @param s       Session (must have auth_key).
 * @param t       Transport (must be connected).
 * @param query   Inner TL query.
 * @param qlen    Query length.
 * @param resp    Output buffer for response.
 * @param max_len Response buffer capacity.
 * @param resp_len Receives response length.
 * @return 0 on success, -1 on error.
 */
int api_call(const ApiConfig *cfg,
             MtProtoSession *s, Transport *t,
             const uint8_t *query, size_t qlen,
             uint8_t *resp, size_t max_len, size_t *resp_len);

#endif /* API_CALL_H */
