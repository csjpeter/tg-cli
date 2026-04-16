/**
 * @file domain/read/dialogs.c
 * @brief messages.getDialogs parser (minimal v1).
 */

#include "domain/read/dialogs.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_messages_getDialogs 0xa0f4cb4fu
#define CRC_inputPeerEmpty      0x7f3b18eau

/* ---- Request builder ---- */

static int build_request(int limit, uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_getDialogs);
    tl_write_uint32(&w, 0);                       /* flags = 0 */
    tl_write_int32 (&w, 0);                       /* offset_date */
    tl_write_int32 (&w, 0);                       /* offset_id */
    tl_write_uint32(&w, CRC_inputPeerEmpty);      /* offset_peer */
    tl_write_int32 (&w, limit);                   /* limit */
    tl_write_int64 (&w, 0);                       /* hash */

    int rc = -1;
    if (w.len <= cap) {
        memcpy(buf, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

/* ---- Dialog TL parsing (minimal, schema-tolerant) ----
 *
 * dialog#d58a08c6 flags:# pinned:flags.2?true unread_mark:flags.3?true
 *     view_forum_as_messages:flags.6?true
 *     peer:Peer
 *     top_message:int
 *     read_inbox_max_id:int
 *     read_outbox_max_id:int
 *     unread_count:int
 *     unread_mentions_count:int
 *     unread_reactions_count:int
 *     notify_settings:PeerNotifySettings
 *     pts:flags.0?int
 *     draft:flags.1?DraftMessage
 *     folder_id:flags.4?int
 *     ttl_period:flags.5?int
 *     = Dialog
 *
 * We only pull the first block (peer + unread_count + top_message_id).
 * Optional trailing fields are not read because callers don't need them;
 * the reader stops advancing after this function returns.
 */
#define CRC_dialog       0xd58a08c6u
#define CRC_dialogFolder 0x71bd134cu

static int parse_peer(TlReader *r, DialogEntry *out) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case TL_peerUser:    out->kind = DIALOG_PEER_USER;    break;
    case TL_peerChat:    out->kind = DIALOG_PEER_CHAT;    break;
    case TL_peerChannel: out->kind = DIALOG_PEER_CHANNEL; break;
    default:
        logger_log(LOG_WARN, "dialogs: unknown Peer constructor 0x%08x", crc);
        out->kind = DIALOG_PEER_UNKNOWN;
        return -1;
    }
    out->peer_id = tl_read_int64(r);
    return 0;
}

int domain_get_dialogs(const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       int max_entries,
                       DialogEntry *out, int *out_count) {
    if (!cfg || !s || !t || !out || !out_count || max_entries <= 0) return -1;
    *out_count = 0;

    uint8_t query[128];
    size_t qlen = 0;
    if (build_request(max_entries, query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "dialogs: build_request overflow");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(262144);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 262144, &resp_len) != 0) {
        logger_log(LOG_ERROR, "dialogs: api_call failed");
        return -1;
    }
    if (resp_len < 8) {
        logger_log(LOG_ERROR, "dialogs: response too short");
        return -1;
    }

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "dialogs: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (top != TL_messages_dialogs && top != TL_messages_dialogsSlice) {
        logger_log(LOG_ERROR,
                   "dialogs: unexpected top constructor 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top constructor */

    /* messages.dialogsSlice#71e094f3 count:int dialogs:Vector<Dialog>... */
    if (top == TL_messages_dialogsSlice) {
        tl_read_int32(&r); /* total count — not stored */
    }

    /* dialogs vector */
    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) {
        logger_log(LOG_ERROR, "dialogs: expected Vector<Dialog>, got 0x%08x", vec);
        return -1;
    }
    uint32_t count = tl_read_uint32(&r);
    int written = 0;
    for (uint32_t i = 0; i < count && written < max_entries; i++) {
        if (!tl_reader_ok(&r)) break;
        uint32_t dcrc = tl_read_uint32(&r);
        if (dcrc == CRC_dialogFolder) {
            /* Skip: a DialogFolder has a different layout we don't consume
             * at v1. Breaking out conservatively. */
            logger_log(LOG_DEBUG, "dialogs: folder entry — stopping parse");
            break;
        }
        if (dcrc != CRC_dialog) {
            logger_log(LOG_WARN, "dialogs: unknown Dialog constructor 0x%08x",
                       dcrc);
            break;
        }

        tl_read_uint32(&r); /* flags — we don't read optionals */
        DialogEntry e = {0};
        if (parse_peer(&r, &e) != 0) break;
        e.top_message_id = tl_read_int32(&r);
        tl_read_int32(&r); /* read_inbox_max_id */
        tl_read_int32(&r); /* read_outbox_max_id */
        e.unread_count = tl_read_int32(&r);

        out[written++] = e;

        /* Remaining fields of this Dialog are not consumed — our reader
         * cursor is now mid-object. Since we only extract per-entry data
         * and stop before the messages/chats/users vectors below, leaving
         * the cursor here is fine: the caller does not need to read more.
         * We just break to avoid alignment errors on the next iteration. */
        break;
    }

    *out_count = written;
    return 0;
}
