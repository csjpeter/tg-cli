/**
 * @file test_write_path.c
 * @brief FT-05 — write-path functional tests through the mock server.
 *
 * Covers the full write surface (US-11..US-13): messages.sendMessage,
 * messages.editMessage, messages.deleteMessages / channels.deleteMessages,
 * messages.forwardMessages, messages.readHistory / channels.readHistory.
 *
 * Every test goes through the real rpc_send_encrypted / rpc_recv_encrypted
 * path, so the in-process mock server sees exactly the wire bytes the
 * client would emit to a real DC.
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

#include "domain/write/send.h"
#include "domain/write/edit.h"
#include "domain/write/delete.h"
#include "domain/write/forward.h"
#include "domain/write/read_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* CRCs the emulator dispatches on. */
#define CRC_messages_sendMessage     0x0d9d75a4U
#define CRC_messages_editMessage     0x48f71778U
#define CRC_messages_deleteMessages  0xe58e95d2U
#define CRC_channels_deleteMessages  0x84c1fd4eU
#define CRC_messages_forwardMessages 0xc661bbc4U
#define CRC_messages_readHistory     0x0e306d3aU
#define CRC_channels_readHistory     0xcc104937U
#define CRC_updateShortSentMessage   0x9015e101U

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-write-%s", tag);
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
    ASSERT(session_store_load(s, &dc) == 0, "load");
}

/* ================================================================ */
/* Reusable reply builders                                          */
/* ================================================================ */

/* updateShortSentMessage#9015e101 flags:# out:flags.1?true id:int
 *   pts:int pts_count:int date:int media:flags.9?MessageMedia ...
 *
 * Minimal construction: flags=0, id=<id>, pts=0, pts_count=0, date=0. */
static void reply_update_short_sent(MtRpcContext *ctx, int32_t id) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updateShortSentMessage);
    tl_write_uint32(&w, 0);    /* flags */
    tl_write_int32 (&w, id);
    tl_write_int32 (&w, 0);    /* pts */
    tl_write_int32 (&w, 0);    /* pts_count */
    tl_write_int32 (&w, 0);    /* date */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* updates#74ae4240 updates:Vector<Update> users:Vector<User>
 *   chats:Vector<Chat> date:int seq:int — empty vectors keep the wire
 *   minimal. The client does not descend into the vectors for now; it
 *   only checks the top CRC. */
static void reply_updates_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* updates */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_int32 (&w, 0); /* date */
    tl_write_int32 (&w, 0); /* seq */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.affectedMessages#84d19185 pts:int pts_count:int */
static void reply_affected_messages(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_affectedMessages);
    tl_write_int32 (&w, 0); /* pts */
    tl_write_int32 (&w, 0); /* pts_count */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void reply_bool_true(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolTrue);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

static void on_send_message(MtRpcContext *ctx) {
    reply_update_short_sent(ctx, 555);
}

static void on_send_message_peer_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PEER_ID_INVALID");
}

static void on_send_message_flood_wait(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 420, "FLOOD_WAIT_30");
}

static void on_edit_message(MtRpcContext *ctx) {
    reply_updates_empty(ctx);
}

static void on_edit_not_modified(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "MESSAGE_NOT_MODIFIED");
}

static void on_delete_messages(MtRpcContext *ctx) {
    reply_affected_messages(ctx);
}

static void on_channels_delete(MtRpcContext *ctx) {
    reply_affected_messages(ctx);
}

/* Responder that asserts flags.0 == 0 (no revoke) and returns ok. */
static void on_delete_no_revoke(MtRpcContext *ctx) {
    /* req_body layout: [CRC:4][flags:4][vector...] */
    ASSERT(ctx->req_body_len >= 8, "req_body large enough for flags");
    uint32_t flags = 0;
    memcpy(&flags, ctx->req_body + 4, 4);
    ASSERT((flags & 1u) == 0u, "flags.0 must be clear (no revoke)");
    reply_affected_messages(ctx);
}

/* Responder that asserts flags.0 == 1 (revoke set) and returns ok. */
static void on_delete_with_revoke(MtRpcContext *ctx) {
    ASSERT(ctx->req_body_len >= 8, "req_body large enough for flags");
    uint32_t flags = 0;
    memcpy(&flags, ctx->req_body + 4, 4);
    ASSERT((flags & 1u) == 1u, "flags.0 must be set (revoke)");
    reply_affected_messages(ctx);
}

static void on_edit_message_id_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "MESSAGE_ID_INVALID");
}

static void on_edit_author_required(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 403, "MESSAGE_AUTHOR_REQUIRED");
}

static void on_delete_peer_id_invalid(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PEER_ID_INVALID");
}

