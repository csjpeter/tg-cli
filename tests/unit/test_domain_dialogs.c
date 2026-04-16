/**
 * @file test_domain_dialogs.c
 * @brief Unit tests for domain_get_dialogs (US-04, v1).
 *
 * Covers the request builder and the single-Dialog parse path. Multi-dialog
 * parsing is deferred until PeerNotifySettings skip logic is added.
 */

#include "test_helpers.h"
#include "domain/read/dialogs.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

#define CRC_dialog 0xd58a08c6u
#define CRC_peerNotifySettings 0xa83b0426u

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    uint8_t zeros24[24] = {0};
    tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);

    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0};
        tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    size_t units = w.len / 4;
    out[0] = (uint8_t)units;
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
    tl_writer_free(&w);
}

static void fix_session(MtProtoSession *s) {
    mtproto_session_init(s);
    uint8_t key[256] = {0};
    mtproto_session_set_auth_key(s, key);
    mtproto_session_set_salt(s, 0xAAAAAAAAAAAAAAAAULL);
}

static void fix_transport(Transport *t) {
    transport_init(t);
    t->fd = 42; t->connected = 1; t->dc_id = 1;
}

static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* Write a full Dialog record including the required trailer (counts +
 * empty PeerNotifySettings). */
static void write_dialog(TlWriter *w,
                          uint32_t peer_crc, int64_t peer_id,
                          int32_t top_msg, int32_t unread) {
    tl_write_uint32(w, CRC_dialog);
    tl_write_uint32(w, 0);          /* flags */
    tl_write_uint32(w, peer_crc);
    tl_write_int64 (w, peer_id);
    tl_write_int32 (w, top_msg);    /* top_message */
    tl_write_int32 (w, 0);          /* read_inbox_max_id */
    tl_write_int32 (w, 0);          /* read_outbox_max_id */
    tl_write_int32 (w, unread);     /* unread_count */
    tl_write_int32 (w, 0);          /* unread_mentions_count */
    tl_write_int32 (w, 0);          /* unread_reactions_count */
    /* PeerNotifySettings with no flags. */
    tl_write_uint32(w, CRC_peerNotifySettings);
    tl_write_uint32(w, 0);
}

/* Build a messages.dialogsSlice with exactly ONE dialog. */
static size_t make_one_dialog_payload(uint8_t *buf, size_t max,
                                       uint32_t peer_crc, int64_t peer_id,
                                       int32_t top_msg, int32_t unread) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 1);                  /* count */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                  /* dialogs count */
    write_dialog(&w, peer_crc, peer_id, top_msg, unread);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

/* Build a messages.dialogsSlice with N dialogs (for v2 iteration test). */
static size_t make_multi_dialog_payload(uint8_t *buf, size_t max,
                                         int n_dialogs) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, n_dialogs);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, (uint32_t)n_dialogs);
    for (int i = 0; i < n_dialogs; i++) {
        write_dialog(&w, TL_peerUser, 1000 + i, 100 + i, i);
    }
    size_t res = w.len < max ? w.len : max;
    memcpy(buf, w.data, res);
    tl_writer_free(&w);
    return res;
}

static void test_dialogs_multi_entries(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[1024];
    size_t plen = make_multi_dialog_payload(payload, sizeof(payload), 5);
    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[10] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, entries, &count);
    ASSERT(rc == 0, "multi-entry dialogs parsed");
    ASSERT(count == 5, "all 5 dialogs iterated");
    ASSERT(entries[0].peer_id == 1000, "first peer id");
    ASSERT(entries[4].peer_id == 1004, "last peer id");
    ASSERT(entries[2].top_message_id == 102, "middle top_msg");
}

static void test_dialogs_single_user(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_one_dialog_payload(payload, sizeof(payload),
                                           TL_peerUser, 123456789LL, 42, 3);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[10] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, entries, &count);
    ASSERT(rc == 0, "dialogs: must succeed");
    ASSERT(count == 1, "one dialog parsed");
    ASSERT(entries[0].kind == DIALOG_PEER_USER, "peer kind=user");
    ASSERT(entries[0].peer_id == 123456789LL, "peer_id matches");
    ASSERT(entries[0].top_message_id == 42, "top_message_id matches");
    ASSERT(entries[0].unread_count == 3, "unread_count matches");
}

static void test_dialogs_single_channel(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_one_dialog_payload(payload, sizeof(payload),
                                           TL_peerChannel,
                                           -1001234567890LL, 17, 0);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[10] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, entries, &count);
    ASSERT(rc == 0, "dialogs: must succeed");
    ASSERT(count == 1, "one dialog parsed");
    ASSERT(entries[0].kind == DIALOG_PEER_CHANNEL, "peer kind=channel");
    ASSERT(entries[0].peer_id == -1001234567890LL, "peer_id matches");
}

static void test_dialogs_rpc_error(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 401);
    tl_write_string(&w, "AUTH_KEY_UNREGISTERED");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, entries, &count);
    ASSERT(rc != 0, "RPC error must propagate");
}

static void test_dialogs_unexpected_top(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[32];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xBADBADBAu);
    tl_write_uint32(&w, 0);
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, entries, &count);
    ASSERT(rc != 0, "unexpected constructor must fail");
}

static void test_dialogs_null_args(void) {
    DialogEntry e[1];
    int c = 0;
    ASSERT(domain_get_dialogs(NULL, NULL, NULL, 5, e, &c) == -1, "null cfg");
    ASSERT(domain_get_dialogs((ApiConfig *)1, NULL, NULL, 5, e, &c) == -1, "null s");
    /* max_entries <= 0 rejected */
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 0, e, &c) == -1, "zero limit");
}

void run_domain_dialogs_tests(void) {
    RUN_TEST(test_dialogs_multi_entries);
    RUN_TEST(test_dialogs_single_user);
    RUN_TEST(test_dialogs_single_channel);
    RUN_TEST(test_dialogs_rpc_error);
    RUN_TEST(test_dialogs_unexpected_top);
    RUN_TEST(test_dialogs_null_args);
}
