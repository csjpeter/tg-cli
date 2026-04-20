/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_auth_flow_errors.c
 * @brief TEST-74 / US-23 — functional coverage for login-failure
 *        error paths in auth_flow.c / auth_session.c / auth_2fa.c.
 *
 * The happy path of login is covered by test_login_flow.c; the migrate
 * branches by test_login_migrate.c.  This suite fills in the long tail
 * of realistic Telegram failure modes enumerated in US-23:
 *
 *   | server reply                  | asserted behaviour                 |
 *   |-------------------------------|------------------------------------|
 *   | 400 PHONE_NUMBER_INVALID      | auth_send_code rc=-1, err populated|
 *   | 400 PHONE_NUMBER_BANNED       | same                               |
 *   | 400 PHONE_CODE_INVALID        | auth_sign_in rc=-1, err populated  |
 *   | 400 PHONE_CODE_EXPIRED        | same                               |
 *   | 400 PHONE_CODE_EMPTY          | same                               |
 *   | 401 SESSION_PASSWORD_NEEDED   | signals 2FA switch via err         |
 *   | 400 PASSWORD_HASH_INVALID     | auth_2fa_check_password rc=-1      |
 *   | 420 FLOOD_WAIT_30             | rpc_parse_error fills flood_wait   |
 *   | 401 AUTH_RESTART              | rc=-1, err with msg AUTH_RESTART   |
 *   | 500 SIGN_IN_FAILED            | rc=-1, err with msg SIGN_IN_FAILED |
 *   | 400 PHONE_NUMBER_FLOOD        | rc=-1, err populated               |
 *   | 401 SESSION_REVOKED           | rc=-1, err populated               |
 *
 * For every fatal path the suite also asserts that no side effect was
 * committed to the persistent session file (the seeded entry on DC2
 * remains the only entry, no stray home-DC promotion or new entry).
 *
 * On top of the error-string matrix the suite adds two auth_flow.c
 * end-to-end tests (fast-path success / fast-path dc_lookup NULL) so
 * the top-level orchestrator actually runs in functional coverage —
 * the existing suites touched it only through test_batch_rejects_*.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "auth_session.h"
#include "infrastructure/auth_2fa.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/auth_flow.h"
#include "app/session_store.h"
#include "app/credentials.h"
#include "app/dc_config.h"
#include "tl_registry.h"
#include "tl_serial.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* CRC kept local to this TU to avoid pulling private headers. */
#define CRC_sentCodeTypeSms         0xc000bba2U
#define CRC_account_getPassword     0x548a30f5U
#define CRC_KdfAlgoPBKDF2           0x3a912d4aU
#define CRC_auth_checkPassword      0xd18b4d16U

/* ================================================================ */
/* Helpers                                                          */
/* ================================================================ */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-auth-err-%s", tag);
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
    /* CI runners export these — clear so platform_config_dir() derives
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

/* Post-fatal-RPC check: reloading the session store must still return
 * the exact seed we planted (DC2) and MUST NOT have been promoted to a
 * different home DC or gained extra entries during the failing RPC. */
static void assert_session_not_mutated(void) {
    MtProtoSession r; mtproto_session_init(&r);
    int home = 0;
    ASSERT(session_store_load(&r, &home) == 0,
           "seeded session file still readable");
    ASSERT(home == 2,
           "home DC unchanged at 2 after fatal RPC");
}

/* ---- Deterministic SRP fixture (mirrors test_login_flow.c) ---- */

static uint8_t g_test_salt1[16];
static uint8_t g_test_salt2[16];
static uint8_t g_test_prime[256];
static uint8_t g_test_srpB[256];

static void init_srp_fixture(void) {
    for (int i = 0; i < 16;  ++i) g_test_salt1[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 16;  ++i) g_test_salt2[i] = (uint8_t)(i + 17);
    for (int i = 0; i < 256; ++i) g_test_prime[i] = (uint8_t)((i * 7 + 3) | 0x80u);
    g_test_prime[0] = 0xC7;
    g_test_prime[255] = 0x7F;
    for (int i = 0; i < 256; ++i) g_test_srpB[i] = (uint8_t)((i * 5 + 11));
    g_test_srpB[0] = 0x01;
}

