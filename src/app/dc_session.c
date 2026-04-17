/**
 * @file app/dc_session.c
 * @brief Implementation of per-DC session bring-up.
 */

#include "app/dc_session.h"

#include "app/auth_flow.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "logger.h"

#include <string.h>

int dc_session_open(int dc_id, DcSession *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->dc_id = dc_id;
    transport_init(&out->transport);
    mtproto_session_init(&out->session);

    /* Fast path: reuse a previously persisted auth_key. */
    if (session_store_load_dc(dc_id, &out->session) == 0) {
        const DcEndpoint *ep = dc_lookup(dc_id);
        if (ep && transport_connect(&out->transport, ep->host, ep->port) == 0) {
            out->transport.dc_id = dc_id;
            logger_log(LOG_INFO, "dc_session: reused cached key on DC%d", dc_id);
            return 0;
        }
        logger_log(LOG_WARN,
                   "dc_session: cached DC%d unusable, re-handshaking", dc_id);
        transport_close(&out->transport);
        mtproto_session_init(&out->session);
    }

    /* Slow path: fresh DH handshake. */
    if (auth_flow_connect_dc(dc_id, &out->transport, &out->session) != 0) {
        logger_log(LOG_ERROR, "dc_session: handshake failed on DC%d", dc_id);
        return -1;
    }

    if (session_store_save_dc(dc_id, &out->session) != 0) {
        logger_log(LOG_WARN,
                   "dc_session: could not persist DC%d (non-fatal)", dc_id);
    }
    return 0;
}

void dc_session_close(DcSession *s) {
    if (!s) return;
    transport_close(&s->transport);
}
