/**
 * @file test_domain_read_history.c
 * @brief Unit tests for domain_mark_read (US-P5-04).
 */

#include "test_helpers.h"
#include "domain/write/read_history.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t zeros24[24] = {0}; tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);
    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0}; tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    size_t dwords = w.len / 4;
    size_t off = 0;
    if (dwords < 0x7F) { out[0] = (uint8_t)dwords; off = 1; }
    else {
        out[0] = 0x7F;
        out[1] = (uint8_t)dwords;
        out[2] = (uint8_t)(dwords >> 8);
        out[3] = (uint8_t)(dwords >> 16);
        off = 4;
    }
    memcpy(out + off, w.data, w.len);
    *out_len = off + w.len;
    tl_writer_free(&w);
}

static void fix_session(MtProtoSession *s) {
    mtproto_session_init(s);
    uint8_t k[256] = {0}; mtproto_session_set_auth_key(s, k);
    mtproto_session_set_salt(s, 0xBADCAFEDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

static void test_mark_read_self(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_affectedMessages);
    tl_write_int32 (&w, 10);                 /* pts */
    tl_write_int32 (&w, 2);                  /* pts_count */
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_mark_read(&cfg, &s, &t, &peer, 100, NULL);
    ASSERT(rc == 0, "mark_read accepts messages.affectedMessages");
}

static void test_mark_read_channel_bool_true(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolTrue);
    uint8_t payload[32]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[128]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_CHANNEL,
                          .peer_id = 1, .access_hash = 2 };
    int rc = domain_mark_read(&cfg, &s, &t, &peer, 50, NULL);
    ASSERT(rc == 0, "mark_read accepts boolTrue on channel");
}

static void test_mark_read_channel_bool_false(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolFalse);
    uint8_t payload[32]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[128]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_CHANNEL,
                          .peer_id = 1, .access_hash = 2 };
    int rc = domain_mark_read(&cfg, &s, &t, &peer, 50, NULL);
    ASSERT(rc == -1, "mark_read rejects boolFalse on channel");
}

static void test_mark_read_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 403);
    tl_write_string(&w, "CHAT_ADMIN_REQUIRED");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_CHAT,
                          .peer_id = 42 };
    RpcError err = {0};
    int rc = domain_mark_read(&cfg, &s, &t, &peer, 0, &err);
    ASSERT(rc != 0, "RPC error propagates");
    ASSERT(err.error_code == 403, "error_code captured");
}

static void test_mark_read_null_args(void) {
    ASSERT(domain_mark_read(NULL, NULL, NULL, NULL, 0, NULL) == -1,
           "null args");
}

void run_domain_read_history_tests(void) {
    RUN_TEST(test_mark_read_self);
    RUN_TEST(test_mark_read_channel_bool_true);
    RUN_TEST(test_mark_read_channel_bool_false);
    RUN_TEST(test_mark_read_rpc_error);
    RUN_TEST(test_mark_read_null_args);
}
