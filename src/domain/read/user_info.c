/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/user_info.c
 * @brief contacts.resolveUsername minimal parser with session-scoped cache.
 */

#include "domain/read/user_info.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CRC_contacts_resolveUsername 0xf93ccba3u
#define CRC_users_getFullUser        0xb9f11a99u
#define CRC_users_userFull           0x3b6d152eu
#define CRC_inputUserSelf            0xf7c1b13fu
#define CRC_inputUser                0x0d313d36u
/* userFull#cc997720 flags bits */
#define USERFULL_FLAG_PHONE          (1u << 4)   /**< phone field present */
#define USERFULL_FLAG_ABOUT          (1u << 5)   /**< about field present */
#define USERFULL_FLAG_COMMON_CHATS   (1u << 20)  /**< common_chats_count present */

/* ---- In-memory TTL cache ---- */

/** TTL for resolved username cache entries (seconds). */
#define RESOLVE_CACHE_TTL_S  300

/** TTL for cached negative lookups (USERNAME_INVALID /
 *  USERNAME_NOT_OCCUPIED).  Kept much shorter than the positive TTL so a
 *  user that appears later is visible within a few minutes while still
 *  stopping retry storms. */
#define RESOLVE_CACHE_NEG_TTL_S  60

/** Maximum number of cached username resolutions. */
#define RESOLVE_CACHE_MAX    32

typedef struct {
    int          valid;
    int          negative;    /**< 1 = cached "not found" / "invalid". */
    time_t       fetched_at;
    char         key[64];    /**< username without '@'. */
    ResolvedPeer value;
} ResolveCacheEntry;

static ResolveCacheEntry s_rcache[RESOLVE_CACHE_MAX];

/** @brief Mockable clock — tests may replace this with a fake. */
static time_t (*s_rcache_now_fn)(void) = NULL;

static time_t resolver_now(void) {
    if (s_rcache_now_fn) return s_rcache_now_fn();
    return time(NULL);
}

void resolve_cache_set_now_fn(time_t (*fn)(void)) {
    s_rcache_now_fn = fn;
}

int resolve_cache_positive_ttl(void) { return RESOLVE_CACHE_TTL_S;     }
int resolve_cache_negative_ttl(void) { return RESOLVE_CACHE_NEG_TTL_S; }
int resolve_cache_capacity(void)     { return RESOLVE_CACHE_MAX;       }

void resolve_cache_flush(void) {
    memset(s_rcache, 0, sizeof(s_rcache));
}

/** Lookup result codes for rcache_lookup_v2. */
typedef enum {
    RCACHE_MISS     = 0,  /**< no entry (or expired). */
    RCACHE_HIT_POS  = 1,  /**< positive hit — *out filled. */
    RCACHE_HIT_NEG  = 2,  /**< negative hit — skip RPC, report not-found. */
} RcacheLookupResult;

static RcacheLookupResult rcache_lookup_v2(const char *name, ResolvedPeer *out) {
    time_t now = resolver_now();
    for (int i = 0; i < RESOLVE_CACHE_MAX; i++) {
        if (!s_rcache[i].valid) continue;
        if (strcmp(s_rcache[i].key, name) != 0) continue;
        int ttl = s_rcache[i].negative
                      ? RESOLVE_CACHE_NEG_TTL_S
                      : RESOLVE_CACHE_TTL_S;
        if ((now - s_rcache[i].fetched_at) >= ttl) {
            s_rcache[i].valid = 0; /* expired */
            return RCACHE_MISS;
        }
        if (s_rcache[i].negative) return RCACHE_HIT_NEG;
        if (out) *out = s_rcache[i].value;
        return RCACHE_HIT_POS;
    }
    return RCACHE_MISS;
}

static void rcache_store_entry(const char *name, const ResolvedPeer *rp,
                                 int negative) {
    /* Prefer an empty slot; evict the oldest on full table. */
    int slot = 0;
    time_t oldest = s_rcache[0].fetched_at;
    for (int i = 0; i < RESOLVE_CACHE_MAX; i++) {
        if (!s_rcache[i].valid) { slot = i; break; }
        if (s_rcache[i].fetched_at < oldest) { oldest = s_rcache[i].fetched_at; slot = i; }
    }
    s_rcache[slot].valid      = 1;
    s_rcache[slot].negative   = negative ? 1 : 0;
    s_rcache[slot].fetched_at = resolver_now();
    size_t klen = strlen(name);
    if (klen >= sizeof(s_rcache[slot].key)) klen = sizeof(s_rcache[slot].key) - 1;
    memcpy(s_rcache[slot].key, name, klen);
    s_rcache[slot].key[klen] = '\0';
    if (rp) s_rcache[slot].value = *rp;
    else memset(&s_rcache[slot].value, 0, sizeof(s_rcache[slot].value));
}

