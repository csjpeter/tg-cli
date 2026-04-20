/**
 * @file test_resolver_cache.c
 * @brief TEST-85 — functional coverage for the @username resolver cache.
 *
 * Drives src/domain/read/user_info.c::domain_resolve_username through the
 * mock Telegram server and verifies:
 *
 *   1. Cold call — first resolve of a @peer issues exactly one
 *      contacts.resolveUsername RPC.
 *   2. Warm call — a second call for the same @peer within the positive
 *      TTL is served from the in-process cache (zero new RPC).
 *   3. Different peer — a separate @peer adds one fresh RPC.
 *   4. TTL expiry — advancing the injected clock past the positive TTL
 *      causes the next call to re-fire the RPC.
 *   5. Logout flush — the logout path (auth_logout with the registered
 *      flush callback) invalidates all cached entries so the next call
 *      fires the RPC again.
 *   6. Negative caching — USERNAME_NOT_OCCUPIED / USERNAME_INVALID
 *      responses are remembered under a shorter TTL; a second call
 *      within that TTL does NOT re-fire the RPC, but after the negative
 *      TTL elapses the RPC is re-issued.
 *   7. Eviction — filling the cache past its capacity evicts the oldest
 *      entry, so that re-resolving the first-inserted @peer fires a
 *      fresh RPC while the newer entries stay cached.
 *
 * Timing is controlled by resolve_cache_set_now_fn() (a compile-time
 * seam in user_info.c), so no real sleeps are required.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_registry.h"
#include "tl_serial.h"
#include "domain/read/user_info.h"
#include "infrastructure/auth_logout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ---- CRCs (not surfaced by public headers) ---- */
#define CRC_contacts_resolveUsername  0xf93ccba3U

/* ---- Injected clock for resolver-cache TTL ---- */

static time_t s_fake_time = 0;

static time_t fake_now(void) { return s_fake_time; }

/* ---- Helpers ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-rcache-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
    /* CI runners (GitHub Actions) may export XDG_{CONFIG,CACHE}_HOME,
     * which makes platform_*_dir() ignore our redirected HOME. Force the
     * HOME-based fallback so production code and these tests agree. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
}

static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id   = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load session");
}

/* ---- Responders ---- */

/** contacts.resolvedPeer → user id 8001 with access_hash. */
static void on_resolve_user_8001(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 8001LL);
    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* users vector: one user with access_hash */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 1u);                    /* flags.0 → access_hash */
    tl_write_uint32(&w, 0);                     /* flags2 */
    tl_write_int64 (&w, 8001LL);
    tl_write_int64 (&w, 0xDEADBEEFCAFEBABEULL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/** contacts.resolvedPeer → user id 8002 (distinct @peer). */
static void on_resolve_user_8002(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 8002LL);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 1u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 8002LL);
    tl_write_int64 (&w, 0x1122334455667788ULL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/** Parametric responder for eviction / capacity stress — walks the key
 *  inside the incoming request to derive a unique id. */
static void on_resolve_capture_id(MtRpcContext *ctx) {
    /* Request layout after the CRC: string @username (tl_read_string).
     * Strings under 254 bytes begin with a 1-byte length prefix. We use
     * keys like "p0"..."p31" so the short-form always applies. */
    const uint8_t *p = ctx->req_body + 4;
    size_t n = (size_t)p[0];
    int64_t id = 7000 + (int64_t)atoi((const char *)(p + 2));
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, id);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 1u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, id);
    tl_write_int64 (&w, (int64_t)(0xA000000000000000ULL | (uint64_t)id));
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    (void)n;
}

/** Error path: USERNAME_NOT_OCCUPIED. */
static void on_resolve_not_occupied(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "USERNAME_NOT_OCCUPIED");
}

/** auth.loggedOut#c3a2835f flags=0 — canonical happy-path logout reply. */
static void on_logout_ok(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_loggedOut);
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * @brief First `info @foo` must issue exactly one resolveUsername RPC.
 */
