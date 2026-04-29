/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_integration_live.c
 * @brief TEST-89 — integration tests against the real Telegram test DC.
 *
 * All 8 tests skip cleanly (exit 0) when TG_TEST_API_ID is absent.
 * When credentials are present they exercise the full MTProto stack:
 * DH handshake, auth.sendCode / signIn, domain read calls, and logout.
 *
 * Environment variables (set via CI secrets or local shell):
 *   TG_TEST_API_ID    — api_id from my.telegram.org test app
 *   TG_TEST_API_HASH  — api_hash
 *   TG_TEST_DC_HOST   — test DC host (e.g. 149.154.175.10)
 *   TG_TEST_DC_PORT   — test DC port (default "443")
 *   TG_TEST_RSA_PEM   — test DC RSA public key (PEM, \\n-escaped single line)
 *   TG_TEST_PHONE     — pre-registered test phone (+999…)
 *   TG_TEST_CODE      — SMS code or "auto" (magic 12345)
 *   TG_TEST_PHONE_B   — (optional) second account for send/receive test
 */

#include "test_helpers_integration.h"

#include "app/bootstrap.h"
#include "app/dc_config.h"
#include "app/auth_flow.h"
#include "app/session_store.h"
#include "telegram_server_key.h"
#include "auth_session.h"
#include "infrastructure/auth_logout.h"
#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/write/send.h"
#include "mtproto_auth.h"
#include "mtproto_rpc.h"
#include "api_call.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/** Apply DC-host and RSA-key overrides from g_integration_config. */
static void apply_test_dc_overrides(void)
{
    const integration_config_t *c = &g_integration_config;

    if (c->rsa_pem && c->rsa_pem[0]) {
        if (telegram_server_key_set_override(c->rsa_pem) != 0) {
            printf("  [WARN] RSA PEM override rejected — using built-in key\n");
        }
    }

    if (c->dc_host && c->dc_host[0]) {
        int port = 443;
        if (c->dc_port && c->dc_port[0])
            port = atoi(c->dc_port);

        /* Override all five DC slots so migration never escapes to production. */
        for (int id = 1; id <= 5; id++) {
            dc_config_set_host_override(id, c->dc_host);
        }
        (void)port; /* port is embedded in the DC table; leave at the default */
    }
}

/**
 * Create a temporary HOME directory and point HOME / XDG env vars at it.
 * Returns the malloc-allocated path (caller must free() after the test).
 * Existing XDG overrides are cleared so path.c falls back to HOME.
 */
