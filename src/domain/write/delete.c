/**
 * @file domain/write/delete.c
 * @brief messages.deleteMessages + channels.deleteMessages (US-P5-06).
 */

#include "domain/write/delete.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"

#include <string.h>

#define CRC_messages_deleteMessages 0xe58e95d2u
#define CRC_channels_deleteMessages 0x84c1fd4eu
#define CRC_inputChannel            0xf35aec28u

int domain_delete_messages(const ApiConfig *cfg,
                            MtProtoSession *s, Transport *t,
                            const HistoryPeer *peer,
                            const int32_t *ids, int n_ids,
                            int revoke,
                            RpcError *err) {
    if (!cfg || !s || !t || !peer || !ids || n_ids <= 0 || n_ids > 100)
        return -1;

    TlWriter w; tl_writer_init(&w);
    int is_channel = (peer->kind == HISTORY_PEER_CHANNEL);

    if (is_channel) {
        tl_write_uint32(&w, CRC_channels_deleteMessages);
        tl_write_uint32(&w, CRC_inputChannel);
        tl_write_int64 (&w, peer->peer_id);
        tl_write_int64 (&w, peer->access_hash);
    } else {
        tl_write_uint32(&w, CRC_messages_deleteMessages);
        tl_write_uint32(&w, revoke ? 1u : 0u);    /* flags */
    }
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, (uint32_t)n_ids);
    for (int i = 0; i < n_ids; i++) tl_write_int32(&w, ids[i]);

    uint8_t query[1024];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[1024]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "delete: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (top == TL_messages_affectedMessages) return 0;
    logger_log(LOG_WARN, "delete: unexpected top 0x%08x", top);
    return 0;
}
