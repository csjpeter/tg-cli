/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/history.h
 * @brief US-06 — fetch message history for a peer.
 *
 * Supports all peer kinds (self, user, legacy chat, channel). Extracts
 * id, date, outgoing flag, message text, media kind, and service-action
 * strings. Peer resolution (access_hash) is the caller's responsibility.
 */

#ifndef DOMAIN_READ_HISTORY_H
#define DOMAIN_READ_HISTORY_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_skip.h"           /* MediaKind + MediaInfo */

#include <stddef.h>
#include <stdint.h>

/** @brief Maximum text size per history entry (bytes incl. NUL). */
#define HISTORY_TEXT_MAX 512

typedef struct {
    int32_t   id;
    int64_t   date;     /**< Unix epoch seconds, 0 if unknown. Widened to int64_t for 2038-safety; wire format is still int32. */
    int       out;      /**< Non-zero if message is outgoing. */
    char      text[HISTORY_TEXT_MAX]; /**< Empty if complex/unparseable. For service messages (US-29) this holds the rendered action string (e.g. "created group 'Name'"). */
    int       truncated;
    int       complex;  /**< Non-zero if a trailing unsupported flag bailed. */
    int       is_service; /**< Non-zero when the row originated from messageService — text carries the rendered action, not user-authored content. */
    MediaKind media;    /**< MEDIA_NONE when flags.9 absent. */
    int64_t   media_id; /**< photo_id or document_id, 0 when N/A. */
    int32_t   media_dc; /**< dc_id for photos; 0 otherwise. */
    MediaInfo media_info; /**< Full metadata for MEDIA_PHOTO downloads. */
    int64_t   peer_id;  /**< Dialog peer id (from Message.peer_id Peer); 0 if unknown. */
} HistoryEntry;

/** @brief InputPeer kind used when building the getHistory request. */
typedef enum {
    HISTORY_PEER_SELF = 0,  /**< inputPeerSelf — no id/hash required. */
    HISTORY_PEER_USER,      /**< inputPeerUser — requires peer_id + access_hash. */
    HISTORY_PEER_CHAT,      /**< inputPeerChat — legacy small group, no hash. */
    HISTORY_PEER_CHANNEL,   /**< inputPeerChannel — requires peer_id + access_hash. */
} HistoryPeerKind;

typedef struct {
    HistoryPeerKind kind;
    int64_t         peer_id;
    int64_t         access_hash;
} HistoryPeer;

/**
 * @brief Fetch up to @p limit messages from the given peer.
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Connected transport.
 * @param peer      Peer descriptor (use HISTORY_PEER_SELF for Saved Messages).
 * @param offset_id Start before this message id (0 = latest).
 * @param limit     Max messages to request (1..100 typical).
 * @param out       Output array of length >= @p limit.
 * @param out_count Receives entries actually written.
 * @return 0 on success, -1 on error.
 */
int domain_get_history(const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer,
                        int32_t offset_id, int limit,
                        HistoryEntry *out, int *out_count);

/** @brief Backwards-compatible helper: Saved Messages only. */
int domain_get_history_self(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             int32_t offset_id, int limit,
                             HistoryEntry *out, int *out_count);

/**
 * @brief Parse a messageService body and render the action into out->text.
 *
 * Called from any Vector<Message> consumer (history.c, updates.c, search.c)
 * after the caller has already read the `crc`, `flags` and `id` fields of
 * the messageService envelope. The cursor must be positioned at the first
 * conditional peer field on entry.
 *
 * Sets out->is_service=1 and populates out->text with a human-readable
 * string per US-29 (e.g. "joined via invite link", "pinned message 42").
 * Unknown action CRCs fall through to a "[service action 0x%08x]" label.
 *
 * @param r     Reader, positioned after (flags, id).
 * @param out   Receives the rendered string / metadata.
 * @param flags The flags:# word from the messageService envelope.
 * @return 0 on success (cursor past the service body), -1 on truncation.
 */
int domain_history_parse_service(TlReader *r, HistoryEntry *out, uint32_t flags);

#endif /* DOMAIN_READ_HISTORY_H */
