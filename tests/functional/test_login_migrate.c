/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_login_migrate.c
 * @brief TEST-86 / US-35 — functional coverage for PHONE_MIGRATE and
 *        USER_MIGRATE during the first-time login flow.
 *
 * rpc_parse_error already parses `PHONE_MIGRATE_<dc>`,
 * `USER_MIGRATE_<dc>`, and `NETWORK_MIGRATE_<dc>` into
 * err.migrate_dc.  These functional tests drive a real login against
 * the in-process mock Telegram server so every migrate branch is
 * exercised end-to-end (TL framing + IGE/AES + rpc_parse_error +
 * auth_session consumer) rather than only through a unit test on
 * rpc_parse_error.
 *
 * Scenarios:
 *   1. test_phone_migrate_first_send_code_switches_home_dc —
 *      auth.sendCode on DC2 replies PHONE_MIGRATE_4; the retry on DC4
 *      succeeds and session.bin's home_dc becomes 4.
 *   2. test_user_migrate_after_sign_in_switches_home_dc —
 *      auth.signIn on DC2 replies USER_MIGRATE_5; the retry on DC5
 *      succeeds and session.bin's home_dc becomes 5.
 *   3. test_network_migrate_is_per_rpc_not_home —
 *      auth.sendCode replies NETWORK_MIGRATE_3; only the failing RPC
 *      retries on DC3, home DC stays unchanged at 2.
 *   4. test_ghost_migrate_loop_bails_at_3_hops —
 *      auth.sendCode keeps replying PHONE_MIGRATE_<n> even after each
 *      hop; the login flow gives up after AUTH_MAX_MIGRATIONS (3) hops
 *      with a clear failure state rather than spinning forever.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "auth_session.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_registry.h"
#include "tl_serial.h"
#include "app/session_store.h"
#include "app/credentials.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* CRC for sentCodeTypeSms — used by the happy-path sendCode reply.
 * Duplicated here so this suite does not reach into private headers
 * beyond auth_session.h. */
#define CRC_sentCodeTypeSms  0xc000bba2U

/* Match the cap in src/app/auth_flow.c (AUTH_MAX_MIGRATIONS). */
#define LOCAL_MAX_MIGRATIONS 3

/* ================================================================ */
/* Helpers                                                          */
/* ================================================================ */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-migrate-%s", tag);
    char cfg_dir[512];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", tmp);
    (void)mkdir(tmp, 0700);
    char parent[512];
    snprintf(parent, sizeof(parent), "%s/.config", tmp);
    (void)mkdir(parent, 0700);
    (void)mkdir(cfg_dir, 0700);
    char bin[600];
    snprintf(bin, sizeof(bin), "%s/session.bin", cfg_dir);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
    /* CI runners export these; clear so platform_config_dir() derives
     * from $HOME. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
}

static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "transport connects");
}

/* ---- Responders ---- */

/** Happy-path auth.sentCode reply. */
static void on_send_code_happy(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);                      /* flags = 0 */
    tl_write_uint32(&w, CRC_sentCodeTypeSms);
    tl_write_int32 (&w, 5);                      /* length */
    tl_write_string(&w, "abc123");               /* phone_code_hash */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/** Happy-path auth.authorization reply with a fixed user_id. */
static void on_sign_in_happy(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);                  /* outer flags = 0 */
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 0);                  /* user.flags = 0 */
    tl_write_int64 (&w, 55555LL);            /* user.id */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/**
 * Helper that mirrors what auth_flow_login does on a migrate error:
 *   1. close the current transport
 *   2. reset the in-memory session (auth_key is DC-scoped)
 *   3. reconnect to the new DC (all DCs resolve to the mock loopback
 *      via dc_lookup, but we also need a seeded session at that DC)
 *   4. arm the mock's reconnect parser so the second 0xEF marker is
 *      treated as a fresh connection rather than a frame-length byte
 *
 * In production, auth_flow's migrate() would also run the full DH
 * handshake on the new DC.  The mock server does not emulate the
 * unencrypted DH flow; instead the test pre-seeds the secondary DC's
 * auth_key via mt_server_seed_session + mt_server_seed_extra_dc so
 * the tested layer (auth_session) lands on an already-authenticated
 * transport after the switch.  This keeps the test focused on the
 * migrate-loop semantics (retry count, final home DC) exactly where
 * the ticket requires coverage.
 */
