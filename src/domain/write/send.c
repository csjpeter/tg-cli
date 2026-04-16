/**
 * @file domain/write/send.c
 * @brief messages.sendMessage — first write-capable domain.
 */

#include "domain/write/send.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "crypto.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

#define CRC_messages_sendMessage 0x0d9d75a4u

/* messages.Updates constructors we may see in the response. We only need
 * to recognise them enough to return success + optionally extract the
 * outgoing message id from updateShortSentMessage. */
#define CRC_updateShortSentMessage  0x9015e101u
#define CRC_updates                 TL_updates
#define CRC_updatesCombined         TL_updatesCombined
#define CRC_updateShort             TL_updateShort

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

#define CRC_inputReplyToMessage 0x22c0f6d5u

int domain_send_message_reply(const ApiConfig *cfg,
                               MtProtoSession *s, Transport *t,
                               const HistoryPeer *peer,
                               const char *message,
                               int32_t reply_to_msg_id,
                               int32_t *msg_id_out,
                               RpcError *err) {
    if (!cfg || !s || !t || !peer || !message) return -1;
    size_t mlen = strlen(message);
    if (mlen == 0 || mlen > 4096) {
        logger_log(LOG_ERROR, "send: message length %zu out of bounds", mlen);
        return -1;
    }

    /* random_id — must be uniformly random 64-bit. */
    uint8_t rand_buf[8] = {0};
    int64_t random_id = 0;
    if (crypto_rand_bytes(rand_buf, sizeof(rand_buf)) == 0) {
        memcpy(&random_id, rand_buf, 8);
    } else {
        /* Fall back to non-crypto randomness; not security-critical. */
        random_id = (int64_t)rand() << 32 | (int64_t)rand();
    }

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_sendMessage);
    uint32_t flags = (reply_to_msg_id > 0) ? 1u : 0u; /* flags.0 = reply_to */
    tl_write_uint32(&w, flags);
    if (write_input_peer(&w, peer) != 0) {
        tl_writer_free(&w);
        return -1;
    }
    if (reply_to_msg_id > 0) {
        /* inputReplyToMessage#22c0f6d5 flags:# reply_to_msg_id:int
         *                              top_msg_id:flags.0?int
         *                              reply_to_peer_id:flags.1?InputPeer
         *                              quote_text:flags.2?string ... */
        tl_write_uint32(&w, CRC_inputReplyToMessage);
        tl_write_uint32(&w, 0);                    /* inner flags */
        tl_write_int32 (&w, reply_to_msg_id);
    }
    tl_write_string(&w, message);
    tl_write_int64 (&w, random_id);

    uint8_t query[8192];
    if (w.len > sizeof(query)) {
        tl_writer_free(&w);
        logger_log(LOG_ERROR, "send: query too large (%zu)", w.len);
        return -1;
    }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[8192]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "send: api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }

    if (msg_id_out) *msg_id_out = 0;

    /* updateShortSentMessage#9015e101 flags:# out:flags.1?true id:int
     *   pts:int pts_count:int date:int media:flags.9?MessageMedia
     *   entities:flags.7?Vector<MessageEntity> ttl_period:flags.25?int */
    if (top == CRC_updateShortSentMessage) {
        TlReader r = tl_reader_init(resp, resp_len);
        tl_read_uint32(&r);                 /* crc */
        tl_read_uint32(&r);                 /* flags */
        if (tl_reader_ok(&r) && r.len - r.pos >= 4) {
            int32_t id = tl_read_int32(&r);
            if (msg_id_out) *msg_id_out = id;
        }
        return 0;
    }

    /* For updates / updatesCombined / updateShort we just accept success.
     * Message-id extraction from the full Updates envelope is future work. */
    if (top == CRC_updates || top == CRC_updatesCombined
        || top == CRC_updateShort) {
        return 0;
    }

    logger_log(LOG_WARN, "send: unexpected top 0x%08x — assuming success", top);
    return 0;
}

int domain_send_message(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         const HistoryPeer *peer,
                         const char *message,
                         int32_t *msg_id_out,
                         RpcError *err) {
    return domain_send_message_reply(cfg, s, t, peer, message, 0,
                                       msg_id_out, err);
}
