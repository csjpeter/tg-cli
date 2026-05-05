/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_service_frames.c
 * @brief TEST-88 / US-37 — functional coverage for server-side service frames.
 *
 * src/infrastructure/api_call.c::classify_service_frame handles five
 * non-result CRCs — bad_server_salt, bad_msg_notification,
 * new_session_created, msgs_ack, pong — plus the SERVICE_FRAME_LIMIT (8)
 * drain loop. Until now no functional test injected them; salt rotation
 * on long-running `watch` / TUI sessions was the most likely untested
 * failure mode in the wild.
 *
 * Coverage here exercises the full api_call() pipeline end-to-end:
 *   - bad_server_salt    → automatic retry with refreshed salt succeeds.
 *   - new_session_created → transparent salt refresh, real result surfaces.
 *   - msgs_ack           → transparent skip.
 *   - pong               → transparent skip.
 *   - bad_msg_notification → surfaces as -1 without dropping auth_key.
 *   - service-frame storm → 9 queued frames exceed SERVICE_FRAME_LIMIT and
 *                           api_call returns -1 cleanly.
 *
 * Uses the six `mt_server_reply_*` / `mt_server_stack_service_frames`
 * helpers added to tests/mocks/mock_tel_server.{h,c} alongside this suite.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_registry.h"
#include "tl_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* help.getConfig#c4f9186b — a convenient "any RPC" the mock can reply to. */
#define CRC_help_getConfig  0xc4f9186bU
/* Mirror the production constants so we can assert on the refreshed salt. */
#define SVC_NEW_SESSION_SALT 0xCAFEF00DBAADC0DEULL

/* ================================================================ */
/* Boilerplate                                                       */
/* ================================================================ */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-svcfr-%s", tag);
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

static void load_session(MtProtoSession *s, uint64_t *initial_salt_out) {
    uint64_t salt = 0;
    ASSERT(mt_server_seed_session(2, NULL, &salt, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load session");
    if (initial_salt_out) *initial_salt_out = salt;
}

/* Build a minimal help.getConfig query (just the CRC + no arguments). */
static void build_get_config(TlWriter *w) {
    tl_writer_init(w);
    tl_write_uint32(w, CRC_help_getConfig);
}

/* Responder: return a 4-byte sentinel as the rpc_result body so tests can
 * verify the payload survived service-frame drain. 0x5A5AA5A5 is arbitrary
 * but distinguishable; api_call unwraps rpc_result and hands the sentinel
 * to the caller through the resp buffer. */
#define RESULT_SENTINEL 0x5A5AA5A5U
static void on_get_config_sentinel(MtRpcContext *ctx) {
    uint8_t body[8];
    TlWriter pw; tl_writer_init(&pw);
    tl_write_uint32(&pw, RESULT_SENTINEL);
    tl_write_uint32(&pw, 0);  /* keep 4-byte alignment */
    memcpy(body, pw.data, pw.len);
    size_t body_len = pw.len;
    tl_writer_free(&pw);
    mt_server_reply_result(ctx, body, body_len);
}

/* Check that the decoded api_call response carries our sentinel. */
static void assert_sentinel(const uint8_t *resp, size_t resp_len) {
    ASSERT(resp_len >= 4, "response contains at least one uint32");
    uint32_t first;
    memcpy(&first, resp, 4);
    ASSERT(first == RESULT_SENTINEL,
           "response payload is the handler sentinel — real reply surfaced");
}

/* ================================================================ */
/* Tests                                                             */
/* ================================================================ */

/* 1. bad_server_salt → automatic retry succeeds, salt updated. */
static void test_bad_server_salt_auto_retries_and_succeeds(void) {
    with_tmp_home("bad-salt");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    /* Arm: the first RPC the client sends will bounce off a bad_server_salt
     * reply carrying THIS new salt; the client retries and the handler
     * serves the sentinel. */
    const uint64_t NEW_SALT = 0x1122334455667788ULL;
    mt_server_reply_bad_server_salt(NEW_SALT);

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == 0, "api_call returns 0 after salt-rotation retry");
    assert_sentinel(resp, resp_len);
    ASSERT(s.server_salt == NEW_SALT,
           "s.server_salt replaced with server-issued new_salt");
    ASSERT(s.server_salt != original_salt,
           "salt actually changed (sanity vs initial)");
    ASSERT(mt_server_rpc_call_count() == 1,
           "exactly one handler dispatch — bad_salt preempted the first frame "
           "so only the retry hit the handler");
    ASSERT(s.has_auth_key == 1,
           "auth_key retained across salt rotation");

    transport_close(&t);
    mt_server_reset();
}

/* 2. new_session_created → transparent; salt refreshed; real result surfaces. */
static void test_new_session_created_refreshes_salt(void) {
    with_tmp_home("new-session");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    mt_server_reply_new_session_created();

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == 0, "api_call surfaces real result past new_session_created");
    assert_sentinel(resp, resp_len);
    ASSERT(s.server_salt == SVC_NEW_SESSION_SALT,
           "s.server_salt picked up from new_session_created frame");
    ASSERT(s.server_salt != original_salt,
           "salt moved from seeded value");
    ASSERT(mt_server_rpc_call_count() == 1,
           "single RPC round-trip despite preceding service frame");

    transport_close(&t);
    mt_server_reset();
}