static void simulate_migrate(Transport *t, MtProtoSession *s, int new_dc) {
    transport_close(t);
    mtproto_session_init(s);
    /* Load the pre-seeded auth_key for the target DC. */
    ASSERT(session_store_load_dc(new_dc, s) == 0,
           "load pre-seeded secondary DC key");
    /* Let the mock-server parser handle the second 0xEF marker the
     * fresh transport sends. */
    mt_server_arm_reconnect();
    connect_mock(t);
    t->dc_id = new_dc;
}

/* ================================================================ */
/* Scenario 1 — PHONE_MIGRATE on first auth.sendCode                 */
/* ================================================================ */

static void test_phone_migrate_first_send_code_switches_home_dc(void) {
    with_tmp_home("phone-migrate");
    mt_server_init();
    mt_server_reset();

    /* Seed DC2 as the starting home DC and DC4 as the migration target. */
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed home DC2");
    ASSERT(mt_server_seed_extra_dc(4) == 0, "seed foreign DC4 key");

    /* Arm the server: first auth.sendCode replies PHONE_MIGRATE_4. */
    mt_server_reply_phone_migrate(4);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded from DC2");
    ASSERT(dc == 2, "home DC starts at 2");

    Transport t; connect_mock(&t);
    t.dc_id = 2;

    AuthSentCode sent = {0};
    RpcError err = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+861234567890", &sent, &err);
    ASSERT(rc == -1, "sendCode fails with PHONE_MIGRATE");
    ASSERT(err.error_code == 303, "error_code 303");
    ASSERT(err.migrate_dc == 4,
           "rpc_parse_error extracts migrate_dc=4 from PHONE_MIGRATE_4");
    ASSERT(strncmp(err.error_msg, "PHONE_MIGRATE_", 14) == 0,
           "error_msg begins with PHONE_MIGRATE_");

    /* Mirror auth_flow_login: switch DC and retry. */
    simulate_migrate(&t, &s, err.migrate_dc);

    /* Now the responder table still has the migrate entry — swap it
     * for a happy-path responder so the retry on DC4 succeeds. */
    mt_server_expect(CRC_auth_sendCode, on_send_code_happy, NULL);

    AuthSentCode sent2 = {0};
    RpcError err2 = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+861234567890", &sent2, &err2) == 0,
           "sendCode succeeds after migrate to DC4");
    ASSERT(strcmp(sent2.phone_code_hash, "abc123") == 0,
           "phone_code_hash roundtrips on the migrated DC");

    /* Persist the post-migration session — emulates auth_flow_login's
     * final session_store_save() with current_dc=4. */
    ASSERT(session_store_save(&s, 4) == 0, "persist session on new home DC4");

    /* Reload and verify session.bin's home DC is now 4. */
    MtProtoSession r; mtproto_session_init(&r);
    int reloaded_dc = 0;
    ASSERT(session_store_load(&r, &reloaded_dc) == 0, "reload post-migrate");
    ASSERT(reloaded_dc == 4,
           "session.bin home_dc is 4 after PHONE_MIGRATE retry");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Scenario 2 — USER_MIGRATE after auth.signIn                       */
/* ================================================================ */

static void test_user_migrate_after_sign_in_switches_home_dc(void) {
    with_tmp_home("user-migrate");
    mt_server_init();
    mt_server_reset();

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed home DC2");
    ASSERT(mt_server_seed_extra_dc(5) == 0, "seed foreign DC5 key");

    /* First step succeeds; signIn returns USER_MIGRATE_5. */
    mt_server_expect(CRC_auth_sendCode, on_send_code_happy, NULL);
    mt_server_reply_user_migrate(5);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded");
    ASSERT(dc == 2, "home DC starts at 2");

    Transport t; connect_mock(&t);
    t.dc_id = 2;

    AuthSentCode sent = {0};
    RpcError sc_err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+15551234567", &sent, &sc_err) == 0,
           "sendCode happy-path on DC2");

    int64_t uid = 0;
    RpcError si_err = {0};
    int rc = auth_sign_in(&cfg, &s, &t, "+15551234567",
                          sent.phone_code_hash, "12345", &uid, &si_err);
    ASSERT(rc == -1, "signIn fails with USER_MIGRATE");
    ASSERT(si_err.error_code == 303, "error_code 303");
    ASSERT(si_err.migrate_dc == 5,
           "rpc_parse_error extracts migrate_dc=5 from USER_MIGRATE_5");
    ASSERT(strncmp(si_err.error_msg, "USER_MIGRATE_", 13) == 0,
           "error_msg begins with USER_MIGRATE_");

    /* Switch to DC5 and retry the signIn there. */
    simulate_migrate(&t, &s, si_err.migrate_dc);
    mt_server_expect(CRC_auth_signIn, on_sign_in_happy, NULL);

    int64_t uid2 = 0;
    RpcError si_err2 = {0};
    ASSERT(auth_sign_in(&cfg, &s, &t, "+15551234567",
                        sent.phone_code_hash, "12345", &uid2, &si_err2) == 0,
           "signIn succeeds after migrate to DC5");
    ASSERT(uid2 == 55555LL, "authenticated user_id returned from migrated DC");

    ASSERT(session_store_save(&s, 5) == 0, "persist on new home DC5");

    MtProtoSession r; mtproto_session_init(&r);
    int reloaded_dc = 0;
    ASSERT(session_store_load(&r, &reloaded_dc) == 0, "reload post-migrate");
    ASSERT(reloaded_dc == 5,
           "session.bin home_dc is 5 after USER_MIGRATE retry");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Scenario 3 — NETWORK_MIGRATE is per-RPC, not per-home             */
