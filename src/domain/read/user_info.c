/**
 * @file domain/read/user_info.c
 * @brief contacts.resolveUsername minimal parser.
 */

#include "domain/read/user_info.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_contacts_resolveUsername 0xf93ccba3u

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
    copy_small(out->username, sizeof(out->username),
               *username == '@' ? username + 1 : username);

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
    if (vec != TL_vector) return 0; /* we have basic info */
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

    return 0;
}
