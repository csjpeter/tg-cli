/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

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
#include <time.h>

#include <stdlib.h>
#include <string.h>

/* ---- In-memory TTL cache ---- */

/** Default TTL for the dialogs cache (seconds). */
#ifndef DIALOGS_CACHE_TTL_S
#define DIALOGS_CACHE_TTL_S 60
#endif

/** Maximum cached dialogs per call site (archived=0 and archived=1). */
#define DIALOGS_CACHE_SLOTS 2
#define DIALOGS_CACHE_MAX   512

typedef struct {
    int         valid;
    int         archived;
    time_t      fetched_at;
    int         count;
    int         total_count;
    DialogEntry entries[DIALOGS_CACHE_MAX];
} DialogsCache;

static DialogsCache s_cache[DIALOGS_CACHE_SLOTS]; /* [0]=inbox [1]=archive */

/** @brief Mockable clock — tests may replace this with a fake. */
static time_t (*s_now_fn)(void) = NULL;

static time_t dialogs_now(void) {
    if (s_now_fn) return s_now_fn();
    return time(NULL);
}

/**
 * @brief Override the clock used for TTL checks (test use only).
 * Pass NULL to restore the real clock.
 */
void dialogs_cache_set_now_fn(time_t (*fn)(void)) {
    s_now_fn = fn;
}

/**
 * @brief Flush the in-memory dialogs cache (test use only).
 *
 * Call before each unit test that drives domain_get_dialogs directly so
 * that cached state from a previous test does not mask a fresh RPC.
 */
void dialogs_cache_flush(void) {
    memset(s_cache, 0, sizeof(s_cache));
}


#define CRC_messages_getDialogs 0xa0f4cb4fu
#define CRC_inputPeerEmpty      0x7f3b18eau

/* ---- Request builder ---- */

/* flags bit for the optional folder_id field in messages.getDialogs */
#define GETDIALOGS_FLAG_FOLDER_ID (1u << 1)

static int build_request(int limit, int archived,
                         uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_getDialogs);
    uint32_t flags = archived ? GETDIALOGS_FLAG_FOLDER_ID : 0u;
    tl_write_uint32(&w, flags);                   /* flags */
    if (archived)
        tl_write_int32(&w, 1);                    /* folder_id = 1 (Archive) */
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

/* Skip a dialogFolder entry after its CRC has been consumed.
 * Layout: flags(uint32) peer:Peer top_message(int32) + 4×int32 counts.
 * pinned:flags.2?true is a flag bit, not a wire field. */
