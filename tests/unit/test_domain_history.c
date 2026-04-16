/**
 * @file test_domain_history.c
 * @brief Unit tests for domain_get_history_self (US-06 v1).
 */

#include "test_helpers.h"
#include "domain/read/history.h"
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
    out[0] = (uint8_t)(w.len / 4);
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
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

/* Build messages.messages containing one messageEmpty entry — enough to
 * exercise the parse prefix without wrestling with flag-conditional
 * Message fields. */
static size_t make_one_empty_message(uint8_t *buf, size_t max, int32_t id) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                /* vector count */
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);                /* flags = 0 */
    tl_write_int32 (&w, id);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_history_one_empty(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_one_empty_message(payload, sizeof(payload), 1234);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry entries[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, entries, &n);
    ASSERT(rc == 0, "history: must succeed");
    ASSERT(n == 1, "one entry parsed");
    ASSERT(entries[0].id == 1234, "id matches");
}

static void test_history_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();
    uint8_t payload[128];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "PEER_ID_INVALID");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc != 0, "RPC error must propagate");
}

/* Build a messages.channelMessages response with one messageEmpty. */
static size_t make_channel_messages(uint8_t *buf, size_t max, int32_t id) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_channelMessages);
    tl_write_uint32(&w, 0);   /* flags */
    tl_write_int32 (&w, 100); /* pts */
    tl_write_int32 (&w, 1);   /* count */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);   /* flags */
    tl_write_int32 (&w, id);
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_history_channel_peer(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_channel_messages(payload, sizeof(payload), 9999);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryPeer peer = {
        .kind = HISTORY_PEER_CHANNEL,
        .peer_id = 1001234567890LL,
        .access_hash = 0xABCDEF1234567890LL,
    };
    HistoryEntry entries[3] = {0}; int n = 0;
    int rc = domain_get_history(&cfg, &s, &t, &peer, 0, 3, entries, &n);
    ASSERT(rc == 0, "channel history parsed");
    ASSERT(n == 1, "one entry");
    ASSERT(entries[0].id == 9999, "id matches");
}

/* Build a simple Message (no complex flags) with text = "hello" and
 * date=1700000000. Flags include out (bit 1) only. */
static size_t make_simple_text_message(uint8_t *buf, size_t max,
                                         int32_t id, const char *text) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 1));   /* flags: out */
    tl_write_uint32(&w, 0);            /* flags2 */
    tl_write_int32 (&w, id);
    /* No from_id (flags.8 not set). */
    tl_write_uint32(&w, TL_peerUser); /* peer_id */
    tl_write_int64 (&w, 123LL);
    tl_write_int32 (&w, 1700000000);   /* date */
    tl_write_string(&w, text);
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_history_text_extraction(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[512];
    size_t plen = make_simple_text_message(payload, sizeof(payload),
                                             55, "hello world");
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc == 0, "simple message parsed");
    ASSERT(n == 1, "one entry");
    ASSERT(e[0].id == 55, "id matches");
    ASSERT(e[0].out == 1, "out flag set");
    ASSERT(e[0].date == 1700000000, "date extracted");
    ASSERT(strcmp(e[0].text, "hello world") == 0, "text extracted");
    ASSERT(e[0].complex == 0, "not complex");
}

static void test_history_complex_flag(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* Message with fwd_from flag set — we should mark complex and not
     * try to parse further. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 2)); /* flags: fwd_from */
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 77);
    /* No further bytes needed — parser bails on complex mask */
    uint8_t payload[128]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc == 0, "complex message parsed (without text)");
    ASSERT(n == 1, "one entry");
    ASSERT(e[0].id == 77, "id still captured");
    ASSERT(e[0].complex == 1, "complex flag set");
    ASSERT(e[0].text[0] == '\0', "text empty for complex");
}

/* Write a simple text Message body only (no top-level wrapper). */
static void write_simple_message(TlWriter *w, int32_t id, const char *text) {
    tl_write_uint32(w, TL_message);
    tl_write_uint32(w, 0);             /* flags: no out, no from_id */
    tl_write_uint32(w, 0);             /* flags2 */
    tl_write_int32 (w, id);
    tl_write_uint32(w, TL_peerUser);   /* peer_id */
    tl_write_int64 (w, 100LL);
    tl_write_int32 (w, 1700000000);    /* date */
    tl_write_string(w, text);
}

static void test_history_iterates_multiple(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* messages.messages + Vector<Message> with 3 simple messages. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 3);
    write_simple_message(&w, 1, "first");
    write_simple_message(&w, 2, "second");
    write_simple_message(&w, 3, "third");
    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry entries[10] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 10, entries, &n);
    ASSERT(rc == 0, "iteration ok");
    ASSERT(n == 3, "all 3 messages parsed");
    ASSERT(entries[0].id == 1, "id0");
    ASSERT(strcmp(entries[0].text, "first") == 0, "text0");
    ASSERT(entries[1].id == 2, "id1");
    ASSERT(strcmp(entries[1].text, "second") == 0, "text1");
    ASSERT(entries[2].id == 3, "id2");
    ASSERT(strcmp(entries[2].text, "third") == 0, "text2");
}

static void test_history_iterates_with_entities(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* Msg 1: with entities (flags.7). */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 7));        /* flags: entities present */
    tl_write_uint32(&w, 0);                 /* flags2 */
    tl_write_int32 (&w, 11);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "bold message");
    /* entities vector: 1 bold */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xbd610bc9u);       /* messageEntityBold */
    tl_write_int32 (&w, 0);                 /* offset */
    tl_write_int32 (&w, 4);                 /* length */

    /* Msg 2: plain. */
    write_simple_message(&w, 12, "plain");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry entries[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, entries, &n);
    ASSERT(rc == 0, "entities iter ok");
    ASSERT(n == 2, "both messages parsed despite entities");
    ASSERT(strcmp(entries[0].text, "bold message") == 0, "text0");
    ASSERT(strcmp(entries[1].text, "plain") == 0, "text1");
}

static void test_history_stops_on_media(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 3);

    /* Msg 1: with media (flags.9) — should capture text but stop iteration. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 9));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 21);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "has media");

    /* Msg 2 & 3 would be here but unreachable. */

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry entries[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, entries, &n);
    ASSERT(rc == 0, "media entry still returned");
    ASSERT(n == 1, "only first captured before iteration stop");
    ASSERT(entries[0].complex == 1, "flagged complex");
    ASSERT(strcmp(entries[0].text, "has media") == 0, "text before bail");
}

static void test_history_null_args(void) {
    HistoryEntry e[1]; int n = 0;
    ASSERT(domain_get_history_self(NULL, NULL, NULL, 0, 5, e, &n) == -1,
           "null args rejected");
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 0, e, &n) == -1,
           "limit=0 rejected");
}

void run_domain_history_tests(void) {
    RUN_TEST(test_history_one_empty);
    RUN_TEST(test_history_rpc_error);
    RUN_TEST(test_history_channel_peer);
    RUN_TEST(test_history_text_extraction);
    RUN_TEST(test_history_complex_flag);
    RUN_TEST(test_history_iterates_multiple);
    RUN_TEST(test_history_iterates_with_entities);
    RUN_TEST(test_history_stops_on_media);
    RUN_TEST(test_history_null_args);
}
