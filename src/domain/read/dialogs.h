/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/dialogs.h
 * @brief US-04 — list dialogs (DMs, groups, channels).
 *
 * Read-only. Wraps messages.getDialogs. The v1 parse extracts only the
 * peer id, type and unread count per dialog — title enrichment (joining
 * with the users/chats vectors in the same response) is a follow-up
 * task.
 */

#ifndef DOMAIN_READ_DIALOGS_H
#define DOMAIN_READ_DIALOGS_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/** @brief Peer kind — matches the TL Peer/InputPeer hierarchy. */
typedef enum {
    DIALOG_PEER_UNKNOWN = 0,
    DIALOG_PEER_USER,
    DIALOG_PEER_CHAT,      /**< Legacy small group. */
    DIALOG_PEER_CHANNEL,   /**< Channel or supergroup. */
} DialogPeerKind;

typedef struct {
    DialogPeerKind kind;
    int64_t        peer_id;
    int64_t        access_hash;     /**< 0 for legacy CHAT; set for USER/CHANNEL when carried in the payload. */
    int            have_access_hash;/**< 1 when the wire payload included an access_hash. */
    int32_t        unread_count;
    int32_t        top_message_id;
    char           title[128];     /**< Chat/channel title or user name. */
    char           username[64];   /**< Public username without '@' (users/channels). */
} DialogEntry;

/**
 * @brief Override the clock used for TTL cache checks.
 *
 * Pass a function that returns the current time, or NULL to restore
 * the real clock.  Intended for test use only — allows fast-forwarding
 * time without sleeping.
 */
void dialogs_cache_set_now_fn(time_t (*fn)(void));

/**
 * @brief Flush the in-memory dialogs cache (test use only).
 *
 * Call before each unit or functional test that drives domain_get_dialogs
 * so that cached state from a previous test does not mask a fresh RPC.
 */
void dialogs_cache_flush(void);

/**
 * @brief Fetch up to @p max_entries dialogs.
 *
 * Issues messages.getDialogs with limit=@p max_entries from the zero
 * offset.
 *
 * @param cfg         API config.
 * @param s           Session (with auth_key).
 * @param t           Connected transport.
 * @param max_entries Maximum entries to request and to write to @p out.
 * @param archived    Non-zero to request the archive folder (folder_id=1);
 *                    zero for the default inbox (folder_id=0).
 * @param out         Output array of length >= @p max_entries.
 * @param out_count   Receives the number of entries actually written.
 * @param total_count If non-NULL, receives the server-reported total dialog
 *                    count from a messages.dialogsSlice response (the "count"
 *                    field which may be larger than the returned batch).  For
 *                    messages.dialogs (complete list) this is set to the same
 *                    value as @p out_count.  Pass NULL to ignore.
 * @return 0 on success, -1 on RPC/parse error.
 */
int domain_get_dialogs(const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       int max_entries, int archived,
                       DialogEntry *out, int *out_count,
                       int *total_count);

#endif /* DOMAIN_READ_DIALOGS_H */