static void test_cold_call_resolves_once(void) {
    with_tmp_home("cold");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 1000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp) == 0,
           "cold resolve ok");
    ASSERT(rp.id == 8001LL, "peer id matches mock reply");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "exactly one resolveUsername RPC was sent");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief Second `info @foo` within the positive TTL must be cache-served.
 */
static void test_warm_call_skips_rpc(void) {
    with_tmp_home("warm");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 2000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp1 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp1) == 0,
           "1st resolve ok");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "cold path → 1 RPC");

    /* +10 s — still well within the positive TTL. */
    s_fake_time += 10;

    ResolvedPeer rp2 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp2) == 0,
           "2nd resolve ok (from cache)");
    ASSERT(rp2.id == rp1.id, "cached id matches");
    ASSERT(rp2.access_hash == rp1.access_hash, "cached access_hash matches");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "warm path added 0 RPCs");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief A different @peer must still issue its own RPC, not reuse the
 *        previous cache entry.
 */
static void test_different_peer_does_not_hit_cache(void) {
    with_tmp_home("diff");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 3000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    /* Fill cache with @foo → 8001. */
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);
    ResolvedPeer rp_foo = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp_foo) == 0,
           "resolve @foo ok");
    ASSERT(rp_foo.id == 8001LL, "@foo id == 8001");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "after @foo → 1 RPC");

    /* Swap responder: @bar → 8002 distinct id. */
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8002, NULL);
    ResolvedPeer rp_bar = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@bar", &rp_bar) == 0,
           "resolve @bar ok");
    ASSERT(rp_bar.id == 8002LL, "@bar id == 8002 (not from @foo cache)");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 2,
           "second @peer fires a separate RPC");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief After the positive TTL lapses the next call must fire a fresh
 *        RPC and refresh the cache.
 */
static void test_ttl_expiry_refreshes(void) {
    with_tmp_home("ttl");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 4000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp) == 0,
           "cold resolve ok");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "after cold → 1 RPC");

    /* Fast-forward past the positive TTL (plus a safety margin). */
    s_fake_time += resolve_cache_positive_ttl() + 1;

    ResolvedPeer rp2 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp2) == 0,
           "refresh resolve ok");
    ASSERT(rp2.id == 8001LL, "refreshed id matches mock reply");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 2,
           "TTL expiry triggers second RPC");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief auth_logout() must invoke the registered cache-flush callback so
 *        that a follow-up resolve re-hits the server.
 */
static void test_logout_flushes_cache(void) {
    with_tmp_home("logout");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 5000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    /* Prime the cache. */
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp) == 0,
           "cold resolve ok");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "after cold → 1 RPC");

    /* Register the production flush callback and arm a happy-path
     * auth.logOut responder so auth_logout() returns cleanly. */
    auth_logout_set_cache_flush_cb(resolve_cache_flush);
    mt_server_expect(CRC_auth_logOut, on_logout_ok, NULL);

    auth_logout(&cfg, &s, &t);

    /* Swap the resolver responder back in for the post-logout call. */
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user_8001, NULL);

    /* The cache must be empty now — the next resolve must fire an RPC. */
    int rpc_before = mt_server_request_crc_count(CRC_contacts_resolveUsername);
    ResolvedPeer rp2 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp2) == 0,
           "post-logout resolve ok");
    int rpc_after = mt_server_request_crc_count(CRC_contacts_resolveUsername);
    ASSERT(rpc_after == rpc_before + 1,
           "post-logout resolve issued a fresh RPC");

    /* Unregister the callback so later tests start clean. */
    auth_logout_set_cache_flush_cb(NULL);

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief USERNAME_NOT_OCCUPIED must be remembered: a repeat call inside
 *        the negative TTL must not re-fire the RPC; after the negative
 *        TTL elapses the RPC fires once more.
 */
