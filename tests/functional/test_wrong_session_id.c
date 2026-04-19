/**
 * @file test_wrong_session_id.c
 * @brief TEST-29 — functional test: mock server injects wrong session_id;
 *        client rejects the frame.
 *
 * The unit tests in tests/unit/test_rpc.c (test_recv_encrypted_wrong_session_id)
 * exercise rpc_recv_encrypted() directly with fake frames and mock crypto.
 * This functional test goes one layer higher: it seeds a real MTProto session,
 * arms the mock server to emit a reply with a deliberately wrong session_id,
 * issues a real api_call(), and asserts that the call returns -1.
 *
 * Flow:
 *   1. mt_server_seed_session() → real auth key on disk and in mock server.
 *   2. mt_server_expect() → register a responder that queues the (tampered)
 *      reply; the client rejects the frame during rpc_recv_encrypted().
 *   3. mt_server_set_wrong_session_id_once() → next reply will have
 *      session_id XOR 0xFFFFFFFFFFFFFFFF in the plaintext.
 *   4. api_call() sends users.getUsers, receives the tampered frame, and
 *      must return -1.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_serial.h"
#include "tl_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- helpers ---- */

static void with_tmp_home_wsi(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-wsi-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock_wsi(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0,
           "transport connects for wrong-session-id test");
}

static void init_cfg_wsi(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id   = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

/* ---- responder ---- */

/**
 * Emit a minimal reply.  The mock server calls this when the client's
 * request arrives, before the (tampered) reply is enqueued.  So this
 * function WILL run — we rely on it to actually queue the bad frame.
 */
static void on_get_users_bad_reply(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Test case                                                        */
/* ================================================================ */

/**
 * @brief TEST-29 — wrong session_id in server reply must be rejected.
 *
 * The mock server is armed to flip all bits of the session_id in its next
 * reply.  rpc_recv_encrypted() verifies the decrypted session_id against
 * s->session_id and must return -1, causing api_call() to propagate -1 to
 * the caller.
 *
 * Note on sequencing: the mock server dispatches the client request
 * synchronously inside api_call() before the client attempts to read.
 * The responder fires (server-side) and enqueues the tampered reply.
 * The client then calls rpc_recv_encrypted(), detects the wrong session_id,
 * and returns -1.  mt_server_rpc_call_count() confirms the server did
 * receive and process the request.
 */
static void test_wrong_session_id_rejected(void) {
    with_tmp_home_wsi("reject");

    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed session");

    /* users.getUsers CRC — a simple query the mock server can handle. */
#define CRC_users_getUsers 0x0d91a548U
    mt_server_expect(CRC_users_getUsers, on_get_users_bad_reply, NULL);

    /* Arm the one-shot: next reply frame will have a wrong session_id. */
    mt_server_set_wrong_session_id_once();

    ApiConfig cfg;  init_cfg_wsi(&cfg);
    MtProtoSession s;  mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded");

    Transport t;  connect_mock_wsi(&t);

    /* Build users.getUsers(inputUserSelf) query. */
#define CRC_inputUserSelf 0xf7c1b13fU
    TlWriter q;
    tl_writer_init(&q);
    tl_write_uint32(&q, CRC_users_getUsers);
    tl_write_uint32(&q, TL_vector);
    tl_write_uint32(&q, 1);
    tl_write_uint32(&q, CRC_inputUserSelf);
    tl_write_uint64(&q, 0ULL);   /* access_hash */

    uint8_t resp[4096];
    size_t  resp_len = 0;
    int rc = api_call(&cfg, &s, &t, q.data, q.len, resp, sizeof(resp), &resp_len);
    tl_writer_free(&q);

    ASSERT(rc == -1, "api_call must return -1 when session_id is wrong");

    /* The mock server dispatched one RPC (used to queue the tampered reply). */
    ASSERT(mt_server_rpc_call_count() == 1,
           "mock server must have received exactly one RPC call");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_wrong_session_id_tests(void) {
    RUN_TEST(test_wrong_session_id_rejected);
}
