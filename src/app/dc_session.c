/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/dc_session.c
 * @brief Implementation of per-DC session bring-up.
 */

#include "app/dc_session.h"

#include "app/auth_flow.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "infrastructure/auth_transfer.h"
#include "logger.h"

#include <string.h>

int dc_session_open(int dc_id, DcSession *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->dc_id = dc_id;
    transport_init(&out->transport);
    mtproto_session_init(&out->session);

    /* Fast path: reuse a previously persisted auth_key. A cached auth_key
     * on the server has whatever authorization state was bound to it at
     * last import, so we treat it as authorized here. A later
     * AUTH_KEY_UNREGISTERED would force a re-handshake. */
    if (session_store_load_dc(dc_id, &out->session) == 0) {
        const DcEndpoint *ep = dc_lookup(dc_id);
        if (ep && transport_connect(&out->transport, ep->host, ep->port) == 0) {
            out->transport.dc_id = dc_id;
            out->authorized      = 1;
            logger_log(LOG_INFO, "dc_session: reused cached key on DC%d", dc_id);
            return 0;
        }
        logger_log(LOG_WARN,
                   "dc_session: cached DC%d unusable, re-handshaking", dc_id);
        transport_close(&out->transport);
        mtproto_session_init(&out->session);
    }

    /* Slow path: fresh DH handshake. The new auth_key has no user binding
     * yet; the caller must run auth.importAuthorization before any
     * authorized RPC on @p out. */
    if (auth_flow_connect_dc(dc_id, &out->transport, &out->session) != 0) {
        logger_log(LOG_ERROR, "dc_session: handshake failed on DC%d", dc_id);
        return -1;
    }
    out->authorized = 0;

    if (session_store_save_dc(dc_id, &out->session) != 0) {
        logger_log(LOG_WARN,
                   "dc_session: could not persist DC%d (non-fatal)", dc_id);
    }
    return 0;
}

int dc_session_ensure_authorized(DcSession *sess,
                                  const ApiConfig *cfg,
                                  MtProtoSession *home_s,
                                  Transport *home_t) {
    if (!sess || !cfg || !home_s || !home_t) return -1;
    if (sess->authorized) return 0;

    AuthExported tok = {0};
    RpcError err = {0};
    if (auth_transfer_export(cfg, home_s, home_t,
                               sess->dc_id, &tok, &err) != 0) {
        logger_log(LOG_ERROR,
                   "dc_session: export for DC%d failed (%d: %s)",
                   sess->dc_id, err.error_code, err.error_msg);
        return -1;
    }
    RpcError ierr = {0};
    if (auth_transfer_import(cfg, &sess->session, &sess->transport,
                               &tok, &ierr) != 0) {
        logger_log(LOG_ERROR,
                   "dc_session: import on DC%d failed (%d: %s)",
                   sess->dc_id, ierr.error_code, ierr.error_msg);
        return -1;
    }
    sess->authorized = 1;

    /* Persist the now-authorized auth_key. The server-side binding is
     * tied to the auth_key itself; on next run we skip the import. */
    (void)session_store_save_dc(sess->dc_id, &sess->session);
    return 0;
}

void dc_session_close(DcSession *s) {
    if (!s) return;
    transport_close(&s->transport);
}
