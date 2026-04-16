/**
 * @file domain/read/search.h
 * @brief US-10 — message search.
 *
 * Per-peer (messages.search) and global (messages.searchGlobal) queries.
 * The peer descriptor is reused from history.h so the CLI only has to
 * resolve a username once.
 *
 * V1 limitation: like domain_get_history, only the message id prefix
 * is parsed; full-text extraction is deferred.
 */

#ifndef DOMAIN_READ_SEARCH_H
#define DOMAIN_READ_SEARCH_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "domain/read/history.h" /* HistoryPeer, HistoryEntry */

#include <stdint.h>

/**
 * @brief Global search (no peer scope).
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Connected transport.
 * @param query     UTF-8 search query.
 * @param limit     1..100 typical.
 * @param out       Output array.
 * @param out_count Entries written.
 * @return 0 on success, -1 on error.
 */
int domain_search_global(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const char *query, int limit,
                          HistoryEntry *out, int *out_count);

/**
 * @brief Per-peer search.
 */
int domain_search_peer(const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer, const char *query,
                        int limit,
                        HistoryEntry *out, int *out_count);

#endif /* DOMAIN_READ_SEARCH_H */