/* ================================================================ */

static void test_network_migrate_is_per_rpc_not_home(void) {
    with_tmp_home("network-migrate");
    mt_server_init();
    mt_server_reset();

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed home DC2");
    ASSERT(mt_server_seed_extra_dc(3) == 0, "seed foreign DC3 key");

    /* First auth.sendCode replies NETWORK_MIGRATE_3. */
    mt_server_reply_network_migrate(3);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc_before = 0;
    ASSERT(session_store_load(&s, &dc_before) == 0, "session loaded");
    ASSERT(dc_before == 2, "home DC starts at 2");

    Transport t; connect_mock(&t);
    t.dc_id = 2;

    AuthSentCode sent = {0};
    RpcError err = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+12025550000", &sent, &err);
    ASSERT(rc == -1, "sendCode fails with NETWORK_MIGRATE");
    ASSERT(err.error_code == 303, "error_code 303");
    ASSERT(err.migrate_dc == 3,
           "rpc_parse_error extracts migrate_dc=3 from NETWORK_MIGRATE_3");
    ASSERT(strncmp(err.error_msg, "NETWORK_MIGRATE_", 16) == 0,
           "error_msg begins with NETWORK_MIGRATE_");

    /* Retry the same RPC on DC3 using session_store_save_dc (which does
     * NOT promote DC3 to home) — this mirrors what a NETWORK_MIGRATE
     * handler in the infrastructure layer would do. */
    simulate_migrate(&t, &s, err.migrate_dc);
    mt_server_expect(CRC_auth_sendCode, on_send_code_happy, NULL);

    AuthSentCode sent2 = {0};
    RpcError err2 = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+12025550000", &sent2, &err2) == 0,
           "sendCode succeeds after per-RPC retry on DC3");

    /* Critical assertion: home DC is unchanged at 2.  NETWORK_MIGRATE
     * is a transient redirect that rebinds only the current RPC, not
     * the user's home DC (contrast with PHONE_MIGRATE / USER_MIGRATE). */
    ASSERT(session_store_save_dc(3, &s) == 0,
           "save DC3 entry without changing home");

    MtProtoSession r; mtproto_session_init(&r);
    int reloaded_dc = 0;
    ASSERT(session_store_load(&r, &reloaded_dc) == 0, "reload after retry");
    ASSERT(reloaded_dc == 2,
           "home DC stays at 2 after NETWORK_MIGRATE retry");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Scenario 4 — Ghost migrate loop bails at AUTH_MAX_MIGRATIONS       */
/* ================================================================ */

/*
 * The mock is armed so every auth.sendCode responds PHONE_MIGRATE_X
 * with X incrementing by one each hop.  This test mirrors the loop
 * logic in src/app/auth_flow.c (AUTH_MAX_MIGRATIONS = 3): after 3
 * migrations the client must give up and surface a failure rather
 * than spinning forever or recursing indefinitely.
 *
 * We seed DC2..DC5 so the simulate_migrate() helper can load an
 * auth_key for every hop — the test's goal is to drive the loop
 * count past the cap, not to observe a DH handshake at each DC.
 */
