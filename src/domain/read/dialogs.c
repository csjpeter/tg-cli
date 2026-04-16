/**
 * @file domain/read/dialogs.c
 * @brief messages.getDialogs parser (minimal v1).
 */

#include "domain/read/dialogs.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stddef.h>

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
            /* dialogFolder has a separate layout — skipping support is a
             * follow-up. Stop iteration cleanly. */
            logger_log(LOG_DEBUG, "dialogs: folder entry — stopping parse");
            break;
        }
        if (dcrc != CRC_dialog) {
            logger_log(LOG_WARN, "dialogs: unknown Dialog constructor 0x%08x",
                       dcrc);
            break;
        }

        uint32_t flags = tl_read_uint32(&r);
        DialogEntry e = {0};
        if (parse_peer(&r, &e) != 0) break;
        e.top_message_id = tl_read_int32(&r);
        tl_read_int32(&r); /* read_inbox_max_id */
        tl_read_int32(&r); /* read_outbox_max_id */
        e.unread_count = tl_read_int32(&r);
        tl_read_int32(&r); /* unread_mentions_count */
        tl_read_int32(&r); /* unread_reactions_count */

        if (tl_skip_peer_notify_settings(&r) != 0) {
            logger_log(LOG_WARN,
                       "dialogs: failed to skip PeerNotifySettings");
            out[written++] = e;
            break;
        }

        if (flags & (1u << 0)) tl_read_int32(&r); /* pts */
        if (flags & (1u << 1)) {
            if (tl_skip_draft_message(&r) != 0) {
                logger_log(LOG_WARN,
                           "dialogs: complex draft — stopping after entry %u",
                           i);
                out[written++] = e;
                break;
            }
        }
        if (flags & (1u << 4)) tl_read_int32(&r); /* folder_id */
        if (flags & (1u << 5)) tl_read_int32(&r); /* ttl_period */

        out[written++] = e;
    }

    *out_count = written;

    /* ---- Title join ----
     *
     * messages.dialogs / dialogsSlice continues with:
     *   messages:Vector<Message>
     *   chats:Vector<Chat>
     *   users:Vector<User>
     *
     * If we consumed every Dialog above, the cursor is positioned at the
     * start of the messages vector. Walk it using tl_skip_message, then
     * build id→title maps from the chats and users vectors and back-fill
     * titles on the returned DialogEntry rows.
     *
     * If ANY step fails (unsupported flag etc.) we stop gracefully and
     * leave whatever titles we collected so far — the caller already has
     * ids and counts, so the feature degrades instead of failing. */
    if (written < (int)count) return 0;              /* partial parse — skip join */
    if (!tl_reader_ok(&r))    return 0;

    uint32_t mvec = tl_read_uint32(&r);
    if (mvec != TL_vector) return 0;
    uint32_t mcount = tl_read_uint32(&r);
    for (uint32_t i = 0; i < mcount; i++) {
        if (tl_skip_message(&r) != 0) return 0;
    }

    uint32_t cvec = tl_read_uint32(&r);
    if (cvec != TL_vector) return 0;
    uint32_t ccount = tl_read_uint32(&r);
    ChatSummary *chats = (ccount > 0)
        ? (ChatSummary *)calloc(ccount, sizeof(ChatSummary))
        : NULL;
    uint32_t chats_written = 0;
    for (uint32_t i = 0; i < ccount; i++) {
        ChatSummary cs = {0};
        if (tl_extract_chat(&r, &cs) != 0) { free(chats); chats = NULL; goto join_done; }
        if (chats) chats[chats_written++] = cs;
    }

    uint32_t uvec = tl_read_uint32(&r);
    if (uvec != TL_vector) { free(chats); goto join_done; }
    uint32_t ucount = tl_read_uint32(&r);
    UserSummary *users = (ucount > 0)
        ? (UserSummary *)calloc(ucount, sizeof(UserSummary))
        : NULL;
    uint32_t users_written = 0;
    for (uint32_t i = 0; i < ucount; i++) {
        UserSummary us = {0};
        if (tl_extract_user(&r, &us) != 0) { free(chats); free(users); goto join_done; }
        if (users) users[users_written++] = us;
    }

    /* Fill DialogEntry title/username by looking up peer_id. */
    for (int i = 0; i < *out_count; i++) {
        DialogEntry *e = &out[i];
        if (e->kind == DIALOG_PEER_USER) {
            for (uint32_t j = 0; j < users_written; j++) {
                if (users[j].id == e->peer_id) {
                    memcpy(e->title,    users[j].name,     sizeof(e->title));
                    memcpy(e->username, users[j].username, sizeof(e->username));
                    break;
                }
            }
        } else { /* CHAT / CHANNEL */
            for (uint32_t j = 0; j < chats_written; j++) {
                if (chats[j].id == e->peer_id) {
                    memcpy(e->title, chats[j].title, sizeof(e->title));
                    break;
                }
            }
        }
    }
    free(chats);
    free(users);
join_done:
    return 0;
}