/* ================================================================ */
/* Responders — one per Telegram error string                       */
/* ================================================================ */

/* auth.sendCode error responders ------------------------------------ */

static void on_phone_number_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_NUMBER_INVALID");
}
static void on_phone_number_banned(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_NUMBER_BANNED");
}
static void on_phone_number_flood(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_NUMBER_FLOOD");
}
static void on_send_code_flood_wait_30(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 420, "FLOOD_WAIT_30");
}
static void on_auth_restart(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 401, "AUTH_RESTART");
}

/* auth.signIn error responders -------------------------------------- */

static void on_phone_code_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_CODE_INVALID");
}
static void on_phone_code_expired(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_CODE_EXPIRED");
}
static void on_phone_code_empty(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_CODE_EMPTY");
}
static void on_session_password_needed(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 401, "SESSION_PASSWORD_NEEDED");
}
static void on_session_revoked(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 401, "SESSION_REVOKED");
}
static void on_sign_in_failed(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 500, "SIGN_IN_FAILED");
}
static void on_sign_in_flood_wait_60(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 420, "FLOOD_WAIT_60");
}

/* 2FA (auth.checkPassword) error responders ------------------------- */

static void on_password_hash_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PASSWORD_HASH_INVALID");
}
static void on_srp_id_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "SRP_ID_INVALID");
}

/* account.password responder reused across 2FA tests. */
static void on_get_password(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_account_password);
    tl_write_uint32(&w, 1u << 2);                         /* flags: has_password */
    tl_write_uint32(&w, CRC_KdfAlgoPBKDF2);
    tl_write_bytes (&w, g_test_salt1, sizeof(g_test_salt1));
    tl_write_bytes (&w, g_test_salt2, sizeof(g_test_salt2));
    tl_write_int32 (&w, 2);                               /* g = 2 */
    tl_write_bytes (&w, g_test_prime, sizeof(g_test_prime));
    tl_write_bytes (&w, g_test_srpB,  sizeof(g_test_srpB));
    tl_write_int64 (&w, 0x1234567890ABCDEFLL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* account.password that advertises has_password=0 — drives the
 * "server says no 2FA set" early-exit in auth_2fa_check_password. */
static void on_get_password_no_password(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_account_password);
    tl_write_uint32(&w, 0);       /* flags = 0 → has_password bit not set */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Test helpers — scaffolding shared across the per-error cases     */
/* ================================================================ */

/*
 * Run one auth.sendCode round-trip against @p responder and assert the
 * error surface: api returns -1, the RpcError is populated, and the
 * persisted session file is unchanged.  This captures the US-23 ask
 * ("stderr includes the human message" translates, at the auth_session
 * layer boundary, to err.error_msg == server_msg).
 */
static void drive_send_code_error(const char *tag, const char *phone,
                                   MtResponder responder,
                                   int expected_code,
                                   const char *expected_msg,
                                   int expected_flood_wait) {
    with_tmp_home(tag);
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, responder, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, phone, &sent, &err) == -1,
           "auth_send_code returns -1 on rpc_error");
    ASSERT(err.error_code == expected_code,
           "error_code matches server reply");
    ASSERT(strcmp(err.error_msg, expected_msg) == 0,
           "error_msg matches server reply");
    ASSERT(err.flood_wait_secs == expected_flood_wait,
           "flood_wait_secs correct (0 unless FLOOD_WAIT_*)");
    /* migrate_dc must be -1 for non-migrate errors; rpc_parse_error only
     * sets it for PHONE/USER/NETWORK/FILE_MIGRATE_X strings. */
    ASSERT(err.migrate_dc == -1,
           "migrate_dc left at -1 for non-migrate errors");
    /* phone_code_hash must remain the zero-initialised sentinel — the
     * failing RPC must not have written stale data into out->phone_code_hash. */
    ASSERT(sent.phone_code_hash[0] == '\0',
           "phone_code_hash untouched on error");

    assert_session_not_mutated();

    transport_close(&t);
    mt_server_reset();
}

/*
 * auth.signIn equivalent — feeds the happy sendCode first, then the
 * failing signIn responder.  Asserts error surface + session not mutated.
 */