static int skip_dialog_folder(TlReader *r) {
    tl_read_uint32(r); /* flags */
    if (tl_skip_peer(r) != 0) return -1;
    for (int k = 0; k < 5; k++) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    return 0;
}

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
                       int max_entries, int archived,
                       DialogEntry *out, int *out_count,
                       int *total_count) {
    if (!cfg || !s || !t || !out || !out_count || max_entries <= 0) return -1;
    *out_count = 0;
    if (total_count) *total_count = 0;

    /* ---- TTL cache check ---- */
    int slot = archived ? 1 : 0;
    DialogsCache *cache = &s_cache[slot];
    time_t now = dialogs_now();
    if (cache->valid && (now - cache->fetched_at) < DIALOGS_CACHE_TTL_S) {
        int n = cache->count < max_entries ? cache->count : max_entries;
        memcpy(out, cache->entries, (size_t)n * sizeof(DialogEntry));
        *out_count = n;
        if (total_count) *total_count = cache->total_count;
        logger_log(LOG_DEBUG, "dialogs: served %d entries from cache (age=%lds)",
                   n, (long)(now - cache->fetched_at));
        return 0;
    }

    uint8_t query[132];
    size_t qlen = 0;
    if (build_request(max_entries, archived, query, sizeof(query), &qlen) != 0) {
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

    /* messages.dialogsNotModified#f0e3e596 count:int
     * Returned by the server when the client's hash matches the cached list —
     * no entries follow.  We surface count via total_count and return 0
     * dialogs so callers know the cache is valid. */
    if (top == TL_messages_dialogsNotModified) {
        TlReader r = tl_reader_init(resp, resp_len);
        tl_read_uint32(&r); /* skip constructor */
        int32_t srv_count = tl_read_int32(&r);
        if (total_count) *total_count = (int)srv_count;
        logger_log(LOG_DEBUG,
                   "dialogs: not-modified, server count=%d", srv_count);
        return 0; /* *out_count remains 0; caller should use its cache */
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
        int32_t slice_total = tl_read_int32(&r);
        if (total_count) *total_count = (int)slice_total;
    }

    /* dialogs vector */
    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) {
        logger_log(LOG_ERROR, "dialogs: expected Vector<Dialog>, got 0x%08x", vec);
        return -1;
    }
    uint32_t count = tl_read_uint32(&r);
    /* For the complete-list variant, total == the vector length. */
    if (top == TL_messages_dialogs && total_count) *total_count = (int)count;
    int written = 0;
    int parsed  = 0; /* entries fully consumed from the stream */
    for (uint32_t i = 0; i < count && written < max_entries; i++) {
        if (!tl_reader_ok(&r)) break;
        uint32_t dcrc = tl_read_uint32(&r);
        if (dcrc == CRC_dialogFolder) {
            /* Skip the archive-folder summary entry; it is not a real dialog.
             * Advance past it so the cursor stays aligned for the join. */
            if (skip_dialog_folder(&r) != 0) break;
            logger_log(LOG_DEBUG, "dialogs: skipped dialogFolder entry");
            parsed++;
            continue;
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
            parsed++;
            break;
        }

        if (flags & (1u << 0)) tl_read_int32(&r); /* pts */
        if (flags & (1u << 1)) {
            if (tl_skip_draft_message(&r) != 0) {
                logger_log(LOG_WARN,
                           "dialogs: complex draft — stopping after entry %u",
                           i);
                out[written++] = e;
                parsed++;
                break;
            }
        }
        if (flags & (1u << 4)) tl_read_int32(&r); /* folder_id */
        if (flags & (1u << 5)) tl_read_int32(&r); /* ttl_period */

        out[written++] = e;
        parsed++;
    }

    *out_count = written;

    /* ---- Populate cache after successful RPC (before title join) ----
     * The join adds titles in-place so we store after the join, but we
     * need to handle all exit paths.  Prime the cache fields now so that
     * the goto-jump at join_done always sees a consistent state. */
    {
        int cached_n = written < DIALOGS_CACHE_MAX ? written : DIALOGS_CACHE_MAX;
        cache->valid      = 1;
        cache->archived   = archived;
        cache->fetched_at = dialogs_now();
        cache->count      = cached_n;
        cache->total_count = total_count ? *total_count : written;
        memcpy(cache->entries, out, (size_t)cached_n * sizeof(DialogEntry));
    }

    /* ---- Title join ----
     *
     * messages.dialogs / dialogsSlice continues with:
     *   messages:Vector<Message>
     *   chats:Vector<Chat>
     *   users:Vector<User>
     *
     * Proceed only when every Dialog entry was fully consumed from the stream
     * (parsed == count); otherwise the cursor is mis-positioned and reading
     * the messages/chats/users vectors would produce garbage. */
    if (parsed < (int)count) return 0;              /* partial parse — skip join */
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

    /* Fill DialogEntry title/username + access_hash by looking up peer_id. */
    for (int i = 0; i < *out_count; i++) {
        DialogEntry *e = &out[i];
        if (e->kind == DIALOG_PEER_USER) {
            for (uint32_t j = 0; j < users_written; j++) {
                if (users[j].id == e->peer_id) {
                    memcpy(e->title,    users[j].name,     sizeof(e->title));
                    memcpy(e->username, users[j].username, sizeof(e->username));
                    e->access_hash      = users[j].access_hash;
                    e->have_access_hash = users[j].have_access_hash;
                    break;
                }
            }
        } else { /* CHAT / CHANNEL */
            for (uint32_t j = 0; j < chats_written; j++) {
                if (chats[j].id == e->peer_id) {
                    memcpy(e->title, chats[j].title, sizeof(e->title));
                    /* Legacy chat has no access_hash on the wire; leave
                     * have_access_hash=0 so the caller knows. Channels do. */
                    e->access_hash      = chats[j].access_hash;
                    e->have_access_hash = chats[j].have_access_hash;
                    break;
                }
            }
        }
    }
    free(chats);
    free(users);
    /* Refresh cached entries to include joined titles / access_hashes. */
    {
        int cached_n = *out_count < DIALOGS_CACHE_MAX ? *out_count : DIALOGS_CACHE_MAX;
        memcpy(cache->entries, out, (size_t)cached_n * sizeof(DialogEntry));
    }
join_done:
    return 0;
}

int domain_dialogs_find_by_id(int64_t peer_id, DialogEntry *out) {
    for (int slot = 0; slot < DIALOGS_CACHE_SLOTS; slot++) {
        const DialogsCache *c = &s_cache[slot];
        if (!c->valid) continue;
        for (int i = 0; i < c->count; i++) {
            if (c->entries[i].peer_id == peer_id) {
                if (out) *out = c->entries[i];
                return 0;
            }
        }
    }
    return -1;
}
