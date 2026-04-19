/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/forward.c
 * @brief messages.forwardMessages (US-P5-06).
 */

#include "domain/write/forward.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "crypto.h"
#include "logger.h"

#include <string.h>

#define CRC_messages_forwardMessages 0xc661bbc4u

static int write_input_peer(TlWriter *w, const HistoryPeer *p) {
    switch (p->kind) {
    case HISTORY_PEER_SELF:
        tl_write_uint32(w, TL_inputPeerSelf);    return 0;
    case HISTORY_PEER_USER:
        tl_write_uint32(w, TL_inputPeerUser);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash);      return 0;
    case HISTORY_PEER_CHAT:
        tl_write_uint32(w, TL_inputPeerChat);
        tl_write_int64 (w, p->peer_id);          return 0;
    case HISTORY_PEER_CHANNEL:
        tl_write_uint32(w, TL_inputPeerChannel);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash);      return 0;
    default: return -1;
    }
}

int domain_forward_messages(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const HistoryPeer *from,
                             const HistoryPeer *to,
                             const int32_t *ids, int n_ids,
                             RpcError *err) {
    if (!cfg || !s || !t || !from || !to || !ids || n_ids <= 0 || n_ids > 100)
        return -1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_forwardMessages);
    tl_write_uint32(&w, 0);                        /* flags = 0 */
    if (write_input_peer(&w, from) != 0) { tl_writer_free(&w); return -1; }
    tl_write_uint32(&w, TL_vector);                /* id vector */
    tl_write_uint32(&w, (uint32_t)n_ids);
    for (int i = 0; i < n_ids; i++) tl_write_int32(&w, ids[i]);
    tl_write_uint32(&w, TL_vector);                /* random_id vector */
    tl_write_uint32(&w, (uint32_t)n_ids);
    for (int i = 0; i < n_ids; i++) {
        uint8_t rnd[8];
        int64_t rid = 0;
        if (crypto_rand_bytes(rnd, 8) == 0) memcpy(&rid, rnd, 8);
        tl_write_int64(&w, rid);
    }
    if (write_input_peer(&w, to) != 0) { tl_writer_free(&w); return -1; }

    uint8_t query[2048];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[4096]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "forward: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (top == TL_updates || top == TL_updatesCombined
        || top == TL_updateShort) {
        return 0;
    }
    logger_log(LOG_WARN, "forward: unexpected top 0x%08x", top);
    return 0;
}
