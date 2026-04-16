/**
 * @file domain/write/read_history.c
 * @brief messages.readHistory + channels.readHistory (US-P5-04).
 */

#include "domain/write/read_history.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"

#include <string.h>

#define CRC_messages_readHistory  0x0e306d3au
#define CRC_channels_readHistory  0xcc104937u
#define CRC_messages_affectedMessages TL_messages_affectedMessages
#define CRC_inputChannel          0xf35aec28u

static int build_messages_request(const HistoryPeer *peer, int32_t max_id,
                                   uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_readHistory);
    switch (peer->kind) {
    case HISTORY_PEER_SELF:
        tl_write_uint32(&w, TL_inputPeerSelf);
        break;
    case HISTORY_PEER_USER:
        tl_write_uint32(&w, TL_inputPeerUser);
        tl_write_int64 (&w, peer->peer_id);
        tl_write_int64 (&w, peer->access_hash);
        break;
    case HISTORY_PEER_CHAT:
        tl_write_uint32(&w, TL_inputPeerChat);
        tl_write_int64 (&w, peer->peer_id);
        break;
    default:
        tl_writer_free(&w);
        return -1;
    }
    tl_write_int32(&w, max_id);

    int rc = -1;
    if (w.len <= cap) { memcpy(buf, w.data, w.len); *out_len = w.len; rc = 0; }
    tl_writer_free(&w);
    return rc;
}

static int build_channels_request(const HistoryPeer *peer, int32_t max_id,
                                   uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_channels_readHistory);
    tl_write_uint32(&w, CRC_inputChannel);
    tl_write_int64 (&w, peer->peer_id);
    tl_write_int64 (&w, peer->access_hash);
    tl_write_int32 (&w, max_id);

    int rc = -1;
    if (w.len <= cap) { memcpy(buf, w.data, w.len); *out_len = w.len; rc = 0; }
    tl_writer_free(&w);
    return rc;
}

int domain_mark_read(const ApiConfig *cfg,
                      MtProtoSession *s, Transport *t,
                      const HistoryPeer *peer,
                      int32_t max_id,
                      RpcError *err) {
    if (!cfg || !s || !t || !peer) return -1;

    uint8_t query[64]; size_t qlen = 0;
    int is_channel = (peer->kind == HISTORY_PEER_CHANNEL);

    int bc = is_channel
           ? build_channels_request(peer, max_id, query, sizeof(query), &qlen)
           : build_messages_request(peer, max_id, query, sizeof(query), &qlen);
    if (bc != 0) return -1;

    uint8_t resp[512]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "read_history: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (is_channel) {
        if (top == TL_boolTrue) return 0;
        if (top == TL_boolFalse) {
            logger_log(LOG_WARN, "read_history: channel returned boolFalse");
            return -1;
        }
    } else if (top == CRC_messages_affectedMessages) {
        return 0;
    }
    logger_log(LOG_WARN, "read_history: unexpected top 0x%08x", top);
    return 0;
}