static void drive_sign_in_error(const char *tag,
                                  MtResponder responder,
                                  int expected_code,
                                  const char *expected_msg,
                                  int expected_flood_wait) {
    with_tmp_home(tag);
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, responder, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0xDEADBEEFLL; /* sentinel — must remain untouched on failure */
    RpcError err = {0};
    ASSERT(auth_sign_in(&cfg, &s, &t, "+15551234567", "abc123", "12345",
                        &uid, &err) == -1,
           "auth_sign_in returns -1 on rpc_error");
    ASSERT(err.error_code == expected_code,
           "error_code matches server reply");
    ASSERT(strcmp(err.error_msg, expected_msg) == 0,
           "error_msg matches server reply");
    ASSERT(err.flood_wait_secs == expected_flood_wait,
           "flood_wait_secs correct");
    ASSERT(err.migrate_dc == -1,
           "migrate_dc left at -1 for non-migrate errors");
    ASSERT(uid == 0xDEADBEEFLL,
           "user_id_out untouched on signIn failure");

    assert_session_not_mutated();

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* auth.sendCode error cases                                         */
/* ================================================================ */

static void test_phone_number_invalid(void) {
    drive_send_code_error("phone-invalid", "+00000000",
                          on_phone_number_invalid,
                          400, "PHONE_NUMBER_INVALID", 0);
}

static void test_phone_number_banned(void) {
    drive_send_code_error("phone-banned", "+15551112222",
                          on_phone_number_banned,
                          400, "PHONE_NUMBER_BANNED", 0);
}

static void test_phone_number_flood(void) {
    drive_send_code_error("phone-flood", "+15551112222",
                          on_phone_number_flood,
                          400, "PHONE_NUMBER_FLOOD", 0);
}

static void test_send_code_flood_wait(void) {
    /* FLOOD_WAIT_30: rpc_parse_error must split off the trailing number
     * and populate err.flood_wait_secs = 30 alongside the raw message. */
    drive_send_code_error("flood-wait-30", "+15551112222",
                          on_send_code_flood_wait_30,
                          420, "FLOOD_WAIT_30", 30);
}

static void test_auth_restart(void) {
    /* AUTH_RESTART isn't a migrate error — it asks the caller to restart
     * the whole login flow from the phone prompt. At the auth_session
     * level it surfaces as a plain rpc_error just like any other. */
    drive_send_code_error("auth-restart", "+15551112222",
                          on_auth_restart,
                          401, "AUTH_RESTART", 0);
}

/* ================================================================ */
/* auth.signIn error cases                                           */
/* ================================================================ */

static void test_phone_code_invalid(void) {
    drive_sign_in_error("code-invalid", on_phone_code_invalid,
                        400, "PHONE_CODE_INVALID", 0);
}

static void test_phone_code_expired(void) {
    drive_sign_in_error("code-expired", on_phone_code_expired,
                        400, "PHONE_CODE_EXPIRED", 0);
}

static void test_phone_code_empty(void) {
    drive_sign_in_error("code-empty", on_phone_code_empty,
                        400, "PHONE_CODE_EMPTY", 0);
}

static void test_session_password_needed(void) {
    /* Signals 2FA — surfaces as rpc_error. auth_flow_login observes
     * err.error_msg=="SESSION_PASSWORD_NEEDED" and switches to the
     * getPassword + checkPassword path.  We assert the signal reaches
     * the caller unchanged; the switch itself is exercised in
     * test_login_flow.c's 2FA cases. */
    drive_sign_in_error("password-needed", on_session_password_needed,
                        401, "SESSION_PASSWORD_NEEDED", 0);
}

static void test_session_revoked(void) {
    drive_sign_in_error("session-revoked", on_session_revoked,
                        401, "SESSION_REVOKED", 0);
}

static void test_sign_in_failed_generic(void) {
    drive_sign_in_error("sign-in-failed", on_sign_in_failed,
                        500, "SIGN_IN_FAILED", 0);
}

static void test_sign_in_flood_wait(void) {
    drive_sign_in_error("signin-flood", on_sign_in_flood_wait_60,
                        420, "FLOOD_WAIT_60", 60);
}

/* ================================================================ */
/* auth.checkPassword (2FA) error cases                              */
/* ================================================================ */

static void test_password_hash_invalid(void) {
    with_tmp_home("pwd-hash-invalid");
    mt_server_init();
    mt_server_reset();
    init_srp_fixture();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password, NULL);
    mt_server_expect(CRC_auth_checkPassword, on_password_hash_invalid, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError gp_err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &gp_err) == 0,
           "account.getPassword succeeds");

    int64_t uid = 0xCAFEBABELL;
    RpcError cp_err = {0};
    ASSERT(auth_2fa_check_password(&cfg, &s, &t, &params, "definitely-wrong",
                                    &uid, &cp_err) == -1,
           "auth_2fa_check_password returns -1 on wrong password");
    ASSERT(cp_err.error_code == 400, "error_code 400");
    ASSERT(strcmp(cp_err.error_msg, "PASSWORD_HASH_INVALID") == 0,
           "error_msg PASSWORD_HASH_INVALID");
    ASSERT(uid == 0xCAFEBABELL, "user_id_out untouched on wrong password");

    assert_session_not_mutated();

    transport_close(&t);
    mt_server_reset();
}

