/**
 * @file domain/read/search.c
 * @brief messages.search / messages.searchGlobal minimal parser.
 */

#include "domain/read/search.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_messages_search       0x29ee847au
#define CRC_messages_searchGlobal 0x4bc6589au
#define CRC_inputMessagesFilterEmpty 0x57e9a944u

/* Input-peer writer shared with history.c would live under app/, but to
 * keep the library split tidy we duplicate the small switch here. */
static int write_input_peer(TlWriter *w, const HistoryPeer *p) {
    switch (p->kind) {
    case HISTORY_PEER_SELF:
        tl_write_uint32(w, TL_inputPeerSelf); return 0;
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
    default: return -1;
    }
}

/* Stop-iteration mask — must match history.c. flag.9 (media) no longer
 * stops iteration since tl_skip_message_media handles it. */
#define MSG_FLAGS_STOP_ITER ( \
      (1u << 6) | (1u << 20) | (1u << 22) | (1u << 23) )

/* Message parser — identical semantics to history.c::parse_message. */
static int parse_message(TlReader *r, HistoryEntry *out) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == TL_messageEmpty) {
        uint32_t flags = tl_read_uint32(r);
        out->id = tl_read_int32(r);
        if (flags & 1u) { tl_read_uint32(r); tl_read_int64(r); }
        return 0;
    }
    if (crc != TL_message && crc != TL_messageService) return -1;

    uint32_t flags  = tl_read_uint32(r);
    uint32_t flags2 = tl_read_uint32(r);
    out->out = (flags & (1u << 1)) ? 1 : 0;
    out->id  = tl_read_int32(r);
    if (crc == TL_messageService) { out->complex = 1; return -1; }

    if (flags & (1u << 8))   if (tl_skip_peer(r) != 0) { out->complex=1; return -1; }
    if (tl_skip_peer(r) != 0) { out->complex = 1; return -1; }
    if (flags & (1u << 28))  if (tl_skip_peer(r) != 0) { out->complex=1; return -1; }
    if (flags & (1u << 2))   if (tl_skip_message_fwd_header(r) != 0) { out->complex=1; return -1; }
    if (flags & (1u << 11))  { if (r->len - r->pos < 8) { out->complex=1; return -1; } tl_read_int64(r); }
    if (flags2 & (1u << 0))  { if (r->len - r->pos < 8) { out->complex=1; return -1; } tl_read_int64(r); }
    if (flags & (1u << 3))   if (tl_skip_message_reply_header(r) != 0) { out->complex=1; return -1; }
    if (r->len - r->pos < 4) { out->complex=1; return -1; }
    out->date = tl_read_int32(r);

    char *msg = tl_read_string(r);
    if (msg) {
        size_t n = strlen(msg);
        if (n >= HISTORY_TEXT_MAX) { n = HISTORY_TEXT_MAX - 1; out->truncated = 1; }
        memcpy(out->text, msg, n);
        out->text[n] = '\0';
        free(msg);
    }

    if (flags & (1u << 9)) {
        if (tl_skip_message_media(r) != 0) { out->complex=1; return -1; }
    }
    if (flags & MSG_FLAGS_STOP_ITER) { out->complex = 1; return -1; }

    if (flags & (1u << 7))   if (tl_skip_message_entities_vector(r) != 0) { out->complex=1; return -1; }
    if (flags & (1u << 10))  { if (r->len - r->pos < 8) { out->complex=1; return -1; } tl_read_int32(r); tl_read_int32(r); }
    if (flags & (1u << 15))  { if (r->len - r->pos < 4) { out->complex=1; return -1; } tl_read_int32(r); }
    if (flags & (1u << 16))  if (tl_skip_string(r) != 0) { out->complex=1; return -1; }
    if (flags & (1u << 17))  { if (r->len - r->pos < 8) { out->complex=1; return -1; } tl_read_int64(r); }
    if (flags & (1u << 25))  { if (r->len - r->pos < 4) { out->complex=1; return -1; } tl_read_int32(r); }
    if (flags2 & (1u << 30)) { if (r->len - r->pos < 4) { out->complex=1; return -1; } tl_read_int32(r); }
    if (flags2 & (1u << 2))  { if (r->len - r->pos < 8) { out->complex=1; return -1; } tl_read_int64(r); }
    return 0;
}