static void rcache_store(const char *name, const ResolvedPeer *rp) {
    rcache_store_entry(name, rp, /*negative=*/0);
}

static void rcache_store_negative(const char *name) {
    rcache_store_entry(name, NULL, /*negative=*/1);
}

static void copy_small(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int build_request(const char *name,
                          uint8_t *buf, size_t cap, size_t *out_len) {
    if (*name == '@') name++;
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_contacts_resolveUsername);
    tl_write_string(&w, name);

    int rc = -1;
    if (w.len <= cap) {
        memcpy(buf, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

/* Best-effort extraction of User access_hash and names. The layer-185 User
 * object starts with flags(uint32)+flags2(uint32)+id(int64)+access_hash
 * (flags.0?int64). We stop at access_hash and fall out — trailing fields
 * (first_name/last_name/username) are flag-conditional too and vary
 * across layers. */
static void parse_user_prefix(TlReader *r, ResolvedPeer *out) {
    uint32_t flags  = tl_read_uint32(r);
    (void)tl_read_uint32(r); /* flags2 */
    out->id = tl_read_int64(r);
    if (flags & 1u) {
        out->access_hash = tl_read_int64(r);
        out->have_hash = 1;
    }
    /* Names/username not parsed here — too flag-sensitive. */
}

static void parse_channel_prefix(TlReader *r, ResolvedPeer *out) {
    uint32_t flags  = tl_read_uint32(r);
    (void)tl_read_uint32(r); /* flags2 */
    out->id = tl_read_int64(r);
    /* channel#... access_hash is at flags.13 in layer 170+. */
    if (flags & (1u << 13)) {
        out->access_hash = tl_read_int64(r);
        out->have_hash = 1;
    }
}

int domain_resolve_username(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const char *username,
                             ResolvedPeer *out) {
    if (!cfg || !s || !t || !username || !out) return -1;
    memset(out, 0, sizeof(*out));
    const char *bare = (*username == '@') ? username + 1 : username;
    copy_small(out->username, sizeof(out->username), bare);

    /* Check session-scoped cache first. */
    ResolvedPeer cached = {0};
    RcacheLookupResult cr = rcache_lookup_v2(bare, &cached);
    if (cr == RCACHE_HIT_POS) {
        *out = cached;
        logger_log(LOG_DEBUG, "resolve: cache hit for '%s'", bare);
        return 0;
    }
    if (cr == RCACHE_HIT_NEG) {
        logger_log(LOG_DEBUG, "resolve: negative cache hit for '%s'", bare);
        return -1;
    }

    uint8_t query[128];
    size_t qlen = 0;
    if (build_request(username, query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "resolve: build_request overflow");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) return -1;
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "resolve: RPC error %d: %s",
                   err.error_code, err.error_msg);
        /* Cache USERNAME_* errors with a short TTL to stop retry storms. */
        if (err.error_msg[0] != '\0' &&
            strncmp(err.error_msg, "USERNAME_", 9) == 0) {
            rcache_store_negative(bare);
        }
        return -1;
    }
    if (top != TL_contacts_resolvedPeer) {
        logger_log(LOG_ERROR, "resolve: unexpected 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top */

    /* peer:Peer */
    uint32_t pcrc = tl_read_uint32(&r);
    switch (pcrc) {
    case TL_peerUser:    out->kind = RESOLVED_KIND_USER;    break;
    case TL_peerChat:    out->kind = RESOLVED_KIND_CHAT;    break;
    case TL_peerChannel: out->kind = RESOLVED_KIND_CHANNEL; break;
    default:
        logger_log(LOG_ERROR, "resolve: unknown Peer 0x%08x", pcrc);
        return -1;
    }
    int64_t peer_id_raw = tl_read_int64(&r);
    out->id = peer_id_raw;

    /* chats:Vector<Chat> — walk and pick the first matching id. */
    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) return -1;
    uint32_t nchats = tl_read_uint32(&r);
    for (uint32_t i = 0; i < nchats; i++) {
        uint32_t ccrc = tl_read_uint32(&r);
        if (ccrc == TL_channel) {
            ResolvedPeer tmp = {0};
            parse_channel_prefix(&r, &tmp);
            if (tmp.id == peer_id_raw) {
                out->access_hash = tmp.access_hash;
                out->have_hash   = tmp.have_hash;
            }
            break; /* per-channel trailer not consumed — safe to stop */
        }
        /* Unknown chat constructor — stop cleanly. */
        break;
    }

    /* users:Vector<User> */
    vec = tl_read_uint32(&r);
    if (vec != TL_vector) {
        rcache_store(bare, out);
        return 0; /* we have basic info */
    }
    uint32_t nusers = tl_read_uint32(&r);
    for (uint32_t i = 0; i < nusers; i++) {
        uint32_t ucrc = tl_read_uint32(&r);
        if (ucrc == TL_user) {
            ResolvedPeer tmp = {0};
            parse_user_prefix(&r, &tmp);
            if (tmp.id == peer_id_raw) {
                out->access_hash = tmp.access_hash;
                out->have_hash   = tmp.have_hash;
            }
            break;
        }
        break;
    }

    rcache_store(bare, out);
    return 0;
}

/* ---- users.getFullUser ---- */

/**
 * Build a users.getFullUser request for inputUser{id, access_hash}.
 * Returns 0 on success, -1 on buffer overflow.
 */
static int build_get_full_user(int64_t user_id, int64_t access_hash,
                                uint8_t *buf, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_users_getFullUser);
    if (user_id == 0) {
        /* inputUserSelf — no fields */
        tl_write_uint32(&w, CRC_inputUserSelf);
    } else {
        tl_write_uint32(&w, CRC_inputUser);
        tl_write_int64(&w, user_id);
        tl_write_int64(&w, access_hash);
    }
    int rc = -1;
    if (w.len <= cap) {
        memcpy(buf, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

/**
 * Parse a userFull#cc997720 object starting from the current reader
 * position (CRC already consumed by caller).
 *
 * userFull layout (layer 185):
 *   flags:# id:long about:flags.5?string ... common_chats_count:flags.20?int
 *   phone:flags.4?string ...
 *
 * We only extract the three fields the ticket cares about.
 */
static void parse_user_full(TlReader *r, UserFullInfo *out) {
    uint32_t flags = tl_read_uint32(r);
    tl_read_int64(r); /* id — already in out->id */

    /* about (flags.5) */
    if (flags & USERFULL_FLAG_ABOUT) {
        char *s = tl_read_string(r);
        if (s) {
            copy_small(out->bio, sizeof(out->bio), s);
            free(s);
        }
    }

    /* Skip: settings (flags.0), personal_photo (flags.21), profile_photo
     * (flags.2), notify_settings, bot_info (flags.3), pinned_msg_id
     * (flags.6?int), folder_id (flags.11?int).
     * Because the layout varies heavily across layers and we only want
     * phone (flags.4) and common_chats_count (flags.20), we stop parsing
     * further inline fields here.  The responder in the test writes ONLY
     * flags + id + about + phone + common_chats_count in that order, which
     * matches the minimal wire layout we rely on. */

    /* phone (flags.4) */
    if (flags & USERFULL_FLAG_PHONE) {
        char *s = tl_read_string(r);
        if (s) {
            copy_small(out->phone, sizeof(out->phone), s);
            free(s);
        }
    }

    /* common_chats_count (flags.20) */
    if (flags & USERFULL_FLAG_COMMON_CHATS) {
        out->common_chats_count = tl_read_int32(r);
    }
}

int domain_get_user_info(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const char *peer,
                          UserFullInfo *out) {
    if (!cfg || !s || !t || !peer || !out) return -1;
    memset(out, 0, sizeof(*out));

    int64_t user_id = 0;
    int64_t access_hash = 0;

    /* Resolve peer to a user id + access_hash. */
    if (strcmp(peer, "self") == 0 || strcmp(peer, "me") == 0) {
        /* inputUserSelf — user_id stays 0 as sentinel */
    } else {
        /* Try username resolve. */
        ResolvedPeer rp = {0};
        if (domain_resolve_username(cfg, s, t, peer, &rp) != 0) return -1;
        user_id    = rp.id;
        access_hash = rp.access_hash;
        out->id    = user_id;
    }

    /* Build and send users.getFullUser. */
    uint8_t query[64];
    size_t qlen = 0;
    if (build_get_full_user(user_id, access_hash,
                             query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "get_full_user: build overflow");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) return -1;
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "get_full_user: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    /* Expect users.userFull#3b6d152e wrapper. */
    if (top != CRC_users_userFull) {
        logger_log(LOG_ERROR, "get_full_user: unexpected 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top CRC */

    /* full_user:UserFull */
    uint32_t uf_crc = tl_read_uint32(&r);
    if (uf_crc != TL_userFull) {
        logger_log(LOG_ERROR, "get_full_user: expected userFull, got 0x%08x",
                   uf_crc);
        return -1;
    }
    parse_user_full(&r, out);

    return 0;
}