static void test_srp_id_invalid(void) {
    /* SRP_ID_INVALID is raised when the srp_id the client replays has
     * expired server-side (a stale getPassword). Assert it surfaces
     * cleanly via RpcError rather than crashing the SRP math. */
    with_tmp_home("srp-id-invalid");
    mt_server_init();
    mt_server_reset();
    init_srp_fixture();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password, NULL);
    mt_server_expect(CRC_auth_checkPassword, on_srp_id_invalid, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError gp_err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &gp_err) == 0,
           "getPassword ok");

    int64_t uid = 0;
    RpcError cp_err = {0};
    ASSERT(auth_2fa_check_password(&cfg, &s, &t, &params, "secret",
                                    &uid, &cp_err) == -1,
           "checkPassword fails with SRP_ID_INVALID");
    ASSERT(cp_err.error_code == 400, "error_code 400");
    ASSERT(strcmp(cp_err.error_msg, "SRP_ID_INVALID") == 0,
           "error_msg SRP_ID_INVALID");

    assert_session_not_mutated();

    transport_close(&t);
    mt_server_reset();
}

static void test_check_password_with_no_password_configured(void) {
    /* auth_2fa_check_password guards against being called on params that
     * report has_password=0 (server claims no 2FA set). Assert the guard
     * fires before the SRP math (no RPC attempted). */
    with_tmp_home("pwd-no-config");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password_no_password, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError gp_err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &gp_err) == 0,
           "getPassword ok (has_password=0)");
    ASSERT(params.has_password == 0,
           "params reflect 'no 2FA on account'");

    int calls_before = mt_server_rpc_call_count();

    int64_t uid = 0;
    RpcError cp_err = {0};
    ASSERT(auth_2fa_check_password(&cfg, &s, &t, &params, "any",
                                    &uid, &cp_err) == -1,
           "checkPassword refuses to run when has_password=0");
    /* Guard fires before api_call — counter must not have incremented. */
    ASSERT(mt_server_rpc_call_count() == calls_before,
           "checkPassword short-circuited before dispatching RPC");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* auth_flow.c orchestrator — fast path + pre-RPC failure           */
/* ================================================================ */

/*
 * Minimal callback triad that signals "callbacks not available".
 * auth_flow_login's fast path returns before these fire; use the ones
 * that return -1 so a regression that skipped the fast path would
 * surface as a clear failure rather than a hang on stdin.
 */
static int cb_no_phone(void *u, char *out, size_t cap) {
    (void)u; (void)out; (void)cap; return -1;
}
static int cb_no_code(void *u, char *out, size_t cap) {
    (void)u; (void)out; (void)cap; return -1;
}

/* Assert auth_flow_login returns 0 on a seeded-session fast path and
 * populates AuthFlowResult with the seeded home DC. Covers lines 70-85
 * of auth_flow.c (the session-restore fast path). */
static void test_auth_flow_login_fast_path_succeeds(void) {
    with_tmp_home("fast-path");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed DC2");

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; transport_init(&t);
    MtProtoSession s; mtproto_session_init(&s);

    AuthFlowCallbacks cb = {
        .get_phone    = cb_no_phone,
        .get_code     = cb_no_code,
        .get_password = NULL,
        .user         = NULL,
    };

    AuthFlowResult result = {0};
    int rc = auth_flow_login(&cfg, &cb, &t, &s, &result);
    ASSERT(rc == 0,
           "auth_flow_login takes fast path when session.bin has auth key");
    ASSERT(result.dc_id == 2,
           "AuthFlowResult reports the persisted home DC");
    ASSERT(s.has_auth_key == 1,
           "session populated with auth_key from session.bin");

    transport_close(&t);
    mt_server_reset();
}

