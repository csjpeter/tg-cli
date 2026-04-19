/**
 * @file test_logout_rpc.c
 * @brief TEST-23 — functional tests for auth.logOut RPC + session wipe.
 *
 * Covers two scenarios:
 *   1. Happy path: server returns auth.loggedOut → RPC fires once, session.bin
 *      removed, auth_logout() returns.
 *   2. Error path: server returns rpc_error → session.bin still wiped
 *      (best-effort logout).
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "infrastructure/auth_logout.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"
#include "app/session_store.h"
#include "tl_serial.h"
#include "tl_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- helpers ---- */

static void with_tmp_home_logout(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-logout-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static int session_bin_exists(void) {
    char path[512];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin", home);
    struct stat st;
    return stat(path, &st) == 0;
}

/** Connect a fresh Transport to the mock-socket loopback. */
static void connect_mock_logout(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0,
           "transport connects for logout test");
}

/** Populate an ApiConfig with fake credentials. */
static void init_cfg_logout(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id   = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

/** Happy path: auth.loggedOut#c3a2835f with flags=0. */
static void on_logout_ok(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_loggedOut);   /* 0xc3a2835f */
    tl_write_uint32(&w, 0);                    /* flags = 0 (no future_auth_token) */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/** Error path: generic rpc_error 500 INTERNAL. */
static void on_logout_error(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 500, "INTERNAL");
}

/* ================================================================ */
/* Test cases                                                       */
/* ================================================================ */

/**
 * @brief TEST-23a — happy path: auth.loggedOut returned, session.bin wiped.
 */
static void test_logout_rpc_happy(void) {
    with_tmp_home_logout("happy");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed session");

    /* Confirm session.bin was created by the seed. */
    ASSERT(session_bin_exists(), "session.bin exists before logout");

    mt_server_expect(CRC_auth_logOut, on_logout_ok, NULL);

    ApiConfig cfg; init_cfg_logout(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded");

    Transport t; connect_mock_logout(&t);

    /* auth_logout() = auth_logout_rpc() + session_store_clear(). */
    auth_logout(&cfg, &s, &t);

    /* Responder must have fired exactly once. */
    ASSERT(mt_server_request_crc_count(CRC_auth_logOut) == 1,
           "auth.logOut sent exactly once");

    /* session.bin must be gone. */
    ASSERT(!session_bin_exists(), "session.bin removed after logout");

    transport_close(&t);
    mt_server_reset();
}

/**
 * @brief TEST-23b — error path: server returns rpc_error, session.bin still
 *        wiped (best-effort logout).
 */
static void test_logout_rpc_error_still_clears_session(void) {
    with_tmp_home_logout("error");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed session");

    ASSERT(session_bin_exists(), "session.bin exists before logout");

    mt_server_expect(CRC_auth_logOut, on_logout_error, NULL);

    ApiConfig cfg; init_cfg_logout(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded");

    Transport t; connect_mock_logout(&t);

    /* auth_logout() must wipe the file even when the RPC fails. */
    auth_logout(&cfg, &s, &t);

    /* Responder fired. */
    ASSERT(mt_server_request_crc_count(CRC_auth_logOut) == 1,
           "auth.logOut sent exactly once (error variant)");

    /* File still gone regardless of RPC error. */
    ASSERT(!session_bin_exists(),
           "session.bin removed even after RPC error (best-effort)");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_logout_rpc_tests(void) {
    RUN_TEST(test_logout_rpc_happy);
    RUN_TEST(test_logout_rpc_error_still_clears_session);
}
