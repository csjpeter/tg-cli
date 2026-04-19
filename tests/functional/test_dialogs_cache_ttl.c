/**
 * @file test_dialogs_cache_ttl.c
 * @brief TEST-04 — dialogs cache TTL functional tests.
 *
 * Verifies that domain_get_dialogs:
 *   1. Serves a second consecutive call from the in-memory cache (no RPC).
 *   2. Issues a fresh RPC once the TTL has expired.
 *
 * The production `DIALOGS_CACHE_TTL_S` is 60 seconds.  Rather than
 * sleeping, we replace the TTL clock with a fake via
 * `dialogs_cache_set_now_fn()` and advance it explicitly.
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
#include "domain/read/dialogs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ---- CRCs ---- */
#define CRC_messages_getDialogs  0xa0f4cb4fU
#define CRC_dialog               0xd58a08c6U
#define CRC_peerNotifySettings   0xa83b0426U

/* ---- Fake clock ---- */

static time_t s_fake_time = 0;

static time_t fake_now(void) { return s_fake_time; }

/* ---- Helpers ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-dlg-ttl-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load session");
}

/* ---- Responder: one user dialog ---- */

static void on_dialogs_one(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);

    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_dialog);
    tl_write_uint32(&w, 0);                  /* flags=0 */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 42LL);               /* peer_id */
    tl_write_int32 (&w, 100);               /* top_message */
    tl_write_int32 (&w, 0);                  /* read_inbox_max_id */
    tl_write_int32 (&w, 0);                  /* read_outbox_max_id */
    tl_write_int32 (&w, 5);                  /* unread_count */
    tl_write_int32 (&w, 0);                  /* unread_mentions_count */
    tl_write_int32 (&w, 0);                  /* unread_reactions_count */
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0);

    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* messages */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * @brief First assert: two consecutive calls within TTL fire exactly ONE RPC.
 *        Second assert: a third call after TTL expiry fires a second RPC.
 */
static void test_dialogs_cache_ttl(void) {
    with_tmp_home("ttl");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    /* Arm the fake clock at t=0. */
    s_fake_time = 1000;
    dialogs_cache_set_now_fn(fake_now);
    dialogs_cache_flush();

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_one, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0;

    /* ---- Call 1: cold cache — must hit the mock server. ---- */
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, NULL) == 0,
           "call-1 ok");
    ASSERT(n == 1,                    "call-1 returns 1 dialog");
    ASSERT(rows[0].peer_id == 42LL,   "call-1 peer_id==42");
    ASSERT(mt_server_rpc_call_count() == 1, "call-1 RPC count == 1");

    /* ---- Call 2: within TTL — must be served from cache, no new RPC. ---- */
    n = 0;
    s_fake_time = 1030; /* +30 s, still within 60-s TTL */
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, NULL) == 0,
           "call-2 ok");
    ASSERT(n == 1,                    "call-2 returns 1 dialog from cache");
    ASSERT(rows[0].peer_id == 42LL,   "call-2 peer_id==42 (from cache)");
    ASSERT(mt_server_rpc_call_count() == 1, "call-2 still only 1 RPC total");

    /* ---- Call 3: advance clock past TTL — must hit the mock server again. ---- */
    n = 0;
    s_fake_time = 1070; /* +70 s from fetch time → TTL expired */
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, NULL) == 0,
           "call-3 ok");
    ASSERT(n == 1,                    "call-3 returns 1 dialog (fresh RPC)");
    ASSERT(rows[0].peer_id == 42LL,   "call-3 peer_id==42");
    ASSERT(mt_server_rpc_call_count() == 2, "call-3 triggers second RPC");

    /* Restore real clock. */
    dialogs_cache_set_now_fn(NULL);

    transport_close(&t);
    mt_server_reset();
}

void run_dialogs_cache_ttl_tests(void) {
    RUN_TEST(test_dialogs_cache_ttl);
}
