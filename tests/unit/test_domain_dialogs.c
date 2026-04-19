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
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
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

/* P5-08: build a full messages.dialogsSlice with 1 dialog (user peer),
 * 1 message, 0 chats, 1 user with first_name+last_name+username, and
 * verify the DialogEntry gets the user's name + username populated. */
static void test_dialogs_title_join_user(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    write_dialog(&w, TL_peerUser, 7777LL, 42, 3);

    /* messages vector: 1 simple message for peer_id=7777 */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 42);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 7777LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "msg");

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users vector: one user with first_name + last_name + username */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    uint32_t uflags = (1u << 1) | (1u << 2) | (1u << 3);
    tl_write_uint32(&w, uflags);
    tl_write_uint32(&w, 0);            /* flags2 */
    tl_write_int64 (&w, 7777LL);
    tl_write_string(&w, "Alice");
    tl_write_string(&w, "Smith");
    tl_write_string(&w, "alice_s");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0, entries, &count, NULL);
    ASSERT(rc == 0, "title join ok");
    ASSERT(count == 1, "one dialog");
    ASSERT(entries[0].peer_id == 7777LL, "peer_id");
    ASSERT(strcmp(entries[0].title, "Alice Smith") == 0, "joined name");
    ASSERT(strcmp(entries[0].username, "alice_s") == 0, "username");
}

/* TUI-08: access_hash from a user with flags.0 set is threaded onto DialogEntry. */
static void test_dialogs_user_access_hash_threaded(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    write_dialog(&w, TL_peerUser, 42LL, 1, 0);

    /* messages vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users vector: one user with access_hash + first_name + username */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    uint32_t uflags = (1u << 0) | (1u << 1) | (1u << 3);
    tl_write_uint32(&w, uflags);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 42LL);
    tl_write_int64 (&w, 0xFEEDFACEDEADBEEFLL);
    tl_write_string(&w, "Bob");
    tl_write_string(&w, "bob");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0, entries, &count, NULL);
    ASSERT(rc == 0, "dialog with access_hash ok");
    ASSERT(count == 1, "one dialog");
    ASSERT(entries[0].have_access_hash == 1, "access_hash threaded");
    ASSERT(entries[0].access_hash == (int64_t)0xFEEDFACEDEADBEEFLL,
           "access_hash value");
}

/* TUI-08: channel access_hash is threaded too. */
static void test_dialogs_channel_access_hash_threaded(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    write_dialog(&w, TL_peerChannel, -1001234567LL, 1, 0);

    /* messages vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector: one channel with access_hash */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_channel);
    uint32_t cflags = (1u << 13);          /* access_hash */
    tl_write_uint32(&w, cflags);
    tl_write_uint32(&w, 0);                /* flags2 */
    tl_write_int64 (&w, -1001234567LL);
    tl_write_int64 (&w, 0xAABBCCDDEEFF0011LL);
    tl_write_string(&w, "Chan");
    tl_write_uint32(&w, 0x37c1011cu);      /* chatPhotoEmpty */
    tl_write_int32 (&w, 1700000000);

    /* users vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0, entries, &count, NULL);
    ASSERT(rc == 0, "channel dialog ok");
    ASSERT(count == 1, "one dialog");
    ASSERT(entries[0].kind == DIALOG_PEER_CHANNEL, "channel kind");
    ASSERT(entries[0].have_access_hash == 1, "channel access_hash threaded");
    ASSERT(entries[0].access_hash == (int64_t)0xAABBCCDDEEFF0011LL,
           "channel access_hash value");
}

static void test_dialogs_multi_entries(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    uint8_t payload[1024];
    size_t plen = make_multi_dialog_payload(payload, sizeof(payload), 5);
    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[10] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, 0, entries, &count, NULL);
    ASSERT(rc == 0, "multi-entry dialogs parsed");
    ASSERT(count == 5, "all 5 dialogs iterated");
    ASSERT(entries[0].peer_id == 1000, "first peer id");
    ASSERT(entries[4].peer_id == 1004, "last peer id");
    ASSERT(entries[2].top_message_id == 102, "middle top_msg");
}

static void test_dialogs_single_user(void) {
    mock_socket_reset();
    mock_crypto_reset();
    dialogs_cache_flush();

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
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, 0, entries, &count, NULL);
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
    dialogs_cache_flush();

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
    int rc = domain_get_dialogs(&cfg, &s, &t, 10, 0, entries, &count, NULL);
    ASSERT(rc == 0, "dialogs: must succeed");
    ASSERT(count == 1, "one dialog parsed");
    ASSERT(entries[0].kind == DIALOG_PEER_CHANNEL, "peer kind=channel");
    ASSERT(entries[0].peer_id == -1001234567890LL, "peer_id matches");
}

static void test_dialogs_rpc_error(void) {
    mock_socket_reset();
    mock_crypto_reset();
    dialogs_cache_flush();

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
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0, entries, &count, NULL);
    ASSERT(rc != 0, "RPC error must propagate");
}

static void test_dialogs_unexpected_top(void) {
    mock_socket_reset();
    mock_crypto_reset();
    dialogs_cache_flush();

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
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0, entries, &count, NULL);
    ASSERT(rc != 0, "unexpected constructor must fail");
}

static void test_dialogs_null_args(void) {
    DialogEntry e[1];
    int c = 0;
    ASSERT(domain_get_dialogs(NULL, NULL, NULL, 5, 0, e, &c, NULL) == -1, "null cfg");
    ASSERT(domain_get_dialogs((ApiConfig *)1, NULL, NULL, 5, 0, e, &c, NULL) == -1, "null s");
    /* max_entries <= 0 rejected */
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 0, 0, e, &c, NULL) == -1, "zero limit");
}

