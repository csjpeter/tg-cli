/**
 * @file test_mt_server_smoke.c
 * @brief End-to-end proof that the mock Telegram server round-trips
 *        real AES-IGE + SHA-256 against the production client code.
 *
 * Wires up:
 *   mock_socket (in-memory transport)
 *     ↕
 *   production rpc_send_encrypted / rpc_recv_encrypted (real crypto)
 *     ↕
 *   mock_tel_server (real crypto on the other side)
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CRC_ping     0x7abe77ecU   /* ping#7abe77ec ping_id:long = Pong; */
#define CRC_pong     0x347773c5U   /* pong#347773c5 msg_id:long ping_id:long = Pong; */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-mt-server-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void on_ping(MtRpcContext *ctx) {
    /* ping body = CRC(4) ping_id:long — echo back pong with the same id. */
    if (ctx->req_body_len < 4 + 8) return;
    uint64_t ping_id = 0;
    for (int i = 0; i < 8; ++i) {
        ping_id |= ((uint64_t)ctx->req_body[4 + i]) << (i * 8);
    }

    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_pong);
    tl_write_uint64(&w, ctx->req_msg_id);
    tl_write_uint64(&w, ping_id);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_ping_pong_roundtrip(void) {
    with_tmp_home("ping-pong");
    mt_server_init();
    mt_server_reset();

    uint8_t auth_key[256];
    uint64_t salt = 0, sid = 0;
    ASSERT(mt_server_seed_session(2, auth_key, &salt, &sid) == 0,
           "seed session writes disk");
    mt_server_expect(CRC_ping, on_ping, NULL);

    /* Load that same session into our own MtProtoSession — the production
     * auth_flow would do this; here we drive session_store directly. */
    MtProtoSession s;
    mtproto_session_init(&s);
    int dc_id = 0;
    ASSERT(session_store_load(&s, &dc_id) == 0, "session loaded");
    ASSERT(dc_id == 2, "home DC is 2");

    Transport t;
    transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "fake connect");

    /* Send a ping. */
    TlWriter req;
    tl_writer_init(&req);
    tl_write_uint32(&req, CRC_ping);
    tl_write_uint64(&req, 0x1234567890ABCDEFULL);
    ASSERT(rpc_send_encrypted(&s, &t, req.data, req.len, 1) == 0,
           "send encrypted ping");
    tl_writer_free(&req);

    /* Read back pong. */
    uint8_t reply[1024];
    size_t reply_len = 0;
    ASSERT(rpc_recv_encrypted(&s, &t, reply, sizeof(reply), &reply_len) == 0,
           "recv encrypted pong");
    ASSERT(reply_len >= 4 + 8 + 4 + 8 + 8, "reply big enough for rpc_result+pong");

    /* rpc_result#f35c6d01 req_msg_id:long result:Object */
    TlReader r = tl_reader_init(reply, reply_len);
    uint32_t crc_result = tl_read_uint32(&r);
    ASSERT(crc_result == 0xf35c6d01U, "outer is rpc_result");
    tl_read_uint64(&r); /* req_msg_id */
    uint32_t crc_pong = tl_read_uint32(&r);
    ASSERT(crc_pong == CRC_pong, "inner is pong");
    tl_read_uint64(&r); /* msg_id echo */
    uint64_t returned_ping_id = tl_read_uint64(&r);
    ASSERT(returned_ping_id == 0x1234567890ABCDEFULL, "ping_id round-trips");

    ASSERT(mt_server_rpc_call_count() == 1, "exactly one RPC dispatched");

    transport_close(&t);
    mt_server_reset();
}

static void test_unknown_crc_returns_rpc_error(void) {
    with_tmp_home("no-handler");
    mt_server_init();
    mt_server_reset();

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    /* no handler registered */

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc_id = 0;
    ASSERT(session_store_load(&s, &dc_id) == 0, "load");

    Transport t;
    transport_init(&t);
    transport_connect(&t, "127.0.0.1", 443);

    /* Send an unregistered CRC (e.g. help.getConfig#c4f9186b). */
    TlWriter req;
    tl_writer_init(&req);
    tl_write_uint32(&req, 0xc4f9186bU);
    rpc_send_encrypted(&s, &t, req.data, req.len, 1);
    tl_writer_free(&req);

    uint8_t reply[512];
    size_t reply_len = 0;
    ASSERT(rpc_recv_encrypted(&s, &t, reply, sizeof(reply), &reply_len) == 0,
           "recv reply");
    uint64_t req_msg_id = 0;
    const uint8_t *inner = NULL;
    size_t inner_len = 0;
    ASSERT(rpc_unwrap_result(reply, reply_len, &req_msg_id, &inner, &inner_len) == 0,
           "unwrap rpc_result");
    RpcError err;
    ASSERT(rpc_parse_error(inner, inner_len, &err) == 0,
           "inner is rpc_error");
    ASSERT(err.error_code == 500, "500 status");
    ASSERT(strstr(err.error_msg, "NO_HANDLER") != NULL,
           "error message says NO_HANDLER");

    transport_close(&t);
    mt_server_reset();
}

void run_mt_server_smoke_tests(void) {
    RUN_TEST(test_ping_pong_roundtrip);
    RUN_TEST(test_unknown_crc_returns_rpc_error);
}