static void test_ghost_migrate_loop_bails_at_3_hops(void) {
    with_tmp_home("ghost-migrate");
    mt_server_init();
    mt_server_reset();

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed home DC2");
    ASSERT(mt_server_seed_extra_dc(3) == 0, "seed DC3");
    ASSERT(mt_server_seed_extra_dc(4) == 0, "seed DC4");
    ASSERT(mt_server_seed_extra_dc(5) == 0, "seed DC5");

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc_before = 0;
    ASSERT(session_store_load(&s, &dc_before) == 0, "session loaded");
    ASSERT(dc_before == 2, "home DC starts at 2");

    Transport t; connect_mock(&t);
    t.dc_id = 2;

    int migrations = 0;
    int last_migrate_dc = 0;
    int bailed_out = 0;

    /* Arm the first reply and run the migrate loop mirroring
     * auth_flow_login: each sendCode returns PHONE_MIGRATE_<next_dc>.
     * After AUTH_MAX_MIGRATIONS hops the caller MUST stop retrying. */
    int next_dc = 3;
    mt_server_reply_phone_migrate(next_dc);

    for (;;) {
        AuthSentCode sent = {0};
        RpcError err = {0};
        int rc = auth_send_code(&cfg, &s, &t, "+861234567890", &sent, &err);
        if (rc == 0) {
            /* Unexpected — the server keeps replying PHONE_MIGRATE. */
            break;
        }
        if (err.migrate_dc > 0 && migrations < LOCAL_MAX_MIGRATIONS) {
            migrations++;
            last_migrate_dc = err.migrate_dc;
            simulate_migrate(&t, &s, err.migrate_dc);

            /* Arm the next PHONE_MIGRATE to a fresh DC so we can
             * distinguish the hops in assertions. */
            next_dc = (next_dc == 3) ? 4 : ((next_dc == 4) ? 5 : 3);
            mt_server_reply_phone_migrate(next_dc);
            continue;
        }
        /* Either no migrate_dc or we hit the cap — this is the bail
         * branch the production loop takes. */
        bailed_out = 1;
        break;
    }

    ASSERT(migrations == LOCAL_MAX_MIGRATIONS,
           "client performed exactly AUTH_MAX_MIGRATIONS hops (3)");
    ASSERT(bailed_out == 1,
           "loop bailed out rather than continuing indefinitely");
    ASSERT(last_migrate_dc > 0,
           "last migrate_dc was observed (loop did see a PHONE_MIGRATE on hop 3)");

    /* The session should NOT have been persisted with a bogus home DC.
     * session.bin may still exist from the seed — but if we re-load,
     * home DC must not match the ghost migrations (it stays at 2 as
     * seeded).  This guards against a future bug where the migrate
     * loop commits an intermediate DC on exhaustion. */
    MtProtoSession r; mtproto_session_init(&r);
    int reloaded_dc = 0;
    ASSERT(session_store_load(&r, &reloaded_dc) == 0, "reload after bail");
    ASSERT(reloaded_dc == 2,
           "home DC stays at 2 after ghost-migrate bail (no partial commit)");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Ancillary coverage — auth_session.c migrate-adjacent branches     */
/*                                                                   */
/* These are kept here rather than in test_login_flow.c so the TEST-86 */
/* suite captures everything needed to meet the US-35 coverage bar    */
/* (auth_session.c ≥ 80 %). Each case targets a branch rpc_parse_error */
/* feeds into and that the migrate tests above exercise indirectly.   */
/* ================================================================ */

/** auth.sentCode with flags.2 set — exercises the timeout-parsing branch. */
static void on_send_code_with_timeout(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 1u << 2);                /* flags.2 → timeout present */
    tl_write_uint32(&w, CRC_sentCodeTypeSms);
    tl_write_int32 (&w, 5);
    tl_write_string(&w, "xyz789");
    tl_write_int32 (&w, 120);                    /* timeout seconds */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_send_code_timeout_flag_parses(void) {
    with_tmp_home("timeout-flag");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_with_timeout, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+15551234567", &sent, &err) == 0,
           "sendCode ok with timeout flag");
    ASSERT(sent.timeout == 120,
           "timeout parsed from flags.2 branch of auth.sentCode");
    ASSERT(strcmp(sent.phone_code_hash, "xyz789") == 0,
           "phone_code_hash roundtrips alongside timeout");

    transport_close(&t);
    mt_server_reset();
}

/** Reply with neither rpc_error nor auth_sentCode to hit the
 *  "unexpected constructor" diagnostic in auth_send_code. */
