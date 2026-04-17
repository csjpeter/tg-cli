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
 * @brief Fetch up to @p max_entries dialogs.
 *
 * Issues messages.getDialogs with limit=@p max_entries from the zero
 * offset.
 *
 * @param cfg         API config.
 * @param s           Session (with auth_key).
 * @param t           Connected transport.
 * @param max_entries Maximum entries to request and to write to @p out.
 * @param out         Output array of length >= @p max_entries.
 * @param out_count   Receives the number of entries actually written.
 * @return 0 on success, -1 on RPC/parse error.
 */
int domain_get_dialogs(const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       int max_entries,
                       DialogEntry *out, int *out_count);

#endif /* DOMAIN_READ_DIALOGS_H */
