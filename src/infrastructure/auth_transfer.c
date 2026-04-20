/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file auth_transfer.c
 * @brief P10-04 — auth.exportAuthorization + auth.importAuthorization.
 */

#include "auth_transfer.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

/* TL constructor IDs (not yet in tl_registry). */
#define CRC_auth_exportAuthorization   0xe5bfffcdu
#define CRC_auth_exportedAuthorization 0xb434e2b8u
#define CRC_auth_importAuthorization   0xa57a7dadu
#define CRC_auth_authorization         0x2ea2c0d4u
#define CRC_auth_authorizationSignUpRequired 0x44747e9au

#define RESP_BUF_SIZE 65536

/* ---- export ---- */

int auth_transfer_export(const ApiConfig *cfg,
                          MtProtoSession *home_s, Transport *home_t,
                          int target_dc_id,
                          AuthExported *out,
                          RpcError *err) {
    if (!cfg || !home_s || !home_t || !out) return -1;
    if (err) { err->error_code = 0; err->error_msg[0] = '\0';
               err->migrate_dc = -1; err->flood_wait_secs = 0; }
    memset(out, 0, sizeof(*out));

    uint8_t query[64];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_exportAuthorization);
    tl_write_int32 (&w, target_dc_id);
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(RESP_BUF_SIZE);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, home_s, home_t, query, qlen,
                 resp, RESP_BUF_SIZE, &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_transfer_export: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError perr; rpc_parse_error(resp, resp_len, &perr);
        if (err) *err = perr;
        logger_log(LOG_ERROR, "auth_transfer_export: RPC %d: %s",
                   perr.error_code, perr.error_msg);
        return -1;
    }
    if (top != CRC_auth_exportedAuthorization) {
        logger_log(LOG_ERROR,
                   "auth_transfer_export: unexpected top 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r);                           /* constructor */
    out->id = tl_read_int64(&r);

    size_t bl = 0;
    RAII_STRING uint8_t *bytes = tl_read_bytes(&r, &bl);
    if (!bytes || bl == 0 || bl > AUTH_TRANSFER_BYTES_MAX) {
        logger_log(LOG_ERROR,
                   "auth_transfer_export: bytes len %zu out of range", bl);
        return -1;
    }
    memcpy(out->bytes, bytes, bl);
    out->bytes_len = bl;
    logger_log(LOG_INFO,
               "auth_transfer_export: exported to DC%d (id=%lld, %zu bytes)",
               target_dc_id, (long long)out->id, bl);
    return 0;
}

/* ---- import ---- */

int auth_transfer_import(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const AuthExported *in,
                          RpcError *err) {
    if (!cfg || !s || !t || !in) return -1;
    if (in->bytes_len == 0 || in->bytes_len > AUTH_TRANSFER_BYTES_MAX)
        return -1;
    if (err) { err->error_code = 0; err->error_msg[0] = '\0';
               err->migrate_dc = -1; err->flood_wait_secs = 0; }

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_importAuthorization);
    tl_write_int64 (&w, in->id);
    tl_write_bytes (&w, in->bytes, in->bytes_len);

    RAII_STRING uint8_t *query = (uint8_t *)malloc(w.len);
    if (!query) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(RESP_BUF_SIZE);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen,
                 resp, RESP_BUF_SIZE, &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_transfer_import: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError perr; rpc_parse_error(resp, resp_len, &perr);
        if (err) *err = perr;
        logger_log(LOG_ERROR, "auth_transfer_import: RPC %d: %s",
                   perr.error_code, perr.error_msg);
        return -1;
    }
    if (top != CRC_auth_authorization
        && top != CRC_auth_authorizationSignUpRequired) {
        logger_log(LOG_ERROR,
                   "auth_transfer_import: unexpected top 0x%08x", top);
        return -1;
    }
    logger_log(LOG_INFO, "auth_transfer_import: foreign DC now authorized");
    return 0;
}
