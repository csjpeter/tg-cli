/**
 * @file domain/read/history.h
 * @brief US-06 — fetch message history for a peer.
 *
 * V1 limitations: supports only inputPeerSelf (the user's "Saved Messages").
 * Message text extraction is deferred to v2 because the full Message TL
 * object has too many flag-conditional fields to parse reliably without
 * a schema-driven parser. V1 extracts id + date + outgoing flag.
 *
 * Supporting inputPeerUser/inputPeerChannel requires resolving
 * access_hash from contacts.resolveUsername or from the dialogs
 * response — that is US-09 scope.
 */

#ifndef DOMAIN_READ_HISTORY_H
#define DOMAIN_READ_HISTORY_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t id;
    int32_t date;       /**< Unix epoch seconds. */
    int     out;        /**< Non-zero if message is outgoing. */
} HistoryEntry;

/**
 * @brief Fetch up to @p limit messages from the Saved Messages peer
 *        (inputPeerSelf) starting from @p offset_id.
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Connected transport.
 * @param offset_id Start before this message id (0 = latest).
 * @param limit     Max messages to request (1..100 typical).
 * @param out       Output array of length >= @p limit.
 * @param out_count Receives entries actually written.
 * @return 0 on success, -1 on error.
 */
int domain_get_history_self(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             int32_t offset_id, int limit,
                             HistoryEntry *out, int *out_count);

#endif /* DOMAIN_READ_HISTORY_H */
