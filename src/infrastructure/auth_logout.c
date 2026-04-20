/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file infrastructure/auth_logout.c
 * @brief Server-side logout: auth.logOut#3e72ba19 + local session wipe.
 */

#include "auth_logout.h"
#include "app/session_store.h"
#include "mtproto_rpc.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

/* ---- Logout observer (Observer pattern keeps domain/ out of here) ---- */

static void (*s_on_logout_cb)(void) = NULL;

void auth_logout_set_cache_flush_cb(void (*cb)(void)) {
    s_on_logout_cb = cb;
}

/* ---- Build the auth.logOut TL request ---- */

static int build_logout_request(uint8_t *out, size_t max_len, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    /* auth.logOut#3e72ba19  — no arguments */
    tl_write_uint32(&w, CRC_auth_logOut);

    if (w.len > max_len) {
        tl_writer_free(&w);
        return -1;
    }
    memcpy(out, w.data, w.len);
    *out_len = w.len;
    tl_writer_free(&w);
    return 0;
}

/* ---- auth_logout_rpc ---- */

int auth_logout_rpc(const ApiConfig *cfg, MtProtoSession *s, Transport *t) {
    if (!cfg || !s || !t) return -1;

    uint8_t query[64];
    size_t  qlen = 0;
    if (build_logout_request(query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "auth_logout: failed to build request");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(4096);
    if (!resp) return -1;
    size_t resp_len = 0;

    if (api_call(cfg, s, t, query, qlen, resp, 4096, &resp_len) != 0) {
        logger_log(LOG_WARN, "auth_logout: api_call failed");
        return -1;
    }

    if (resp_len < 4) {
        logger_log(LOG_WARN, "auth_logout: response too short (%zu bytes)", resp_len);
        return -1;
    }

    uint32_t constructor;
    memcpy(&constructor, resp, 4);

    if (constructor == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_WARN, "auth_logout: RPC error %d: %s",
                   err.error_code, err.error_msg);
        /* AUTH_KEY_UNREGISTERED / NOT_AUTHORIZED mean the session is already
         * dead on the server — still counts as a successful logout. */
        if (err.error_code == 401) {
            logger_log(LOG_INFO, "auth_logout: session already invalid on server");
            return 0;
        }
        return -1;
    }

    if (constructor != CRC_auth_loggedOut) {
        logger_log(LOG_WARN, "auth_logout: unexpected constructor 0x%08x", constructor);
        return -1;
    }

    /* auth.loggedOut#c3a2835f flags:# future_auth_token:flags.0?bytes
     * We intentionally ignore future_auth_token. */
    logger_log(LOG_INFO, "auth_logout: server confirmed logout");
    return 0;
}

/* ---- auth_logout ---- */

void auth_logout(const ApiConfig *cfg, MtProtoSession *s, Transport *t) {
    if (auth_logout_rpc(cfg, s, t) != 0) {
        logger_log(LOG_WARN,
                   "auth_logout: server invalidation failed; clearing local session anyway");
    }
    session_store_clear();
    /* Drop session-scoped caches. Infrastructure cannot depend on the
     * domain layer directly, so we do this through a registered callback
     * set by the calling binary. */
    if (s_on_logout_cb) s_on_logout_cb();
}