static void on_forward_messages(MtRpcContext *ctx) {
    reply_updates_empty(ctx);
}

static void on_read_history(MtRpcContext *ctx) {
    reply_affected_messages(ctx);
}

static void on_channels_read_history(MtRpcContext *ctx) {
    reply_bool_true(ctx);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

static void test_send_message_happy(void) {
    with_tmp_home("send-ok");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_sendMessage, on_send_message, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t, &self,
                               "hello from tg-cli", &mid, &err) == 0,
           "sendMessage succeeds");
    ASSERT(mid == 555, "message id echoed from updateShortSentMessage");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_message_reply(void) {
    with_tmp_home("send-reply");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_sendMessage, on_send_message, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message_reply(&cfg, &s, &t, &self,
                                     "thread reply", 100, &mid, &err) == 0,
           "sendMessage w/ reply succeeds");
    ASSERT(mid == 555, "id still echoed");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_message_empty_rejected(void) {
    with_tmp_home("send-empty");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    /* No handler registered — the call must be rejected before reaching
     * the network. */

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t, &self, "", &mid, &err) == -1,
           "empty message rejected client-side");
    ASSERT(mt_server_rpc_call_count() == 0,
           "no RPC dispatched for empty message");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_message_rpc_error(void) {
    with_tmp_home("send-err");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_sendMessage,
                     on_send_message_peer_invalid, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer bogus = {
        .kind = HISTORY_PEER_USER, .peer_id = 9, .access_hash = 0
    };
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t, &bogus, "oops", &mid, &err) == -1,
           "sendMessage -1 on PEER_ID_INVALID");
    ASSERT(err.error_code == 400, "400");
    ASSERT(strcmp(err.error_msg, "PEER_ID_INVALID") == 0,
           "PEER_ID_INVALID propagated");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_message_flood_wait(void) {
    with_tmp_home("send-flood");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_sendMessage, on_send_message_flood_wait, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    int rc = domain_send_message(&cfg, &s, &t, &self, "hi", &mid, &err);
    ASSERT(rc == -1, "FLOOD_WAIT_30 must return -1");
    ASSERT(err.error_code == 420, "error_code == 420");
    ASSERT(strcmp(err.error_msg, "FLOOD_WAIT_30") == 0,
           "error_msg is FLOOD_WAIT_30");
    ASSERT(err.flood_wait_secs == 30, "flood_wait_secs parsed as 30");
    /* Verify no auto-retry: exactly one RPC call dispatched. */
    ASSERT(mt_server_rpc_call_count() == 1,
           "no auto-retry: exactly 1 RPC call");

    transport_close(&t);
    mt_server_reset();
}

static void test_edit_message_happy(void) {
    with_tmp_home("edit-ok");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_editMessage, on_edit_message, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_edit_message(&cfg, &s, &t, &self, 100,
                               "edited text", &err) == 0,
           "editMessage succeeds");

    transport_close(&t);
    mt_server_reset();
}

static void test_edit_message_not_modified(void) {
    with_tmp_home("edit-nm");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_editMessage, on_edit_not_modified, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_edit_message(&cfg, &s, &t, &self, 100,
                               "same text", &err) == -1,
           "editMessage -1 on MESSAGE_NOT_MODIFIED");
    ASSERT(strcmp(err.error_msg, "MESSAGE_NOT_MODIFIED") == 0, "msg");

    transport_close(&t);
    mt_server_reset();
}

