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
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
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

/* With the MessageMedia skipper, media-bearing messages now iterate. */
/* Message with messageMediaPhoto carrying a photoEmpty — parser should
 * populate media=MEDIA_PHOTO, media_id=photo_id. */
static void test_history_media_photo_info(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);

    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 9));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 77);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "check this");
    /* messageMediaPhoto with flags=0x01 (photo present) → photoEmpty#2331b22d id:long */
    tl_write_uint32(&w, 0x695150d7u);    /* messageMediaPhoto */
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, 0x2331b22du);    /* photoEmpty */
    tl_write_int64 (&w, 99999999LL);

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc == 0, "media photo info parsed");
    ASSERT(n == 1, "one entry");
    ASSERT(e[0].media == MEDIA_PHOTO, "kind=photo");
    ASSERT(e[0].media_id == 99999999LL, "photo_id captured");
    ASSERT(strcmp(e[0].text, "check this") == 0, "text preserved");
}

static void test_history_iterates_with_media_geo(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* Msg 1: flags.9 set + messageMediaGeo (empty geoPoint). */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 9));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 42);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "here");
    tl_write_uint32(&w, 0x56e0d474u); /* messageMediaGeo */
    tl_write_uint32(&w, 0x1117dd5fu); /* geoPointEmpty */

    /* Msg 2: plain */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 43);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000001);
    tl_write_string(&w, "there");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, e, &n);
    ASSERT(rc == 0, "iter with geo media");
    ASSERT(n == 2, "both messages iterate past media");
    ASSERT(e[0].id == 42 && strcmp(e[0].text, "here") == 0, "msg0");
    ASSERT(e[1].id == 43 && strcmp(e[1].text, "there") == 0, "msg1");
    ASSERT(e[0].complex == 0, "msg0 NOT complex after media skip");
}

static void test_history_stops_on_reply_markup(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 3);

    /* Msg 1: flags.6 (reply_markup) set — still no skipper, must bail. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 6));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 21);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "has reply_markup");

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
    ASSERT(rc == 0, "reply_markup entry still returned");
    ASSERT(n == 1, "only first captured before iteration stop");
    ASSERT(entries[0].complex == 1, "flagged complex");
    ASSERT(strcmp(entries[0].text, "has reply_markup") == 0, "text before bail");
}

/* After phase 3c, a well-formed reply_markup (inline keyboard) no longer
 * halts iteration — we can parse the message *and* continue to the
 * next one in the same response. */
static void test_history_iterates_with_reply_markup(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* Msg 1: flags.6 set + replyInlineMarkup with one URL button. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 6));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 210);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "check this bot");
    /* reply_markup */
    tl_write_uint32(&w, 0x48a30254u);            /* replyInlineMarkup */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x77608b83u);            /* keyboardButtonRow */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x258aff05u);            /* keyboardButtonUrl */
    tl_write_string(&w, "Docs");
    tl_write_string(&w, "https://example.com/docs");

    /* Msg 2: plain — must be reachable. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 211);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000001);
    tl_write_string(&w, "after the keyboard");

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, e, &n);
    ASSERT(rc == 0, "history parses past reply_markup");
    ASSERT(n == 2, "both messages iterate past keyboard");
    ASSERT(e[0].id == 210 && strcmp(e[0].text, "check this bot") == 0,
           "msg0 text");
    ASSERT(e[0].complex == 0, "msg0 NOT complex after keyboard skip");
    ASSERT(e[1].id == 211 && strcmp(e[1].text, "after the keyboard") == 0,
           "msg1 text");
}

/* Reactions (flags.20) with a simple results vector should also no
 * longer halt. */
static void test_history_iterates_with_reactions(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* Msg 1: flags.20 = reactions. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 20));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 310);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "popular message");
    /* messageReactions: flags=0, one emoji reaction. */
    tl_write_uint32(&w, 0x4f2b9479u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xa3d1cb80u);   /* reactionCount */
    tl_write_uint32(&w, 0);              /* flags */
    tl_write_uint32(&w, 0x1b2286b8u);   /* reactionEmoji */
    tl_write_string(&w, "\xf0\x9f\x94\xa5"); /* 🔥 */
    tl_write_int32 (&w, 42);

    /* Msg 2: plain. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 311);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000001);
    tl_write_string(&w, "next");

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, e, &n);
    ASSERT(rc == 0, "history parses past reactions");
    ASSERT(n == 2, "both messages iterate past reactions");
    ASSERT(e[0].id == 310 && strcmp(e[0].text, "popular message") == 0,
           "msg0 text");
    ASSERT(e[0].complex == 0, "msg0 NOT complex after reactions skip");
    ASSERT(e[1].id == 311 && strcmp(e[1].text, "next") == 0, "msg1 text");
}

/* Message carrying flags.23 (replies) + flags.22 (restriction_reason)
 * now iterates — both have skippers. */
static void test_history_iterates_with_replies_and_restriction(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* Msg 1: flags.23 + flags.22 set. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 23) | (1u << 22));
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 410);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "discussion post");
    /* messageReplies#83d60fc2: flags=0, replies=3, replies_pts=1 */
    tl_write_uint32(&w, 0x83d60fc2u);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 3);
    tl_write_int32 (&w, 1);
    /* restriction_reason: Vector<RestrictionReason>, 1 entry */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xd072acb4u);        /* restrictionReason */
    tl_write_string(&w, "android");
    tl_write_string(&w, "sensitive");
    tl_write_string(&w, "Age-restricted");

    /* Msg 2: plain. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 411);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000001);
    tl_write_string(&w, "next");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, e, &n);
    ASSERT(rc == 0, "history parses past replies + restriction_reason");
    ASSERT(n == 2, "both messages iterate");
    ASSERT(e[0].id == 410 && strcmp(e[0].text, "discussion post") == 0,
           "msg0 text");
    ASSERT(e[0].complex == 0, "msg0 NOT complex");
    ASSERT(e[1].id == 411 && strcmp(e[1].text, "next") == 0, "msg1 text");
}

/* Test that a media-only message (no caption text) has empty text and non-NONE
 * media — the fields relied on by the --no-media filter in cmd_history. */
static void test_history_media_only_has_empty_text(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);

    /* Message with flags.9 (media) set, empty caption string. */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, (1u << 9));   /* flags: media present, no out */
    tl_write_uint32(&w, 0);            /* flags2 */
    tl_write_int32 (&w, 500);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 100LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "");           /* empty caption */
    /* messageMediaPhoto with flags=0x01 (photo present) → photoEmpty */
    tl_write_uint32(&w, 0x695150d7u);  /* messageMediaPhoto */
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, 0x2331b22du);  /* photoEmpty */
    tl_write_int64 (&w, 88888888LL);

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc == 0,                  "media-only message parsed");
    ASSERT(n == 1,                   "one entry");
    ASSERT(e[0].media == MEDIA_PHOTO, "--no-media filter: media kind is PHOTO");
    ASSERT(e[0].text[0] == '\0',     "--no-media filter: text is empty for media-only");
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
    RUN_TEST(test_history_iterates_with_media_geo);
    RUN_TEST(test_history_media_photo_info);
    RUN_TEST(test_history_stops_on_reply_markup);
    RUN_TEST(test_history_iterates_with_reply_markup);
    RUN_TEST(test_history_iterates_with_reactions);
    RUN_TEST(test_history_iterates_with_replies_and_restriction);
    RUN_TEST(test_history_media_only_has_empty_text);
    RUN_TEST(test_history_null_args);
}
