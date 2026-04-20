/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/history.c
 * @brief Minimal messages.getHistory parser (US-06 v1).
 */

#include "domain/read/history.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_messages_getHistory 0x4423e6c5u
#define CRC_inputPeerSelf_local TL_inputPeerSelf /* alias for readability */

/* ---- MessageAction CRCs (US-29) ----
 * Stable across recent TL layers (170+). See docs/userstory/US-29 for
 * the rendering policy. Unknown action CRCs fall through to a labelled
 * "[service action 0x%08x]" string to keep group histories dense even
 * when Telegram adds new variants. */
#define CRC_messageActionEmpty                0xb6aef7b0u
#define CRC_messageActionChatCreate           0xbd47cbadu
#define CRC_messageActionChatEditTitle        0xb5a1ce5au
#define CRC_messageActionChatEditPhoto        0x7fcb13a8u
#define CRC_messageActionChatDeletePhoto      0x95e3fbefu
#define CRC_messageActionChatAddUser          0x15cefd00u
#define CRC_messageActionChatDeleteUser       0xa43f30ccu
#define CRC_messageActionChatJoinedByLink     0x031224c3u
#define CRC_messageActionChannelCreate        0x95d2ac92u
#define CRC_messageActionChatMigrateTo        0xe1037f92u
#define CRC_messageActionChannelMigrateFrom   0xea3948e9u
#define CRC_messageActionPinMessage           0x94bd38edu
#define CRC_messageActionHistoryClear         0x9fbab604u
#define CRC_messageActionPhoneCall            0x80e11a7fu
#define CRC_messageActionScreenshotTaken      0x4792929bu
#define CRC_messageActionCustomAction         0xfae69f56u
#define CRC_messageActionGroupCall            0x7a0d7f42u
#define CRC_messageActionGroupCallScheduled   0xb3a07661u
#define CRC_messageActionInviteToGroupCall    0x502f92f7u

/* Phone-call discard reason CRCs (flags.0 on PhoneCall). */
#define CRC_phoneCallDiscardReasonMissed      0x85e42301u
#define CRC_phoneCallDiscardReasonDisconnect  0xe095c1a0u
#define CRC_phoneCallDiscardReasonHangup      0x57adc690u
#define CRC_phoneCallDiscardReasonBusy        0xfaf7e8c9u

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

/* All trailer flags now have skippers; only factcheck (flags2.3)
 * still halts iteration. */

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