static void test_delete_messages_user(void) {
    with_tmp_home("del-user");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_deleteMessages, on_delete_messages, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t ids[] = {1, 2, 3};
    RpcError err = {0};
    ASSERT(domain_delete_messages(&cfg, &s, &t, &self, ids, 3,
                                  /*revoke=*/1, &err) == 0,
           "deleteMessages (user/chat) ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_delete_messages_channel(void) {
    with_tmp_home("del-chan");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_channels_deleteMessages, on_channels_delete, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer chan = {
        .kind = HISTORY_PEER_CHANNEL,
        .peer_id = 1234567,
        .access_hash = 0x1111222233334444LL
    };
    int32_t ids[] = {42};
    RpcError err = {0};
    ASSERT(domain_delete_messages(&cfg, &s, &t, &chan, ids, 1,
                                  /*revoke=*/0, &err) == 0,
           "deleteMessages (channel) ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_delete_messages_no_revoke(void) {
    with_tmp_home("del-no-revoke");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_deleteMessages, on_delete_no_revoke, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t ids[] = {10};
    RpcError err = {0};
    ASSERT(domain_delete_messages(&cfg, &s, &t, &self, ids, 1,
                                  /*revoke=*/0, &err) == 0,
           "deleteMessages (no revoke) ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_delete_messages_with_revoke(void) {
    with_tmp_home("del-revoke");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_deleteMessages, on_delete_with_revoke, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t ids[] = {20};
    RpcError err = {0};
    ASSERT(domain_delete_messages(&cfg, &s, &t, &self, ids, 1,
                                  /*revoke=*/1, &err) == 0,
           "deleteMessages (with --revoke) ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_edit_message_id_invalid(void) {
    with_tmp_home("edit-mid-inv");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_editMessage, on_edit_message_id_invalid, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_edit_message(&cfg, &s, &t, &self, 9999,
                               "new text", &err) == -1,
           "editMessage -1 on MESSAGE_ID_INVALID");
    ASSERT(err.error_code == 400, "error_code == 400");
    ASSERT(strcmp(err.error_msg, "MESSAGE_ID_INVALID") == 0,
           "MESSAGE_ID_INVALID propagated");

    transport_close(&t);
    mt_server_reset();
}

static void test_edit_message_author_required(void) {
    with_tmp_home("edit-auth-req");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_editMessage, on_edit_author_required, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_edit_message(&cfg, &s, &t, &self, 100,
                               "not my msg", &err) == -1,
           "editMessage -1 on MESSAGE_AUTHOR_REQUIRED");
    ASSERT(err.error_code == 403, "error_code == 403");
    ASSERT(strcmp(err.error_msg, "MESSAGE_AUTHOR_REQUIRED") == 0,
           "MESSAGE_AUTHOR_REQUIRED propagated");

    transport_close(&t);
    mt_server_reset();
}

static void test_delete_peer_id_invalid(void) {
    with_tmp_home("del-peer-inv");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_deleteMessages, on_delete_peer_id_invalid, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer bogus = {
        .kind = HISTORY_PEER_USER, .peer_id = 0, .access_hash = 0
    };
    int32_t ids[] = {1};
    RpcError err = {0};
    ASSERT(domain_delete_messages(&cfg, &s, &t, &bogus, ids, 1,
                                  /*revoke=*/0, &err) == -1,
           "deleteMessages -1 on PEER_ID_INVALID");
    ASSERT(err.error_code == 400, "error_code == 400");
    ASSERT(strcmp(err.error_msg, "PEER_ID_INVALID") == 0,
           "PEER_ID_INVALID propagated");

    transport_close(&t);
    mt_server_reset();
}

static void test_forward_messages(void) {
    with_tmp_home("fwd");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_forwardMessages, on_forward_messages, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    HistoryPeer other = {
        .kind = HISTORY_PEER_USER,
        .peer_id = 555,
        .access_hash = 0xDEAD
    };
    int32_t ids[] = {10, 20};
    RpcError err = {0};
    ASSERT(domain_forward_messages(&cfg, &s, &t, &self, &other, ids, 2,
                                   &err) == 0,
           "forwardMessages ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_mark_read_user(void) {
    with_tmp_home("read-user");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_readHistory, on_read_history, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_mark_read(&cfg, &s, &t, &self, 100, &err) == 0,
           "mark_read (user/chat) ok");

    transport_close(&t);
    mt_server_reset();
}

static void test_mark_read_channel(void) {
    with_tmp_home("read-chan");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_channels_readHistory, on_channels_read_history, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer chan = {
        .kind = HISTORY_PEER_CHANNEL,
        .peer_id = 99,
        .access_hash = 0xABCD
    };
    RpcError err = {0};
    ASSERT(domain_mark_read(&cfg, &s, &t, &chan, 500, &err) == 0,
           "mark_read (channel) ok");

    transport_close(&t);
    mt_server_reset();
}

void run_write_path_tests(void) {
    RUN_TEST(test_send_message_happy);
    RUN_TEST(test_send_message_reply);
    RUN_TEST(test_send_message_empty_rejected);
    RUN_TEST(test_send_message_rpc_error);
    RUN_TEST(test_send_message_flood_wait);
    RUN_TEST(test_edit_message_happy);
    RUN_TEST(test_edit_message_not_modified);
    RUN_TEST(test_edit_message_id_invalid);
    RUN_TEST(test_edit_message_author_required);
    RUN_TEST(test_delete_peer_id_invalid);
    RUN_TEST(test_delete_messages_user);
    RUN_TEST(test_delete_messages_channel);
    RUN_TEST(test_delete_messages_no_revoke);
    RUN_TEST(test_delete_messages_with_revoke);
    RUN_TEST(test_forward_messages);
    RUN_TEST(test_mark_read_user);
    RUN_TEST(test_mark_read_channel);
}