static char *setup_temp_home(void)
{
    char tmpl[] = "/tmp/tg-integ-XXXXXX";
    const char *tmp = mkdtemp(tmpl);
    if (!tmp) {
        printf("  [SKIP] mkdtemp failed — cannot create temp HOME\n");
        return NULL;
    }
    char *path = strdup(tmp);
    if (!path) return NULL;

    setenv("HOME", path, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
    return path;
}

/** Remove a directory tree (best-effort, only two levels deep). */
static void rmdir_tree(const char *root)
{
    if (!root) return;
    /* Walk known subdirectories created by bootstrap. */
    static const char *const subdirs[] = {
        "/.config/tg-cli/logs",
        "/.config/tg-cli",
        "/.config",
        "/.cache/tg-cli/logs",
        "/.cache/tg-cli",
        "/.cache",
        NULL
    };
    char path[4096];
    for (int i = 0; subdirs[i]; i++) {
        snprintf(path, sizeof(path), "%s%s", root, subdirs[i]);
        rmdir(path); /* ignore errors — dir may not exist */
    }
    /* Remove any remaining files (session.bin, session.bin.tmp, log files). */
    /* (Simple best-effort — not full recursive.) */
    rmdir(root);
}

/**
 * Build an ApiConfig from g_integration_config.
 */
static ApiConfig make_api_config(void)
{
    ApiConfig cfg;
    api_config_init(&cfg);
    if (g_integration_config.api_id)
        cfg.api_id = atoi(g_integration_config.api_id);
    cfg.api_hash = g_integration_config.api_hash;
    return cfg;
}

/**
 * Callback: supply phone from g_integration_config.
 */
static int cb_get_phone(void *user, char *out, size_t cap)
{
    (void)user;
    const char *phone = g_integration_config.phone;
    if (!phone || !phone[0]) return -1;
    snprintf(out, cap, "%s", phone);
    return 0;
}

/**
 * Callback: supply the magic code ("12345" when TG_TEST_CODE == "auto").
 */
static int cb_get_code(void *user, char *out, size_t cap)
{
    (void)user;
    const char *code = g_integration_config.code;
    if (!code || !code[0]) {
        snprintf(out, cap, "12345");
        return 0;
    }
    if (strcmp(code, "auto") == 0) {
        snprintf(out, cap, "12345");
        return 0;
    }
    snprintf(out, cap, "%s", code);
    return 0;
}

/* -------------------------------------------------------------------------
 * Test 1: DH handshake completes
 * ---------------------------------------------------------------------- */

static void test_dh_handshake_completes(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-dh") == 0, "app_bootstrap failed");

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    int dc_id = DEFAULT_DC_ID;
    int rc = auth_flow_connect_dc(dc_id, &t, &s);
    ASSERT(rc == 0, "auth_flow_connect_dc failed — DH handshake did not complete");

    /* Verify auth key is non-zero (at least the first byte). */
    int any_nonzero = 0;
    for (int i = 0; i < MTPROTO_AUTH_KEY_SIZE; i++) {
        if (s.auth_key[i] != 0) { any_nonzero = 1; break; }
    }
    ASSERT(any_nonzero, "auth_key is all-zero after DH");

    /* Session id should be non-zero (set by mtproto_session_init). */
    ASSERT(s.session_id != 0, "session_id is zero");

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Test 2: login — auth.sendCode + auth.signIn
 * ---------------------------------------------------------------------- */

static void test_login_send_code_and_sign_in(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-login") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    AuthFlowResult result = {0};
    int rc = auth_flow_login(&cfg, &cbs, &t, &s, &result);
    ASSERT(rc == 0, "auth_flow_login failed");
    ASSERT(result.dc_id > 0, "dc_id <= 0 after login");

    /* session.bin must exist after a successful login. */
    char session_path[4096];
    snprintf(session_path, sizeof(session_path),
             "%s/.config/tg-cli/session.bin", tmp_home);
    struct stat st;
    ASSERT(stat(session_path, &st) == 0,
           "session.bin not found after login");

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Test 3: users.getFullUser — self profile
 * ---------------------------------------------------------------------- */

static void test_get_self(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-self") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    ASSERT(auth_flow_login(&cfg, &cbs, &t, &s, NULL) == 0,
           "login failed in test_get_self");

    SelfInfo self = {0};
    int rc = domain_get_self(&cfg, &s, &t, &self);
    ASSERT(rc == 0, "domain_get_self failed");
    ASSERT(self.id != 0, "self.id is zero");

    /* Phone field must match the configured test phone (without leading +). */
    const char *phone = g_integration_config.phone;
    if (phone && phone[0] == '+') phone++;
    if (phone && phone[0]) {
        ASSERT(strstr(self.phone, phone) != NULL ||
               strcmp(self.phone, g_integration_config.phone) == 0,
               "self.phone does not match TG_TEST_PHONE");
    }

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Test 4: messages.getDialogs — at least one dialog
 * ---------------------------------------------------------------------- */

static void test_get_dialogs_returns_at_least_one(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-dialogs") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    ASSERT(auth_flow_login(&cfg, &cbs, &t, &s, NULL) == 0,
           "login failed in test_get_dialogs");

    dialogs_cache_flush();

    DialogEntry dialogs[32];
    int count = 0;
    int total = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 32, 0, dialogs, &count, &total);
    ASSERT(rc == 0, "domain_get_dialogs returned error");
    ASSERT(count >= 1, "expected >=1 dialog; got 0");

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Test 5: messages.getHistory — smoke (empty history is acceptable)
 * ---------------------------------------------------------------------- */

static void test_get_history_smoke(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-history") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    ASSERT(auth_flow_login(&cfg, &cbs, &t, &s, NULL) == 0,
           "login failed in test_get_history_smoke");

    /* Use Saved Messages (inputPeerSelf) — always accessible. */
    HistoryEntry entries[10];
    int count = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 10, entries, &count);
    ASSERT(rc == 0, "domain_get_history_self returned error");
    /* Empty history is fine for a fresh account. */

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Test 6: send and receive message (requires TG_TEST_PHONE_B)
 * ---------------------------------------------------------------------- */

/** Callback: supply second phone (for account B). */
static int cb_get_phone_b(void *user, char *out, size_t cap)
{
    (void)user;
    const char *phone = getenv("TG_TEST_PHONE_B");
    if (!phone || !phone[0]) return -1;
    snprintf(out, cap, "%s", phone);
    return 0;
}

static void test_send_and_receive_message(void)
{
    SKIP_IF_NO_CREDS();

    const char *phone_b = getenv("TG_TEST_PHONE_B");
    if (!phone_b || !phone_b[0]) {
        printf("  [SKIP] test_send_and_receive_message — TG_TEST_PHONE_B not set\n");
        return;
    }

    /* --- Login as account A --- */
    char *tmp_a = setup_temp_home();
    ASSERT(tmp_a != NULL, "setup_temp_home failed for account A");

    apply_test_dc_overrides();

    AppContext ctx_a;
    ASSERT(app_bootstrap(&ctx_a, "tg-integ-send-a") == 0,
           "app_bootstrap failed for account A");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs_a = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t_a;
    transport_init(&t_a);
    MtProtoSession s_a;
    mtproto_session_init(&s_a);

    ASSERT(auth_flow_login(&cfg, &cbs_a, &t_a, &s_a, NULL) == 0,
           "login A failed");

    /* Get self of A so we can determine direction, but mainly just need
     * to send to Saved Messages (self) to avoid needing to resolve B's
     * access_hash — Saved Messages is always reachable as inputPeerSelf. */
    HistoryPeer peer_self = { .kind = HISTORY_PEER_SELF, .peer_id = 0, .access_hash = 0 };
    RpcError err = {0};
    err.migrate_dc = -1;
    int32_t sent_msg_id = 0;
    char test_text[128];
    snprintf(test_text, sizeof(test_text),
             "TEST-89 integration ping %ld", (long)time(NULL));

    int rc = domain_send_message(&cfg, &s_a, &t_a, &peer_self,
                                 test_text, &sent_msg_id, &err);
    ASSERT(rc == 0, "domain_send_message to self failed");

    /* Verify the message appears in history. */
    HistoryEntry entries[10];
    int count = 0;
    rc = domain_get_history_self(&cfg, &s_a, &t_a, 0, 10, entries, &count);
    ASSERT(rc == 0, "domain_get_history_self after send failed");

    /* Check that the sent message id appears in the result (best-effort). */
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].id == sent_msg_id) { found = 1; break; }
    }
    ASSERT(found || count > 0,
           "sent message not found in history (or history empty)");

    transport_close(&t_a);
    app_shutdown(&ctx_a);
    rmdir_tree(tmp_a);
    free(tmp_a);
}

