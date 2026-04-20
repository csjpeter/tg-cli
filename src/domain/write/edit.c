/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/edit.c
 * @brief messages.editMessage (US-P5-06).
 */

#include "domain/write/edit.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"

#include <string.h>

#define CRC_messages_editMessage 0x48f71778u

static int write_input_peer(TlWriter *w, const HistoryPeer *p) {
    switch (p->kind) {
    case HISTORY_PEER_SELF:
        tl_write_uint32(w, TL_inputPeerSelf);
        return 0;
    case HISTORY_PEER_USER:
        tl_write_uint32(w, TL_inputPeerUser);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash);
        return 0;
    case HISTORY_PEER_CHAT:
        tl_write_uint32(w, TL_inputPeerChat);
        tl_write_int64 (w, p->peer_id);
        return 0;
    case HISTORY_PEER_CHANNEL:
        tl_write_uint32(w, TL_inputPeerChannel);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash);
        return 0;
    default:
        return -1;
    }
}

int domain_edit_message(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         const HistoryPeer *peer,
                         int32_t msg_id,
                         const char *new_text,
                         RpcError *err) {
    if (!cfg || !s || !t || !peer || !new_text) return -1;
    size_t mlen = strlen(new_text);
    if (mlen == 0 || mlen > 4096) {
        logger_log(LOG_ERROR, "edit: text length %zu out of bounds", mlen);
        return -1;
    }
    if (msg_id <= 0) return -1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_editMessage);
    tl_write_uint32(&w, 1u << 11);             /* flags: only message set */
    if (write_input_peer(&w, peer) != 0) {
        tl_writer_free(&w);
        return -1;
    }
    tl_write_int32 (&w, msg_id);
    tl_write_string(&w, new_text);

    uint8_t query[8192];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[4096]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "edit: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    /* Any Updates envelope = success. */
    if (top == TL_updates || top == TL_updatesCombined
        || top == TL_updateShort) {
        return 0;
    }
    logger_log(LOG_WARN, "edit: unexpected top 0x%08x — assuming success", top);
    return 0;
}
