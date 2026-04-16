/**
 * @file test_domain_edit_delete_forward.c
 * @brief Unit tests for the P5-06 write family (edit / delete / forward).
 */

#include "test_helpers.h"
#include "domain/write/edit.h"
#include "domain/write/delete.h"
#include "domain/write/forward.h"
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
    mtproto_session_set_salt(s, 0xDEADBEEFDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* ---- edit ---- */

static void test_edit_updates_envelope(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updateShort);
    tl_write_uint32(&w, 0); tl_write_int32(&w, 1);
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_edit_message(&cfg, &s, &t, &peer, 42, "fixed", NULL);
    ASSERT(rc == 0, "edit accepts Updates envelope");
}

static void test_edit_rejects_bad_args(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    ASSERT(domain_edit_message(&cfg, &s, &t, &peer, 0, "x", NULL) == -1,
           "msg_id=0 rejected");
    ASSERT(domain_edit_message(&cfg, &s, &t, &peer, 10, "", NULL) == -1,
           "empty text rejected");
    ASSERT(domain_edit_message(NULL, NULL, NULL, NULL, 1, "x", NULL) == -1,
           "null args");
}

/* ---- delete ---- */

static void test_delete_affected_messages(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_affectedMessages);
    tl_write_int32 (&w, 5); tl_write_int32(&w, 1);
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int32_t ids[] = { 101, 102, 103 };
    int rc = domain_delete_messages(&cfg, &s, &t, &peer, ids, 3, 1, NULL);
    ASSERT(rc == 0, "delete accepts affectedMessages");
}

static void test_delete_channel_dispatch(void) {
    mock_socket_reset(); mock_crypto_reset();
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_affectedMessages);
    tl_write_int32 (&w, 1); tl_write_int32(&w, 1);
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);
    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_CHANNEL,
                          .peer_id = 1, .access_hash = 2 };
    int32_t ids[] = { 55 };
    int rc = domain_delete_messages(&cfg, &s, &t, &peer, ids, 1, 0, NULL);
    ASSERT(rc == 0, "channel delete ok");

    /* Verify channels.deleteMessages CRC appears in the outbound buffer. */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    uint32_t want = 0x84c1fd4eu;
    int found = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &want, 4) == 0) { found = 1; break; }
    ASSERT(found, "channels.deleteMessages CRC transmitted");
}

static void test_delete_rejects_bad_args(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int32_t ids[1] = { 1 };
    ASSERT(domain_delete_messages(&cfg, &s, &t, &peer, NULL, 1, 0, NULL) == -1,
           "null ids");
    ASSERT(domain_delete_messages(&cfg, &s, &t, &peer, ids, 0, 0, NULL) == -1,
           "n_ids=0");
    ASSERT(domain_delete_messages(&cfg, &s, &t, &peer, ids, 999, 0, NULL) == -1,
           "n_ids too large");
}

/* ---- forward ---- */

static void test_forward_updates_envelope(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updateShort);
    tl_write_uint32(&w, 0); tl_write_int32(&w, 1);
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer from = { .kind = HISTORY_PEER_SELF };
    HistoryPeer to   = { .kind = HISTORY_PEER_USER,
                          .peer_id = 10, .access_hash = 20 };
    int32_t ids[] = { 1 };
    int rc = domain_forward_messages(&cfg, &s, &t, &from, &to, ids, 1, NULL);
    ASSERT(rc == 0, "forward accepts Updates envelope");
}

static void test_forward_rejects_bad_args(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer from = { .kind = HISTORY_PEER_SELF };
    int32_t ids[1] = { 1 };
    ASSERT(domain_forward_messages(&cfg, &s, &t, &from, NULL, ids, 1, NULL) == -1,
           "null to");
    ASSERT(domain_forward_messages(&cfg, &s, &t, &from, &from, NULL, 1, NULL) == -1,
           "null ids");
}

/* ---- reply via domain_send_message_reply ---- */

static void test_send_reply_embeds_reply_to(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x9015e101u);        /* updateShortSentMessage */
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 888);
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
    int32_t id = -1;
    int rc = domain_send_message_reply(&cfg, &s, &t, &peer,
                                         "thread reply", 1234, &id, NULL);
    ASSERT(rc == 0, "reply send ok");
    ASSERT(id == 888, "id captured");

    /* inputReplyToMessage CRC must be present in the transmitted buffer. */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    uint32_t want = 0x22c0f6d5u;
    int found = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &want, 4) == 0) { found = 1; break; }
    ASSERT(found, "inputReplyToMessage CRC transmitted");
}

void run_domain_edit_delete_forward_tests(void) {
    RUN_TEST(test_edit_updates_envelope);
    RUN_TEST(test_edit_rejects_bad_args);
    RUN_TEST(test_delete_affected_messages);
    RUN_TEST(test_delete_channel_dispatch);
    RUN_TEST(test_delete_rejects_bad_args);
    RUN_TEST(test_forward_updates_envelope);
    RUN_TEST(test_forward_rejects_bad_args);
    RUN_TEST(test_send_reply_embeds_reply_to);
}