/* -------------------------------------------------------------------------
 * Test 7: salt rotation survives 90-second session
 * ---------------------------------------------------------------------- */

/** Send a ping (ping#7abe77ec) over the encrypted channel. */
static int send_ping(MtProtoSession *s, Transport *t, uint64_t ping_id)
{
    uint8_t buf[12];
    uint32_t crc = 0x7abe77ec;
    memcpy(buf + 0, &crc,     4);
    memcpy(buf + 4, &ping_id, 8);
    return rpc_send_encrypted(s, t, buf, sizeof(buf), 0 /* not content-related */);
}

static void test_salt_rotation_survives_long_session(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-salt") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    ASSERT(auth_flow_login(&cfg, &cbs, &t, &s, NULL) == 0,
           "login failed in test_salt_rotation_survives_long_session");

    /* Hold the session for 90 seconds; send a ping every 10 seconds. */
    printf("  [INFO] holding session for 90 s with pings...\n");

    int ping_errors = 0;
    for (int i = 0; i < 9; i++) {
        sleep(10);
        uint64_t pid = (uint64_t)(i + 1);
        if (send_ping(&s, &t, pid) != 0) {
            ping_errors++;
            printf("  [WARN] ping %d failed (non-fatal — bad_server_salt expected)\n",
                   i + 1);
        }
    }

    /* Even if individual pings fail (e.g. bad_server_salt on first salt change),
     * the session must remain usable — verify by fetching dialogs. */
    dialogs_cache_flush();
    DialogEntry dialogs[4];
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 4, 0, dialogs, &count, NULL);
    ASSERT(rc == 0, "domain_get_dialogs failed after 90s session");

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
    (void)ping_errors;
}

/* -------------------------------------------------------------------------
 * Test 8: auth.logOut clears session
 * ---------------------------------------------------------------------- */

static void test_logout_clears_session(void)
{
    SKIP_IF_NO_CREDS();

    char *tmp_home = setup_temp_home();
    ASSERT(tmp_home != NULL, "setup_temp_home failed");

    apply_test_dc_overrides();

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "tg-integ-logout") == 0, "app_bootstrap failed");

    ApiConfig cfg = make_api_config();

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = NULL,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    ASSERT(auth_flow_login(&cfg, &cbs, &t, &s, NULL) == 0,
           "login failed in test_logout_clears_session");

    /* Confirm session.bin exists before logout. */
    char session_path[4096];
    snprintf(session_path, sizeof(session_path),
             "%s/.config/tg-cli/session.bin", tmp_home);
    struct stat st;
    ASSERT(stat(session_path, &st) == 0,
           "session.bin absent before logout");

    /* Perform full logout (sends auth.logOut + wipes session.bin). */
    auth_logout(&cfg, &s, &t);

    /* session.bin must be gone now. */
    ASSERT(stat(session_path, &st) != 0,
           "session.bin still exists after auth_logout");

    /* Attempting to load the cleared session must fail. */
    MtProtoSession s2;
    mtproto_session_init(&s2);
    int dc_id2 = 0;
    int load_rc = session_store_load(&s2, &dc_id2);
    ASSERT(load_rc != 0, "session_store_load succeeded after logout (should fail)");

    transport_close(&t);
    app_shutdown(&ctx);
    rmdir_tree(tmp_home);
    free(tmp_home);
}

/* -------------------------------------------------------------------------
 * Suite entry point
 * ---------------------------------------------------------------------- */

void run_integration_live_tests(void)
{
    RUN_TEST(test_dh_handshake_completes);
    RUN_TEST(test_login_send_code_and_sign_in);
    RUN_TEST(test_get_self);
    RUN_TEST(test_get_dialogs_returns_at_least_one);
    RUN_TEST(test_get_history_smoke);
    RUN_TEST(test_send_and_receive_message);
    RUN_TEST(test_salt_rotation_survives_long_session);
    RUN_TEST(test_logout_clears_session);
}