/* 3. msgs_ack → transparent; caller sees only the real result. */
static void test_msgs_ack_is_transparent(void) {
    with_tmp_home("msgs-ack");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    /* Three distinct msg_ids — simulates the server acking a small batch
     * of previously-sent client frames. */
    uint64_t ids[] = {0xA1A1A1A100000001ULL,
                     0xA1A1A1A100000002ULL,
                     0xA1A1A1A100000003ULL};
    mt_server_reply_msgs_ack(ids, 3);

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == 0, "api_call succeeds through transparent msgs_ack");
    assert_sentinel(resp, resp_len);
    ASSERT(s.server_salt == original_salt,
           "msgs_ack does not touch the session salt");

    transport_close(&t);
    mt_server_reset();
}

/* 4. pong → transparent; caller sees only the real result. */
static void test_pong_is_transparent(void) {
    with_tmp_home("pong");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    mt_server_reply_pong(0xDEADBEEF01020304ULL,
                         0xCAFE00DD12345678ULL);

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == 0, "api_call succeeds through transparent pong");
    assert_sentinel(resp, resp_len);
    ASSERT(s.server_salt == original_salt, "pong does not touch the session salt");

    transport_close(&t);
    mt_server_reset();
}

/* 5. bad_msg_notification → specific RPC fails, session retained intact. */
static void test_bad_msg_notification_surfaces_error_without_dropping_session(void) {
    with_tmp_home("bad-msg");
    mt_server_init(); mt_server_reset();
    /* Register a handler so the dispatch path is complete — classify returns
     * SVC_ERROR before the queued handler reply is reached, so the handler
     * never actually surfaces a payload to the client. But the server still
     * computes a reply and queues it (harmless). */
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    /* Capture a copy of the auth_key so we can verify it is preserved. */
    uint8_t auth_key_before[MTPROTO_AUTH_KEY_SIZE];
    memcpy(auth_key_before, s.auth_key, sizeof(auth_key_before));

    /* Queue the bad_msg_notification ahead of the real reply.
     * bad_msg_id = 0xFFEE (synthetic), error_code = 16 (msg_id too low). */
    mt_server_reply_bad_msg_notification(0xFFEEFFEEFFEEFFEEULL, 16);

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == -1,
           "api_call surfaces bad_msg_notification as -1");
    /* Session retained: auth_key bytes unchanged, salt unchanged. */
    ASSERT(memcmp(s.auth_key, auth_key_before, sizeof(auth_key_before)) == 0,
           "auth_key preserved across bad_msg_notification");
    ASSERT(s.server_salt == original_salt,
           "server_salt preserved across bad_msg_notification");
    ASSERT(s.has_auth_key == 1,
           "has_auth_key flag retained — session not discarded");

    transport_close(&t);
    mt_server_reset();
}

/* 6. Service-frame storm — 65 queued acks exceed SERVICE_FRAME_LIMIT (64).
 *    api_call returns -1 cleanly instead of looping forever. */
static void test_service_frame_storm_bails_at_limit(void) {
    with_tmp_home("storm");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_help_getConfig, on_get_config_sentinel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; uint64_t original_salt = 0;
    load_session(&s, &original_salt);

    /* Stack exactly 65 msgs_ack frames — one more than the client's
     * 64-frame drain limit. The 65th classify iteration hits the loop
     * cap and api_call_once falls out of the for-loop with whatever
     * happened to be in `raw_resp` (still a msgs_ack frame) — rpc_unwrap_gzip
     * refuses to decode that as a real payload, so api_call returns -1. */
    mt_server_stack_service_frames(65);

    TlWriter q; build_get_config(&q);
    uint8_t resp[256];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == -1,
           "api_call returns -1 after SERVICE_FRAME_LIMIT is exceeded");
    /* Session untouched even though the call failed. */
    ASSERT(s.server_salt == original_salt,
           "salt unchanged after service-frame storm");
    ASSERT(s.has_auth_key == 1,
           "auth_key retained through storm");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_service_frames_tests(void) {
    RUN_TEST(test_bad_server_salt_auto_retries_and_succeeds);
    RUN_TEST(test_new_session_created_refreshes_salt);
    RUN_TEST(test_msgs_ack_is_transparent);
    RUN_TEST(test_pong_is_transparent);
    RUN_TEST(test_bad_msg_notification_surfaces_error_without_dropping_session);
    RUN_TEST(test_service_frame_storm_bails_at_limit);
}