/* Copy src into out->text with HISTORY_TEXT_MAX truncation. */
static void set_text(HistoryEntry *out, const char *src) {
    if (!src) { out->text[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= HISTORY_TEXT_MAX) { n = HISTORY_TEXT_MAX - 1; out->truncated = 1; }
    memcpy(out->text, src, n);
    out->text[n] = '\0';
}

/* Read and discard a Vector<long>. Returns 0 on success. */
static int skip_long_vector(TlReader *r) {
    if (r->len - r->pos < 8) return -1;
    if (tl_read_uint32(r) != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    if (n > 4096) return -1; /* sanity cap */
    if (r->len - r->pos < (size_t)n * 8) return -1;
    for (uint32_t i = 0; i < n; i++) tl_read_int64(r);
    return 0;
}

/* Discard the remaining Vector<long>; return the first element (or 0). */
static int read_long_vector_first(TlReader *r, int64_t *first) {
    if (r->len - r->pos < 8) return -1;
    if (tl_read_uint32(r) != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    if (n > 4096) return -1;
    if (r->len - r->pos < (size_t)n * 8) return -1;
    if (first) *first = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t v = tl_read_int64(r);
        if (i == 0 && first) *first = v;
    }
    return 0;
}

/* Read a MessageAction and render a human-readable string into out->text.
 * Sets out->is_service=1 in all cases. Returns 0 if the action was fully
 * consumed (cursor past), -1 on truncation / unknown substructure. */
static int parse_service_action(TlReader *r, HistoryEntry *out) {
    out->is_service = 1;
    if (r->len - r->pos < 4) { set_text(out, "[service action truncated]"); return -1; }
    uint32_t acrc = tl_read_uint32(r);
    char buf[HISTORY_TEXT_MAX];

    switch (acrc) {
    case CRC_messageActionEmpty:
        set_text(out, "");
        return 0;
    case CRC_messageActionChatCreate: {
        /* title:string users:Vector<long> */
        RAII_STRING char *title = tl_read_string(r);
        if (!title) return -1;
        if (skip_long_vector(r) != 0) return -1;
        snprintf(buf, sizeof(buf), "created group '%s'", title);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChatEditTitle: {
        /* title:string */
        RAII_STRING char *title = tl_read_string(r);
        if (!title) return -1;
        snprintf(buf, sizeof(buf), "changed title to '%s'", title);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChatEditPhoto: {
        /* photo:Photo — skip via tl_skip_photo */
        if (tl_skip_photo(r) != 0) { set_text(out, "changed group photo"); return -1; }
        set_text(out, "changed group photo");
        return 0;
    }
    case CRC_messageActionChatDeletePhoto:
        set_text(out, "removed group photo");
        return 0;
    case CRC_messageActionChatAddUser: {
        /* users:Vector<long> */
        int64_t first = 0;
        if (read_long_vector_first(r, &first) != 0) return -1;
        snprintf(buf, sizeof(buf), "added @%lld", (long long)first);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChatDeleteUser: {
        /* user_id:long */
        if (r->len - r->pos < 8) return -1;
        int64_t uid = tl_read_int64(r);
        snprintf(buf, sizeof(buf), "removed @%lld", (long long)uid);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChatJoinedByLink: {
        /* inviter_id:long */
        if (r->len - r->pos < 8) return -1;
        (void)tl_read_int64(r);
        set_text(out, "joined via invite link");
        return 0;
    }
    case CRC_messageActionChannelCreate: {
        /* title:string */
        RAII_STRING char *title = tl_read_string(r);
        if (!title) return -1;
        snprintf(buf, sizeof(buf), "created channel '%s'", title);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChatMigrateTo: {
        /* channel_id:long */
        if (r->len - r->pos < 8) return -1;
        int64_t cid = tl_read_int64(r);
        snprintf(buf, sizeof(buf), "migrated to channel %lld", (long long)cid);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionChannelMigrateFrom: {
        /* title:string chat_id:long */
        RAII_STRING char *title = tl_read_string(r);
        if (!title) return -1;
        if (r->len - r->pos < 8) return -1;
        int64_t cid = tl_read_int64(r);
        snprintf(buf, sizeof(buf), "migrated from group %lld '%s'",
                 (long long)cid, title);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionPinMessage:
        /* Pinned message id is carried on the outer reply_to header, not
         * on the action itself. The outer parser captured it already. */
        if (out->text[0] == '\0') set_text(out, "pinned message");
        return 0;
    case CRC_messageActionHistoryClear:
        set_text(out, "history cleared");
        return 0;
    case CRC_messageActionPhoneCall: {
        /* flags:# video:flags.2?true call_id:long reason:flags.0?PhoneCallDiscardReason duration:flags.1?int */
        if (r->len - r->pos < 4 + 8) return -1;
        uint32_t pflags = tl_read_uint32(r);
        (void)tl_read_int64(r); /* call_id */
        const char *reason = "completed";
        if (pflags & 1u) {
            if (r->len - r->pos < 4) return -1;
            uint32_t rcrc = tl_read_uint32(r);
            switch (rcrc) {
            case CRC_phoneCallDiscardReasonMissed:     reason = "missed"; break;
            case CRC_phoneCallDiscardReasonDisconnect: reason = "disconnected"; break;
            case CRC_phoneCallDiscardReasonHangup:     reason = "hangup"; break;
            case CRC_phoneCallDiscardReasonBusy:       reason = "busy"; break;
            default: break;
            }
        }
        int duration = 0;
        if (pflags & (1u << 1)) {
            if (r->len - r->pos < 4) return -1;
            duration = tl_read_int32(r);
        }
        snprintf(buf, sizeof(buf), "called (duration %ds, %s)", duration, reason);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionScreenshotTaken:
        set_text(out, "took screenshot");
        return 0;
    case CRC_messageActionCustomAction: {
        /* message:string — the raw action label chosen by the server. */
        RAII_STRING char *msg = tl_read_string(r);
        if (!msg) return -1;
        set_text(out, msg);
        return 0;
    }
    case CRC_messageActionGroupCall: {
        /* flags:# call:InputGroupCall duration:flags.0?int
         * inputGroupCall#d8aa840f id:long access_hash:long */
        if (r->len - r->pos < 4) return -1;
        uint32_t gflags = tl_read_uint32(r);
        if (r->len - r->pos < 4 + 16) return -1;
        tl_read_uint32(r); /* inputGroupCall crc */
        tl_read_int64(r);  /* id */
        tl_read_int64(r);  /* access_hash */
        if (gflags & 1u) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* duration */
        }
        set_text(out, "started video chat");
        return 0;
    }
    case CRC_messageActionGroupCallScheduled: {
        /* call:InputGroupCall schedule_date:int */
        if (r->len - r->pos < 4 + 16 + 4) return -1;
        tl_read_uint32(r); tl_read_int64(r); tl_read_int64(r);
        int sched = tl_read_int32(r);
        snprintf(buf, sizeof(buf), "scheduled video chat for %d", sched);
        set_text(out, buf);
        return 0;
    }
    case CRC_messageActionInviteToGroupCall: {
        /* call:InputGroupCall users:Vector<long> */
        if (r->len - r->pos < 4 + 16) return -1;
        tl_read_uint32(r); tl_read_int64(r); tl_read_int64(r);
        if (skip_long_vector(r) != 0) return -1;
        set_text(out, "invited to video chat");
        return 0;
    }
    default:
        snprintf(buf, sizeof(buf), "[service action 0x%08x]", acrc);
        set_text(out, buf);
        /* Unknown trailing data: we cannot advance reliably, but the
         * enclosing vector iterator will stop on rc=-1 from parse_message.
         * Return 0 so the entry is emitted with its label. */
        return 0;
    }
}

/* Parse the messageService layout and render the action into out->text.
 * Cursor is positioned just after (flags, id) on entry — messageService
 * does not carry flags2 on the current TL schema. Shared with updates.c
 * and search.c via `domain_history_parse_service` (see header). */
int domain_history_parse_service(TlReader *r, HistoryEntry *out, uint32_t flags) {
    out->is_service = 1;
    if (flags & MSG_FLAG_HAS_FROM_ID) {
        if (tl_skip_peer(r) != 0) { out->complex = 1; return -1; }
    }
    if (tl_skip_peer(r) != 0) { out->complex = 1; return -1; }
    if (flags & (1u << 3)) { /* reply_to — for pinMessage the target id lives here. */
        /* Peek the first 3 words (crc + flags + first optional) without
         * advancing: messageReplyHeader#afbc09db always has flags bit 4 =
         * reply_to_msg_id as the first int32 payload when present. */
        if (r->len - r->pos >= 12) {
            uint32_t rh_crc, rh_flags;
            memcpy(&rh_crc,   r->data + r->pos,     4);
            memcpy(&rh_flags, r->data + r->pos + 4, 4);
            if (rh_crc == 0xafbc09dbu && (rh_flags & (1u << 4))) {
                int32_t pinned;
                memcpy(&pinned, r->data + r->pos + 8, 4);
                char buf[HISTORY_TEXT_MAX];
                snprintf(buf, sizeof(buf), "pinned message %d", pinned);
                set_text(out, buf);
            }
        }
        if (tl_skip_message_reply_header(r) != 0) { out->complex = 1; return -1; }
    }
    if (r->len - r->pos < 4) { out->complex = 1; return -1; }
    out->date = (int64_t)(int32_t)tl_read_int32(r);
    /* Preserve any pre-populated text (e.g. "pinned message N") across
     * parse_service_action for the PinMessage case, which has no body. */
    char saved_text[HISTORY_TEXT_MAX];
    memcpy(saved_text, out->text, sizeof(saved_text));
    int rc = parse_service_action(r, out);
    if (out->text[0] == '\0' && saved_text[0] != '\0')
        memcpy(out->text, saved_text, sizeof(saved_text));
    /* ttl_period (flags.25) — optional. Best-effort skip. */
    if (rc == 0 && (flags & (1u << 25))) {
        if (r->len - r->pos >= 4) tl_read_int32(r);
    }
    return rc;
}

/* Parse a Message. On success advance r past the whole object so the
 * caller can read the next vector element. Returns 0 on success, -1 on
 * parse failure, and sets `out->complex = 1` when we got the text/date
 * but had to bail before reaching the end of the object. */
static int parse_message(TlReader *r, HistoryEntry *out) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_messageEmpty) {
        uint32_t flags = tl_read_uint32(r);
        out->id = tl_read_int32(r);
        if (flags & 1u) { tl_read_uint32(r); tl_read_int64(r); }
        return 0;
    }

    if (crc != TL_message && crc != TL_messageService) {
        logger_log(LOG_WARN, "history: unknown Message 0x%08x", crc);
        return -1;
    }

    uint32_t flags = tl_read_uint32(r);
    uint32_t flags2 = 0;
    /* TL_message carries flags2; TL_messageService does not on the upstream
     * schema. Tests and the in-process mock server mirror this layout. */
    if (crc == TL_message) flags2 = tl_read_uint32(r);
    out->out = (flags & MSG_FLAG_OUT) ? 1 : 0;
    out->id  = tl_read_int32(r);

    if (crc == TL_messageService) {
        /* US-29 — render the action into out->text and return it as a
         * normal row. The enclosing vector iterator continues. */
        return domain_history_parse_service(r, out, flags);
    }

    /* Pre-text fields (message is read AFTER these). */
    if (flags & MSG_FLAG_HAS_FROM_ID) {
        if (tl_skip_peer(r) != 0) { out->complex = 1; return -1; }
    }
    if (tl_skip_peer(r) != 0)         { out->complex = 1; return -1; }
    if (flags & (1u << 28)) { /* saved_peer_id (layer 185+) */
        if (tl_skip_peer(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 2)) { /* fwd_from */
        if (tl_skip_message_fwd_header(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 11)) { /* via_bot_id */
        if (r->len - r->pos < 8) { out->complex = 1; return -1; }
        tl_read_int64(r);
    }
    if (flags2 & (1u << 0)) { /* via_business_bot_id */
        if (r->len - r->pos < 8) { out->complex = 1; return -1; }
        tl_read_int64(r);
    }
    if (flags & (1u << 3)) { /* reply_to */
        if (tl_skip_message_reply_header(r) != 0) { out->complex = 1; return -1; }
    }
    if (r->len - r->pos < 4) { out->complex = 1; return -1; }
    out->date = (int64_t)(int32_t)tl_read_int32(r);

    /* message:string — always present. */
    RAII_STRING char *msg = tl_read_string(r);
    if (msg) {
        size_t n = strlen(msg);
        if (n >= HISTORY_TEXT_MAX) { n = HISTORY_TEXT_MAX - 1; out->truncated = 1; }
        memcpy(out->text, msg, n);
        out->text[n] = '\0';
    }

    /* Skippable optionals after `message` — in schema order:
     *   flags.9  media
     *   flags.6  reply_markup (BAIL — no skipper yet)
     *   flags.7  entities
     *   flags.10 views + forwards
     *   ... (more scalars below)
     *
     * Media is attempted first; if it fails the Message is left
     * mid-parse but we have at least captured id/date/text. */
    if (flags & (1u << 9)) {
        MediaInfo mi = {0};
        if (tl_skip_message_media_ex(r, &mi) != 0) {
            out->media = mi.kind;
            out->complex = 1;
            return -1;
        }
        out->media    = mi.kind;
        out->media_id = (mi.kind == MEDIA_PHOTO) ? mi.photo_id
                      : (mi.kind == MEDIA_DOCUMENT) ? mi.document_id : 0;
        out->media_dc = mi.dc_id;
        out->media_info = mi;
    }

    /* Per-layer order: media → reply_markup → entities → views/forwards
     * → replies → edit_date → post_author → grouped_id → reactions →
     * restriction_reason → ttl_period → ... replies + restriction_reason
     * still don't have skippers. */
    if (flags & (1u << 6)) { /* reply_markup */
        if (tl_skip_reply_markup(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 7))  { /* entities */
        if (tl_skip_message_entities_vector(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 10)) { /* views + forwards */
        if (r->len - r->pos < 8) { out->complex = 1; return -1; }
        tl_read_int32(r); tl_read_int32(r);
    }
    if (flags & (1u << 23)) { /* replies */
        if (tl_skip_message_replies(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 15)) { /* edit_date */
        if (r->len - r->pos < 4) { out->complex = 1; return -1; }
        tl_read_int32(r);
    }
    if (flags & (1u << 16)) { /* post_author */
        if (tl_skip_string(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 17)) { /* grouped_id */
        if (r->len - r->pos < 8) { out->complex = 1; return -1; }
        tl_read_int64(r);
    }
    if (flags & (1u << 20)) { /* reactions */
        if (tl_skip_message_reactions(r) != 0) { out->complex = 1; return -1; }
    }
    if (flags & (1u << 22)) { /* restriction_reason */
        if (tl_skip_restriction_reason_vector(r) != 0) {
            out->complex = 1; return -1;
        }
    }
    if (flags & (1u << 25)) { /* ttl_period */
        if (r->len - r->pos < 4) { out->complex = 1; return -1; }
        tl_read_int32(r);
    }
    if (flags2 & (1u << 30)) { /* quick_reply_shortcut_id */
        if (r->len - r->pos < 4) { out->complex = 1; return -1; }
        tl_read_int32(r);
    }
    if (flags2 & (1u << 2)) { /* effect */
        if (r->len - r->pos < 8) { out->complex = 1; return -1; }
        tl_read_int64(r);
    }
    if (flags2 & (1u << 3)) { /* factcheck */
        if (tl_skip_factcheck(r) != 0) { out->complex = 1; return -1; }
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
        int rc2 = parse_message(&r, &e);
        if (e.id != 0 || e.text[0] != '\0') {
            out[written++] = e;
        }
        if (rc2 != 0) break; /* could not advance past message — stop */
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
