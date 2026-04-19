/**
 * @file domain/read/user_info.h
 * @brief US-09 — resolve @username into a user or channel summary.
 *
 * V1 supports contacts.resolveUsername and surfaces the peer id. The
 * Telegram response also carries access_hash on User / Channel objects;
 * we attempt a best-effort extraction, but callers should treat the
 * hash as opaque and verify via a follow-up API call before trusting it.
 */

#ifndef DOMAIN_READ_USER_INFO_H
#define DOMAIN_READ_USER_INFO_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stdint.h>

typedef enum {
    RESOLVED_KIND_UNKNOWN = 0,
    RESOLVED_KIND_USER,
    RESOLVED_KIND_CHAT,
    RESOLVED_KIND_CHANNEL,
} ResolvedKind;

typedef struct {
    ResolvedKind kind;
    int64_t      id;
    int64_t      access_hash;  /**< 0 if unavailable. */
    char         title[128];   /**< For channels/chats; first+last for users. */
    char         username[64]; /**< Echoes the requested username. */
    int          have_hash;    /**< Non-zero if @p access_hash is valid. */
} ResolvedPeer;

/**
 * @brief Resolve a @username to a ResolvedPeer.
 *
 * Results are cached in a small session-scoped LRU table so that a
 * second call for the same username within RESOLVE_CACHE_TTL_S seconds
 * skips the RPC entirely.
 *
 * Strips a leading '@' from @p username if present.
 *
 * @return 0 on success, -1 on RPC or parse error.
 */
int domain_resolve_username(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const char *username,
                             ResolvedPeer *out);

/**
 * @brief Flush the in-memory resolve cache (test use only).
 *
 * Call before tests that drive domain_resolve_username to avoid stale
 * cache hits masking fresh RPCs.
 */
void resolve_cache_flush(void);

#endif /* DOMAIN_READ_USER_INFO_H */
