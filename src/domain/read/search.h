/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/search.h
 * @brief US-10 — message search.
 *
 * Per-peer (messages.search) and global (messages.searchGlobal) queries.
 * Returns full HistoryEntry results including message text, date, and media.
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
