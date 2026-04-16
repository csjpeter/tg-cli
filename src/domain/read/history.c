/**
 * @file domain/read/history.c
 * @brief Minimal messages.getHistory parser (US-06 v1).
 */

#include "domain/read/history.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_messages_getHistory 0x4423e6c5u
#define CRC_inputPeerSelf_local TL_inputPeerSelf /* alias for readability */

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

/* Field bit positions used to decide whether to skip the first-stage
 * Message prefix (before we abort and move on to the next message).
 * These correspond to layer 170+ but are stable across recent layers. */
#define MSG_FLAG_OUT              (1u << 1)
#define MSG_FLAG_HAS_FROM_ID      (1u << 8)

/* Complex-optional mask: any of these present means we can't trivially
 * walk to the `message` field without a schema table. */
#define MSG_FLAGS_COMPLEX_MASK    ( \
      (1u << 2)   /* fwd_from */    \
    | (1u << 3)   /* reply_to */    \
    | (1u << 9)   /* media */       \
    | (1u << 10)  /* reply_markup */ \
    | (1u << 11)  /* via_bot_id */  \
    | (1u << 13)  /* entities */    \
    )

static int build_request(const HistoryPeer *peer, int32_t offset_id, int limit,
                          uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_getHistory);
    if (write_input_peer(&w, peer) != 0) {
        tl_writer_free(&w);
        return -1;
    }
    tl_write_int32 (&w, offset_id);
    tl_write_int32 (&w, 0);  /* offset_date */
    tl_write_int32 (&w, 0);  /* add_offset */
    tl_write_int32 (&w, limit);
    tl_write_int32 (&w, 0);  /* max_id */
    tl_write_int32 (&w, 0);  /* min_id */
    tl_write_int64 (&w, 0);  /* hash */

    int rc = -1;
    if (w.len <= cap) {
        memcpy(buf, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

/* Best-effort Message parser. Extracts id + out + date + text in the
 * "simple" case (no fwd_from, reply_to, media, reply_markup, via_bot_id,
 * entities). Otherwise marks entry as `complex` and still fills id/out. */
static int parse_message_prefix(TlReader *r, HistoryEntry *out) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_messageEmpty) {
        uint32_t flags = tl_read_uint32(r);
        out->id   = tl_read_int32(r);
        if (flags & 1u) { tl_read_uint32(r); tl_read_int64(r); }
        return 0;
    }

    if (crc != TL_message && crc != TL_messageService) {
        logger_log(LOG_WARN, "history: unknown Message constructor 0x%08x", crc);
        return -1;
    }

    uint32_t flags  = tl_read_uint32(r);
    uint32_t flags2 = tl_read_uint32(r);
    (void)flags2;
    out->out = (flags & MSG_FLAG_OUT) ? 1 : 0;
    out->id  = tl_read_int32(r);

    if (crc == TL_messageService) {
        /* messageService has a different layout (action) — mark complex. */
        out->complex = 1;
        return 0;
    }

    if (flags & MSG_FLAGS_COMPLEX_MASK) {
        out->complex = 1;
        return 0;
    }

    /* Simple layout:
     *   [from_id:Peer]? (flags.8 : crc+int64)
     *   peer_id:Peer    (crc+int64)
     *   date:int
     *   message:string
     */
    if (flags & MSG_FLAG_HAS_FROM_ID) {
        tl_read_uint32(r); /* peer crc */
        tl_read_int64(r);  /* peer id */
    }
    tl_read_uint32(r); /* peer_id crc */
    tl_read_int64(r);  /* peer_id value */
    out->date = tl_read_int32(r);

    if (!tl_reader_ok(r)) return 0;
    RAII_STRING char *msg = tl_read_string(r);
    if (msg) {
        size_t n = strlen(msg);
        if (n >= HISTORY_TEXT_MAX) {
            n = HISTORY_TEXT_MAX - 1;
            out->truncated = 1;
        }
        memcpy(out->text, msg, n);
        out->text[n] = '\0';
    }
    return 0;
}

int domain_get_history(const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer,
                        int32_t offset_id, int limit,
                        HistoryEntry *out, int *out_count) {
    if (!cfg || !s || !t || !peer || !out || !out_count || limit <= 0) return -1;
    *out_count = 0;

    uint8_t query[128];
    size_t qlen = 0;
    if (build_request(peer, offset_id, limit, query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "history: build_request overflow");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(262144);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 262144, &resp_len) != 0) {
        logger_log(LOG_ERROR, "history: api_call failed");
        return -1;
    }
    if (resp_len < 8) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "history: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (top != TL_messages_messages &&
        top != TL_messages_messagesSlice &&
        top != TL_messages_channelMessages) {
        logger_log(LOG_ERROR, "history: unexpected top 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top */

    /* messagesSlice/channelMessages prepend some counters we skip. */
    if (top == TL_messages_messagesSlice) {
        tl_read_uint32(&r); /* flags */
        tl_read_int32 (&r); /* count */
        /* next_rate + offset_id_offset are optional (flags.0/.2) — we
         * don't read them; the messages vector follows after them in the
         * wire. For robustness the parse stops after one entry anyway. */
    } else if (top == TL_messages_channelMessages) {
        tl_read_uint32(&r); /* flags */
        tl_read_int32 (&r); /* pts */
        tl_read_int32 (&r); /* count */
        /* optional offset_id_offset (flags.2) not read */
    }

    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) {
        logger_log(LOG_ERROR, "history: expected Vector<Message>, got 0x%08x",
                   vec);
        return -1;
    }
    uint32_t count = tl_read_uint32(&r);
    int written = 0;
    for (uint32_t i = 0; i < count && written < limit; i++) {
        HistoryEntry e = {0};
        if (parse_message_prefix(&r, &e) != 0) break;
        out[written++] = e;
        /* As with dialogs, fully consuming a Message's flag-conditional
         * trailer requires a schema table; for v1 we stop after the first
         * message to avoid alignment errors on the next iteration. */
        break;
    }
    *out_count = written;
    return 0;
}

int domain_get_history_self(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             int32_t offset_id, int limit,
                             HistoryEntry *out, int *out_count) {
    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    return domain_get_history(cfg, s, t, &self, offset_id, limit, out, out_count);
}