/* Wire-inspection: when archived=1, folder_id=1 (flags bit 1 set) must appear
 * in the outbound buffer before the response is processed.
 *
 * messages.getDialogs with archived:
 *   CRC     0xa0f4cb4f  (LE: 4f cb f4 a0)
 *   flags   0x00000002  (bit 1 = folder_id present)
 *   folder_id 0x00000001
 *   ...
 *
 * We scan the raw sent buffer for the 4-byte little-endian sequence
 * {0x02, 0x00, 0x00, 0x00} immediately following the CRC, and then
 * {0x01, 0x00, 0x00, 0x00} as folder_id. Because the mock AES is the
 * identity cipher the TL bytes appear verbatim in the sent buffer.
 */
static void test_dialogs_archived_folder_id_on_wire(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    /* Minimal valid response: empty messages.dialogs */
    TlWriter pw; tl_writer_init(&pw);
    tl_write_uint32(&pw, TL_messages_dialogs);
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0); /* dialogs */
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0); /* messages */
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0); /* chats */
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0); /* users */
    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(pw.data, pw.len, resp, &rlen);
    tl_writer_free(&pw);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 1 /* archived */, entries, &count, NULL);
    ASSERT(rc == 0, "archived dialogs: call succeeds");

    /* Inspect the wire bytes for: CRC(4) + flags=2(4) + folder_id=1(4) */
    static const uint8_t crc_le[4]       = {0x4f, 0xcb, 0xf4, 0xa0};
    static const uint8_t flags_le[4]     = {0x02, 0x00, 0x00, 0x00};
    static const uint8_t folder_id_le[4] = {0x01, 0x00, 0x00, 0x00};

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent != NULL && sent_len > 0, "client transmitted bytes");

    int found = 0;
    for (size_t i = 0; i + 12 <= sent_len; i++) {
        if (memcmp(sent + i,      crc_le,       4) == 0 &&
            memcmp(sent + i + 4,  flags_le,     4) == 0 &&
            memcmp(sent + i + 8,  folder_id_le, 4) == 0) {
            found = 1;
            break;
        }
    }
    ASSERT(found, "folder_id=1 appears on wire when archived=1");
}

/* Inverse: when archived=0, flags=0 and no folder_id in the outbound buffer. */
static void test_dialogs_default_no_folder_id_on_wire(void) {
    mock_socket_reset(); mock_crypto_reset(); dialogs_cache_flush();

    TlWriter pw; tl_writer_init(&pw);
    tl_write_uint32(&pw, TL_messages_dialogs);
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0);
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0);
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0);
    tl_write_uint32(&pw, TL_vector); tl_write_uint32(&pw, 0);
    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(pw.data, pw.len, resp, &rlen);
    tl_writer_free(&pw);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    DialogEntry entries[5] = {0};
    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, 5, 0 /* not archived */, entries, &count, NULL);
    ASSERT(rc == 0, "default dialogs: call succeeds");

    /* flags must be 0 (not 2) right after the CRC */
    static const uint8_t crc_le[4]        = {0x4f, 0xcb, 0xf4, 0xa0};
    static const uint8_t flags_zero_le[4] = {0x00, 0x00, 0x00, 0x00};

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent != NULL && sent_len > 0, "client transmitted bytes");

    int found = 0;
    for (size_t i = 0; i + 8 <= sent_len; i++) {
        if (memcmp(sent + i,     crc_le,        4) == 0 &&
            memcmp(sent + i + 4, flags_zero_le, 4) == 0) {
            found = 1;
            break;
        }
    }
    ASSERT(found, "flags=0 (no folder_id) on wire when archived=0");
}

void run_domain_dialogs_tests(void) {
    RUN_TEST(test_dialogs_title_join_user);
    RUN_TEST(test_dialogs_user_access_hash_threaded);
    RUN_TEST(test_dialogs_channel_access_hash_threaded);
    RUN_TEST(test_dialogs_multi_entries);
    RUN_TEST(test_dialogs_single_user);
    RUN_TEST(test_dialogs_single_channel);
    RUN_TEST(test_dialogs_rpc_error);
    RUN_TEST(test_dialogs_unexpected_top);
    RUN_TEST(test_dialogs_null_args);
    RUN_TEST(test_dialogs_archived_folder_id_on_wire);
    RUN_TEST(test_dialogs_default_no_folder_id_on_wire);
}