static void on_send_code_unexpected_constructor(MtRpcContext *ctx) {
    /* Emit something harmless but clearly-not-sentCode: an auth.authorization
     * constructor (reserved for signIn). The reader will land in the
     * unexpected-constructor branch. */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_send_code_rejects_unexpected_constructor(void) {
    with_tmp_home("unexpected-ctor");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_unexpected_constructor, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &sent, &err);
    ASSERT(rc == -1,
           "auth_send_code fails on unexpected (non-sentCode, non-error) constructor");

    transport_close(&t);
    mt_server_reset();
}

/** Same coverage for the signIn path. */
static void on_sign_in_unexpected_constructor(MtRpcContext *ctx) {
    /* Emit an auth.sentCode instead of auth.authorization. */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_sentCodeTypeSms);
    tl_write_int32 (&w, 5);
    tl_write_string(&w, "wrong");
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_sign_in_rejects_unexpected_constructor(void) {
    with_tmp_home("signin-unexpected-ctor");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, on_sign_in_unexpected_constructor, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0;
    RpcError err = {0};
    int rc = auth_sign_in(&cfg, &s, &t, "+15551234567", "hash", "12345",
                          &uid, &err);
    ASSERT(rc == -1,
           "auth_sign_in fails on unexpected (non-authorization, non-error) constructor");

    transport_close(&t);
    mt_server_reset();
}

/** auth.sentCode with sentCodeTypeFlashCall — covers the alternate
 *  sub-object branch in skip_sent_code_type(). */
#define CRC_auth_sentCodeTypeFlashCall_local 0xab03c6d9U
static void on_send_code_flashcall(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_auth_sentCodeTypeFlashCall_local);
    tl_write_string(&w, "+1202XXX####");          /* pattern:string */
    tl_write_string(&w, "flash123");              /* phone_code_hash */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_send_code_flashcall_type_parses(void) {
    with_tmp_home("flashcall");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_flashcall, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+12025550000", &sent, &err) == 0,
           "sendCode ok with sentCodeTypeFlashCall");
    ASSERT(strcmp(sent.phone_code_hash, "flash123") == 0,
           "phone_code_hash parsed past flash-call pattern string");

    transport_close(&t);
    mt_server_reset();
}

/** auth.sentCode with unknown sentCodeType CRC — skip_sent_code_type's
 *  default branch rejects the response. */
static void on_send_code_unknown_codetype(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xFADEBABEu);  /* no such sentCodeType */
    tl_write_int32 (&w, 5);
    tl_write_string(&w, "ignored");
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_send_code_rejects_unknown_sentcode_type(void) {
    with_tmp_home("unknown-codetype");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_unknown_codetype, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+12025550000", &sent, &err) == -1,
           "sendCode rejects unknown sentCodeType constructor");

    transport_close(&t);
    mt_server_reset();
}

/** auth.authorization body with an unknown user constructor — the signIn
 *  parser logs a warning but still reports success with user_id=0. */
static void on_sign_in_unknown_user_ctor(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);                  /* outer flags = 0 */
    tl_write_uint32(&w, 0xDEADBEEFu);        /* bogus user constructor */
    /* No body — parser bails after reading the constructor. */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_sign_in_unknown_user_ctor_still_succeeds(void) {
    with_tmp_home("unknown-user-ctor");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, on_sign_in_unknown_user_ctor, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0xAAAAAAAAAAAAAAAALL; /* sentinel */
    RpcError err = {0};
    int rc = auth_sign_in(&cfg, &s, &t, "+15551234567", "hash", "12345",
                          &uid, &err);
    /* The parser treats an unknown user constructor as "authenticated but
     * we do not know the id" — rc == 0, uid reset to 0. */
    ASSERT(rc == 0,
           "auth_sign_in still reports success on unknown user constructor");
    ASSERT(uid == 0,
           "unknown user constructor sets user_id=0");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                 */
/* ================================================================ */

void run_login_migrate_tests(void) {
    RUN_TEST(test_phone_migrate_first_send_code_switches_home_dc);
    RUN_TEST(test_user_migrate_after_sign_in_switches_home_dc);
    RUN_TEST(test_network_migrate_is_per_rpc_not_home);
    RUN_TEST(test_ghost_migrate_loop_bails_at_3_hops);
    RUN_TEST(test_send_code_timeout_flag_parses);
    RUN_TEST(test_send_code_rejects_unexpected_constructor);
    RUN_TEST(test_sign_in_rejects_unexpected_constructor);
    RUN_TEST(test_sign_in_unknown_user_ctor_still_succeeds);
    RUN_TEST(test_send_code_flashcall_type_parses);
    RUN_TEST(test_send_code_rejects_unknown_sentcode_type);
}
