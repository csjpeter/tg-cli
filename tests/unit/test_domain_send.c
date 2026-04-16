/**
 * @file test_domain_send.c
 * @brief Unit tests for domain_send_message (US-P5-03).
 */

#include "test_helpers.h"
#include "domain/write/send.h"
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
        out[1] = (uint8_t)(dwords);
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
    mtproto_session_set_salt(s, 0xDEADBEEFDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

#define CRC_updateShortSentMessage 0x9015e101u

/* Server acks sendMessage with updateShortSentMessage carrying id=777. */
static void test_send_self_parses_message_id(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updateShortSentMessage);
    tl_write_uint32(&w, 0);                /* flags */
    tl_write_int32 (&w, 777);              /* id */
    tl_write_int32 (&w, 1);                /* pts */
    tl_write_int32 (&w, 1);                /* pts_count */
    tl_write_int32 (&w, 1700000000);       /* date */
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int32_t msg_id = -1;
    RpcError err = {0};
    int rc = domain_send_message(&cfg, &s, &t, &peer, "hello", &msg_id, &err);
    ASSERT(rc == 0, "send returns ok");
    ASSERT(msg_id == 777, "outgoing message id captured");
}

/* Updates envelope (generic): rc == 0 but we don't try to parse id. */
static void test_send_generic_updates_envelope(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updateShort);    /* simplest Updates variant */
    tl_write_uint32(&w, 0);                 /* update (skipped) */
    tl_write_int32 (&w, 1700000000);        /* date */
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int32_t msg_id = 999;
    int rc = domain_send_message(&cfg, &s, &t, &peer, "ping", &msg_id, NULL);
    ASSERT(rc == 0, "updateShort envelope is accepted");
    ASSERT(msg_id == 0, "unknown envelope resets msg_id to 0");
}

static void test_send_rpc_error_propagates(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 400);
    tl_write_string(&w, "PEER_ID_INVALID");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryPeer peer = { .kind = HISTORY_PEER_USER,
                          .peer_id = 1, .access_hash = 2 };
    RpcError err = {0};
    int rc = domain_send_message(&cfg, &s, &t, &peer, "hi", NULL, &err);
    ASSERT(rc == -1, "RPC error propagates");
    ASSERT(err.error_code == 400, "error_code captured");
    ASSERT(strcmp(err.error_msg, "PEER_ID_INVALID") == 0, "error_msg captured");
}

static void test_send_rejects_bad_inputs(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };

    ASSERT(domain_send_message(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == -1,
           "null args");
    ASSERT(domain_send_message(&cfg, &s, &t, &peer, "", NULL, NULL) == -1,
           "empty message rejected");

    char oversized[4100]; memset(oversized, 'A', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    ASSERT(domain_send_message(&cfg, &s, &t, &peer, oversized, NULL, NULL) == -1,
           "message > 4096 chars rejected");
}

/* Verify the sent bytes carry messages.sendMessage CRC + the message text. */
static void test_send_writes_correct_query(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updateShortSentMessage);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 1700000000);
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);
    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };

    int rc = domain_send_message(&cfg, &s, &t, &peer, "HELLO_WORLD",
                                  NULL, NULL);
    ASSERT(rc == 0, "send ok");

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent != NULL && sent_len > 0, "client transmitted bytes");
    /* Look for the literal message text in the encrypted payload — this
     * works because the mock AES block cipher is identity. */
    int found = 0;
    for (size_t i = 0; i + 11 <= sent_len; i++) {
        if (memcmp(sent + i, "HELLO_WORLD", 11) == 0) { found = 1; break; }
    }
    ASSERT(found, "message text appears in outbound wire buffer");
}

void run_domain_send_tests(void) {
    RUN_TEST(test_send_self_parses_message_id);
    RUN_TEST(test_send_generic_updates_envelope);
    RUN_TEST(test_send_rpc_error_propagates);
    RUN_TEST(test_send_rejects_bad_inputs);
    RUN_TEST(test_send_writes_correct_query);
}