static int parse_top(const uint8_t *resp, size_t resp_len,
                      HistoryEntry *out, int limit, int *out_count) {
    TlReader r = tl_reader_init(resp, resp_len);
    uint32_t top = tl_read_uint32(&r);

    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "search: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (top != TL_messages_messages &&
        top != TL_messages_messagesSlice &&
        top != TL_messages_channelMessages) {
        logger_log(LOG_ERROR, "search: unexpected top 0x%08x", top);
        return -1;
    }

    if (top == TL_messages_messagesSlice) {
        tl_read_uint32(&r); tl_read_int32(&r); /* flags, count */
    } else if (top == TL_messages_channelMessages) {
        tl_read_uint32(&r); tl_read_int32(&r); tl_read_int32(&r);
    }

    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) return -1;
    uint32_t count = tl_read_uint32(&r);
    int written = 0;
    for (uint32_t i = 0; i < count && written < limit; i++) {
        HistoryEntry e = {0};
        int rc = parse_message(&r, &e);
        if (e.id != 0 || e.text[0] != '\0') out[written++] = e;
        if (rc != 0) break;
    }
    *out_count = written;
    return 0;
}

int domain_search_peer(const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer, const char *query,
                        int limit,
                        HistoryEntry *out, int *out_count) {
    if (!cfg || !s || !t || !peer || !query || !out || !out_count || limit <= 0)
        return -1;
    *out_count = 0;

    /* messages.search#29ee847a flags:# peer:InputPeer q:string
     *     from_id:flags.0?InputPeer saved_peer_id:flags.2?InputPeer
     *     saved_reaction:flags.3?Vector<Reaction>
     *     top_msg_id:flags.1?int filter:MessagesFilter
     *     min_date:int max_date:int offset_id:int add_offset:int
     *     limit:int max_id:int min_id:int hash:long = messages.Messages
     */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_search);
    tl_write_uint32(&w, 0); /* flags = 0 */
    if (write_input_peer(&w, peer) != 0) { tl_writer_free(&w); return -1; }
    tl_write_string(&w, query);
    tl_write_uint32(&w, CRC_inputMessagesFilterEmpty);
    tl_write_int32 (&w, 0); /* min_date */
    tl_write_int32 (&w, 0); /* max_date */
    tl_write_int32 (&w, 0); /* offset_id */
    tl_write_int32 (&w, 0); /* add_offset */
    tl_write_int32 (&w, limit);
    tl_write_int32 (&w, 0); /* max_id */
    tl_write_int32 (&w, 0); /* min_id */
    tl_write_int64 (&w, 0); /* hash */

    uint8_t query_buf[2048];
    if (w.len > sizeof(query_buf)) { tl_writer_free(&w); return -1; }
    memcpy(query_buf, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(262144);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query_buf, qlen, resp, 262144, &resp_len) != 0)
        return -1;
    if (resp_len < 4) return -1;
    return parse_top(resp, resp_len, out, limit, out_count);
}

int domain_search_global(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const char *query, int limit,
                          HistoryEntry *out, int *out_count) {
    if (!cfg || !s || !t || !query || !out || !out_count || limit <= 0) return -1;
    *out_count = 0;

    /* messages.searchGlobal#4bc6589a flags:# folder_id:flags.0?int
     *     q:string filter:MessagesFilter min_date:int max_date:int
     *     offset_rate:int offset_peer:InputPeer offset_id:int
     *     limit:int = messages.Messages
     */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_searchGlobal);
    tl_write_uint32(&w, 0); /* flags */
    tl_write_string(&w, query);
    tl_write_uint32(&w, CRC_inputMessagesFilterEmpty);
    tl_write_int32 (&w, 0); /* min_date */
    tl_write_int32 (&w, 0); /* max_date */
    tl_write_int32 (&w, 0); /* offset_rate */
    tl_write_uint32(&w, TL_inputPeerEmpty); /* offset_peer */
    tl_write_int32 (&w, 0); /* offset_id */
    tl_write_int32 (&w, limit);

    uint8_t query_buf[2048];
    if (w.len > sizeof(query_buf)) { tl_writer_free(&w); return -1; }
    memcpy(query_buf, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(262144);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query_buf, qlen, resp, 262144, &resp_len) != 0)
        return -1;
    if (resp_len < 4) return -1;
    return parse_top(resp, resp_len, out, limit, out_count);
}