static void test_negative_result_cached_with_shorter_ttl(void) {
    with_tmp_home("neg");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 6000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_not_occupied, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    /* First miss: fires one RPC, caches the negative result. */
    ResolvedPeer rp1 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@nobody", &rp1) == -1,
           "1st resolve returns not-found");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "negative cold path → 1 RPC");

    /* Second call within the negative TTL: still fails, NO new RPC. */
    s_fake_time += 5; /* +5 s, inside the negative TTL */
    ResolvedPeer rp2 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@nobody", &rp2) == -1,
           "2nd resolve (neg-cached) returns not-found");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 1,
           "negative cache suppressed 2nd RPC");

    /* Sanity: the positive TTL is strictly greater than the negative
     * TTL — otherwise the whole "shorter" premise breaks. */
    ASSERT(resolve_cache_positive_ttl() > resolve_cache_negative_ttl(),
           "positive TTL > negative TTL by construction");

    /* Fast-forward past the negative TTL: RPC must fire again. */
    s_fake_time += resolve_cache_negative_ttl() + 1;
    ResolvedPeer rp3 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@nobody", &rp3) == -1,
           "3rd resolve after neg-TTL still not-found");
    ASSERT(mt_server_request_crc_count(CRC_contacts_resolveUsername) == 2,
           "neg-TTL expiry triggers second RPC");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief Fill the cache past its fixed capacity; the first-inserted
 *        entry must be evicted.  After eviction re-resolving the first
 *        @peer fires a fresh RPC, while one of the more recently
 *        inserted entries stays cached (zero new RPC).
 */
static void test_cache_eviction_oldest_first(void) {
    with_tmp_home("evict");
    mt_server_init(); mt_server_reset();
    resolve_cache_set_now_fn(fake_now);
    s_fake_time = 10000;
    resolve_cache_flush();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_capture_id, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    int cap = resolve_cache_capacity();
    ASSERT(cap >= 4, "cache capacity is non-trivial");

    /* Insert cap+1 distinct entries, advancing the clock so each entry
     * has a unique fetched_at (the eviction policy is oldest-first). */
    for (int i = 0; i <= cap; i++) {
        char key[16];
        snprintf(key, sizeof(key), "@p%d", i);
        ResolvedPeer rp = {0};
        ASSERT(domain_resolve_username(&cfg, &s, &t, key, &rp) == 0,
               "insert resolve ok");
        s_fake_time += 1;   /* spread fetched_at so eviction order is deterministic */
    }

    int rpc_after_fill =
        mt_server_request_crc_count(CRC_contacts_resolveUsername);
    ASSERT(rpc_after_fill == cap + 1,
           "cap+1 distinct peers fired cap+1 RPCs");

    /* The first inserted entry (@p0) must have been evicted → resolving
     * it again fires a fresh RPC. */
    ResolvedPeer rp_first = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@p0", &rp_first) == 0,
           "re-resolve evicted entry ok");
    int rpc_after_re_first =
        mt_server_request_crc_count(CRC_contacts_resolveUsername);
    ASSERT(rpc_after_re_first == rpc_after_fill + 1,
           "re-resolving evicted @p0 fires RPC");

    /* A recently-inserted entry (@p<cap>) must still be cached → zero
     * new RPC. */
    char last_key[16];
    snprintf(last_key, sizeof(last_key), "@p%d", cap);
    ResolvedPeer rp_last = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, last_key, &rp_last) == 0,
           "re-resolve recent entry ok");
    int rpc_after_re_last =
        mt_server_request_crc_count(CRC_contacts_resolveUsername);
    ASSERT(rpc_after_re_last == rpc_after_re_first,
           "recent entry still cached — no new RPC");

    resolve_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/* ---- Suite entry ---- */

void run_resolver_cache_tests(void) {
    RUN_TEST(test_cold_call_resolves_once);
    RUN_TEST(test_warm_call_skips_rpc);
    RUN_TEST(test_different_peer_does_not_hit_cache);
    RUN_TEST(test_ttl_expiry_refreshes);
    RUN_TEST(test_logout_flushes_cache);
    RUN_TEST(test_negative_result_cached_with_shorter_ttl);
    RUN_TEST(test_cache_eviction_oldest_first);
}