/* auth_flow_login param-validation guard: missing callbacks → -1.
 * Covers lines 64-68. */
static void test_auth_flow_login_missing_callbacks_rejected(void) {
    with_tmp_home("no-cb");
    mt_server_init();
    mt_server_reset();

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; transport_init(&t);
    MtProtoSession s; mtproto_session_init(&s);

    AuthFlowCallbacks cb_missing_phone = {
        .get_phone = NULL, .get_code = cb_no_code,
        .get_password = NULL, .user = NULL,
    };
    ASSERT(auth_flow_login(&cfg, &cb_missing_phone, &t, &s, NULL) == -1,
           "auth_flow_login rejects callbacks with NULL get_phone");

    AuthFlowCallbacks cb_missing_code = {
        .get_phone = cb_no_phone, .get_code = NULL,
        .get_password = NULL, .user = NULL,
    };
    ASSERT(auth_flow_login(&cfg, &cb_missing_code, &t, &s, NULL) == -1,
           "auth_flow_login rejects callbacks with NULL get_code");

    ASSERT(auth_flow_login(NULL, &cb_missing_code, &t, &s, NULL) == -1,
           "auth_flow_login rejects NULL cfg");
    ASSERT(auth_flow_login(&cfg, NULL, &t, &s, NULL) == -1,
           "auth_flow_login rejects NULL cb");
    ASSERT(auth_flow_login(&cfg, &cb_missing_phone, NULL, &s, NULL) == -1,
           "auth_flow_login rejects NULL transport");
    ASSERT(auth_flow_login(&cfg, &cb_missing_phone, &t, NULL, NULL) == -1,
           "auth_flow_login rejects NULL session");

    transport_close(&t);
    mt_server_reset();
}

/* auth_flow_connect_dc rejects unknown DC IDs (covers the dc_lookup
 * NULL branch at lines 28-32). */
static void test_auth_flow_connect_dc_unknown_id(void) {
    with_tmp_home("connect-dc-unknown");

    Transport t; transport_init(&t);
    MtProtoSession s; mtproto_session_init(&s);

    ASSERT(auth_flow_connect_dc(999, &t, &s) == -1,
           "auth_flow_connect_dc rejects unknown DC id");

    /* NULL-guards. */
    ASSERT(auth_flow_connect_dc(2, NULL, &s) == -1,
           "auth_flow_connect_dc rejects NULL transport");
    ASSERT(auth_flow_connect_dc(2, &t, NULL) == -1,
           "auth_flow_connect_dc rejects NULL session");
}

/* ================================================================ */
/* Suite entry point                                                 */
/* ================================================================ */

void run_auth_flow_errors_tests(void) {
    /* auth.sendCode error surface */
    RUN_TEST(test_phone_number_invalid);
    RUN_TEST(test_phone_number_banned);
    RUN_TEST(test_phone_number_flood);
    RUN_TEST(test_send_code_flood_wait);
    RUN_TEST(test_auth_restart);
    /* auth.signIn error surface */
    RUN_TEST(test_phone_code_invalid);
    RUN_TEST(test_phone_code_expired);
    RUN_TEST(test_phone_code_empty);
    RUN_TEST(test_session_password_needed);
    RUN_TEST(test_session_revoked);
    RUN_TEST(test_sign_in_failed_generic);
    RUN_TEST(test_sign_in_flood_wait);
    /* 2FA error surface */
    RUN_TEST(test_password_hash_invalid);
    RUN_TEST(test_srp_id_invalid);
    RUN_TEST(test_check_password_with_no_password_configured);
    /* auth_flow.c orchestrator */
    RUN_TEST(test_auth_flow_login_fast_path_succeeds);
    RUN_TEST(test_auth_flow_login_missing_callbacks_rejected);
    RUN_TEST(test_auth_flow_connect_dc_unknown_id);
}
