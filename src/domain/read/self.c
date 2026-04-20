/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/self.c
 * @brief Implementation of users.getUsers([inputUserSelf]).
 */

#include "domain/read/self.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

/* Method and input-type CRCs (stable across recent layers). */
#define CRC_users_getUsers   0x0d91a548u
#define CRC_inputUserSelf    0xf7c1b13fu

/* Flag bits on user#... (layer 170+). The exact bit positions are part of
 * the schema; we read only the flags word to skip optional fields we don't
 * need. The three we care about are known by bit position:
 *   flags.0  = has_access_hash
 *   flags.1  = has_first_name
 *   flags.2  = has_last_name
 *   flags.3  = has_username
 *   flags.4  = has_phone
 * Premium flag lives in flags2 (introduced in layer 144+): flags2.3.
 * We fall back to safe parsing and only fill what we find.
 */

static int build_request(uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_users_getUsers);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                 /* count */
    tl_write_uint32(&w, CRC_inputUserSelf); /* element */

    int rc = -1;
    if (w.len <= cap) {
        memcpy(buf, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

static void copy_str(char *dst, size_t dst_cap, const char *src) {
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t n = strlen(src);
    if (n >= dst_cap) n = dst_cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Parse a single User (or Empty/Full). On success fills @out and returns 0. */
static int parse_user(TlReader *r, SelfInfo *out) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_userEmpty) {
        out->id = tl_read_int64(r);
        return 0;
    }
    if (crc != TL_user && crc != TL_userFull) {
        logger_log(LOG_WARN,
                   "domain_get_self: unexpected user constructor 0x%08x", crc);
        return -1;
    }

    uint32_t flags  = tl_read_uint32(r);
    uint32_t flags2 = tl_read_uint32(r); /* layer 144+: second flags word */
    out->id = tl_read_int64(r);

    if (flags & (1u << 0)) tl_read_int64(r); /* access_hash */

    if (flags & (1u << 1)) {
        RAII_STRING char *fn = tl_read_string(r);
        copy_str(out->first_name, sizeof(out->first_name), fn);
    }
    if (flags & (1u << 2)) {
        RAII_STRING char *ln = tl_read_string(r);
        copy_str(out->last_name, sizeof(out->last_name), ln);
    }
    if (flags & (1u << 3)) {
        RAII_STRING char *un = tl_read_string(r);
        copy_str(out->username, sizeof(out->username), un);
    }
    if (flags & (1u << 4)) {
        RAII_STRING char *ph = tl_read_string(r);
        copy_str(out->phone, sizeof(out->phone), ph);
    }

    /* Best-effort premium/bot detection from well-known flag bits:
     *   flags.14 = bot (layer 144+)
     *   flags2.3 = premium (layer 144+)
     * Trailing fields we do not need are left unread; Telegram tolerates a
     * short read on the client side for the purposes of this query because
     * the wire contains only the object we asked for.                      */
    out->is_bot     = (flags & (1u << 14)) ? 1 : 0;
    out->is_premium = (flags2 & (1u << 3)) ? 1 : 0;
    return 0;
}

int domain_get_self(const ApiConfig *cfg,
                    MtProtoSession *s, Transport *t,
                    SelfInfo *out) {
    if (!cfg || !s || !t || !out) return -1;
    memset(out, 0, sizeof(*out));

    uint8_t query[64];
    size_t qlen = 0;
    if (build_request(query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "domain_get_self: failed to build request");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) {
        logger_log(LOG_ERROR, "domain_get_self: api_call failed");
        return -1;
    }
    if (resp_len < 8) {
        logger_log(LOG_ERROR, "domain_get_self: response too short");
        return -1;
    }

    /* RPC error? */
    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "domain_get_self: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    /* Expected: Vector<User> → [vector_crc, count, user0, ...] */
    if (top != TL_vector) {
        logger_log(LOG_ERROR,
                   "domain_get_self: expected Vector, got 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* vector */
    uint32_t count = tl_read_uint32(&r);
    if (count == 0) {
        logger_log(LOG_ERROR, "domain_get_self: empty Vector<User>");
        return -1;
    }

    return parse_user(&r, out);
}
