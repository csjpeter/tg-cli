/**
 * @file test_history_rich_metadata.c
 * @brief TEST-79 — functional coverage for rich message metadata.
 *
 * US-28 identifies five Message flag branches in
 * `src/domain/read/history.c` that the existing read-path suite never
 * feeds: fwd_from (flags.2), via_bot_id (flags.11), reply_to (flags.3),
 * via_business_bot_id (flags2.0), and saved_peer_id (flags.28). Each
 * branch has a skipper in tl_skip.c that must execute for the parser
 * to surface the message body rather than drop the row.
 *
 * These scenarios craft real messages.Messages payloads carrying each
 * flag combination, drive the production domain_get_history() through
 * the in-process mock server, and assert that id / date / text still
 * arrive intact. Full "[fwd from @channel]" rendering is US-28 scope;
 * this suite validates the parse-through contract that unlocks it.
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

#include "domain/read/history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not re-exposed from public headers ---- */
#define CRC_messages_getHistory 0x4423e6c5U
#define CRC_messageFwdHeader    0x4e4df4bbU
#define CRC_messageReplyHeader  0xafbc09dbU

/* Message flag bits tested here. */
#define MSG_FLAG_FWD_FROM       (1u <<  2)
#define MSG_FLAG_REPLY_TO       (1u <<  3)
#define MSG_FLAG_FROM_ID        (1u <<  8)
#define MSG_FLAG_VIA_BOT        (1u << 11)
#define MSG_FLAG_SAVED_PEER     (1u << 28)
#define MSG2_FLAG_VIA_BIZ_BOT   (1u <<  0)

/* Fwd-header flag bits used below. */
#define FWD_HAS_FROM_ID         (1u << 0)
#define FWD_HAS_FROM_NAME       (1u << 5)

/* Reply-header flag bits used below. */
#define REPLY_HAS_MSG_ID        (1u << 4)

/* ---- boilerplate ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-hist-rich-%s", tag);
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
    ASSERT(session_store_load(s, &dc) == 0, "load session");
}

/* Envelope: messages.messages { messages: Vector<Message>{1}, chats, users }
 * with the caller providing the inner message bytes (starting at TL_message). */
static void wrap_messages_messages(TlWriter *w, const uint8_t *msg_bytes,
                                    size_t msg_len) {
    tl_write_uint32(w, TL_messages_messages);
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    tl_write_raw(w, msg_bytes, msg_len);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* chats */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* users */
}

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

/* Shared pre-text layout (no from_id unless FROM_ID flag set):
 *   TL_message | flags | flags2 | id(i32)
 *   [from_id:Peer  if flags.8]
 *   peer_id:Peer (peerUser 1)
 *   [saved_peer_id:Peer  if flags.28]
 *   [fwd_from:MessageFwdHeader  if flags.2]
 *   [via_bot_id:i64  if flags.11]
 *   [via_business_bot_id:i64  if flags2.0]
 *   [reply_to:MessageReplyHeader  if flags.3]
 *   date(i32)  message:string
 */

/* Test 1 — fwd_from with a channel peer (from_id variant). */
static void on_history_fwd_from_channel(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_FWD_FROM);
    tl_write_uint32(&inner, 0);                /* flags2 */
    tl_write_int32 (&inner, 10347);            /* id */
    tl_write_uint32(&inner, TL_peerUser);      /* peer_id */
    tl_write_int64 (&inner, 1LL);
    /* messageFwdHeader flags: from_id=peerChannel */
    tl_write_uint32(&inner, CRC_messageFwdHeader);
    tl_write_uint32(&inner, FWD_HAS_FROM_ID);
    tl_write_uint32(&inner, TL_peerChannel);   /* from_id */
    tl_write_int64 (&inner, 12345678LL);
    tl_write_int32 (&inner, 1700000123);       /* fwd date */
    tl_write_int32 (&inner, 1700000200);       /* outer date */
    tl_write_string(&inner, "fresh news");     /* message */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 2 — fwd_from with a hidden-user from_name (string, not peer). */
static void on_history_fwd_from_hidden(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_FWD_FROM);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 10348);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    /* fwd header with only from_name (flag 5). */
    tl_write_uint32(&inner, CRC_messageFwdHeader);
    tl_write_uint32(&inner, FWD_HAS_FROM_NAME);
    tl_write_string(&inner, "HiddenSender");   /* from_name */
    tl_write_int32 (&inner, 1700000500);       /* fwd date */
    tl_write_int32 (&inner, 1700000600);       /* outer date */
    tl_write_string(&inner, "hidden fwd body");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 3 — reply_to referencing msg_id=12345. */
static void on_history_reply_to(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_REPLY_TO);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 12346);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    /* messageReplyHeader with reply_to_msg_id. */
    tl_write_uint32(&inner, CRC_messageReplyHeader);
    tl_write_uint32(&inner, REPLY_HAS_MSG_ID);
    tl_write_int32 (&inner, 12345);            /* reply_to_msg_id */
    tl_write_int32 (&inner, 1700000800);       /* date */
    tl_write_string(&inner, "yes");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 4 — via_bot_id (e.g. @gif). */
static void on_history_via_bot(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_VIA_BOT);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 20001);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    tl_write_int64 (&inner, 7777001LL);        /* via_bot_id */
    tl_write_int32 (&inner, 1700001000);       /* date */
    tl_write_string(&inner, "<gif>");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 5 — via_business_bot_id (flags2 bit 0). */
static void on_history_via_business_bot(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, 0);                /* flags */
    tl_write_uint32(&inner, MSG2_FLAG_VIA_BIZ_BOT); /* flags2 */
    tl_write_int32 (&inner, 20101);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    tl_write_int64 (&inner, 8888001LL);        /* via_business_bot_id */
    tl_write_int32 (&inner, 1700001100);
    tl_write_string(&inner, "auto reply");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 6 — saved_peer_id (flags.28). */
static void on_history_saved_peer(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_SAVED_PEER);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 30001);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    tl_write_uint32(&inner, TL_peerUser);      /* saved_peer_id */
    tl_write_int64 (&inner, 42LL);
    tl_write_int32 (&inner, 1700001200);
    tl_write_string(&inner, "topic-A msg");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 7 — reply_to AND via_bot on the same message. */
static void on_history_reply_and_via_bot(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_REPLY_TO | MSG_FLAG_VIA_BOT);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 40001);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    /* via_bot_id comes BEFORE reply_to in schema order. */
    tl_write_int64 (&inner, 9990001LL);
    tl_write_uint32(&inner, CRC_messageReplyHeader);
    tl_write_uint32(&inner, REPLY_HAS_MSG_ID);
    tl_write_int32 (&inner, 39000);
    tl_write_int32 (&inner, 1700001300);
    tl_write_string(&inner, "combo reply");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Test 8 — via_bot_id set but id not resolvable (no users vector entry).
 * Parser must still land the line with text intact, not drop it. */
static void on_history_via_bot_unresolvable(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, TL_message);
    tl_write_uint32(&inner, MSG_FLAG_VIA_BOT);
    tl_write_uint32(&inner, 0);
    tl_write_int32 (&inner, 50001);
    tl_write_uint32(&inner, TL_peerUser);
    tl_write_int64 (&inner, 1LL);
    tl_write_int64 (&inner, 424242LL);         /* unknown bot id */
    tl_write_int32 (&inner, 1700001400);
    tl_write_string(&inner, "orphan bot msg");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

static void test_forwarded_from_channel_labelled(void) {
    with_tmp_home("fwd-chan");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_fwd_from_channel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "get_history with fwd_from channel succeeds");
    ASSERT(n == 1, "one fwd message parsed");
    ASSERT(rows[0].id == 10347, "id preserved past fwd header");
    ASSERT(rows[0].date == 1700000200, "outer date preserved");
    ASSERT(strcmp(rows[0].text, "fresh news") == 0,
           "text after fwd_from skipped correctly");
    ASSERT(rows[0].complex == 0, "not flagged complex — fwd header fully skipped");

    transport_close(&t);
    mt_server_reset();
}

static void test_forwarded_from_hidden_user_labelled(void) {
    with_tmp_home("fwd-hidden");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_fwd_from_hidden, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "get_history with fwd from_name succeeds");
    ASSERT(n == 1, "hidden-sender fwd preserved");
    ASSERT(rows[0].id == 10348, "id preserved");
    ASSERT(strcmp(rows[0].text, "hidden fwd body") == 0,
           "text after from_name string skipped correctly");
    ASSERT(rows[0].complex == 0, "from_name path does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_reply_to_labelled_with_msg_id(void) {
    with_tmp_home("reply-to");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_reply_to, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "reply_to parse succeeds");
    ASSERT(n == 1, "reply-to message preserved");
    ASSERT(rows[0].id == 12346, "id preserved");
    ASSERT(rows[0].date == 1700000800, "outer date preserved");
    ASSERT(strcmp(rows[0].text, "yes") == 0,
           "text after reply header skipped correctly");
    ASSERT(rows[0].complex == 0, "reply_to branch does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_via_bot_labelled_with_username(void) {
    with_tmp_home("via-bot");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_via_bot, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "via_bot parse succeeds");
    ASSERT(n == 1, "via-bot message preserved");
    ASSERT(rows[0].id == 20001, "id preserved past via_bot int64");
    ASSERT(strcmp(rows[0].text, "<gif>") == 0,
           "text after via_bot_id i64 skipped correctly");
    ASSERT(rows[0].complex == 0, "via_bot branch does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_via_business_bot_labelled(void) {
    with_tmp_home("via-bizbot");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory,
                     on_history_via_business_bot, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "via_business_bot parse succeeds");
    ASSERT(n == 1, "biz-bot message preserved");
    ASSERT(rows[0].id == 20101, "id preserved past flags2.0 i64");
    ASSERT(strcmp(rows[0].text, "auto reply") == 0,
           "text after via_business_bot_id skipped correctly");
    ASSERT(rows[0].complex == 0, "flags2.0 branch does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_saved_peer_id_suffix(void) {
    with_tmp_home("saved-peer");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_saved_peer, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "saved_peer_id parse succeeds");
    ASSERT(n == 1, "saved-peer message preserved");
    ASSERT(rows[0].id == 30001, "id preserved past saved_peer_id skipper");
    ASSERT(strcmp(rows[0].text, "topic-A msg") == 0,
           "text after saved_peer_id skipped correctly");
    ASSERT(rows[0].complex == 0, "flags.28 branch does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_multiple_flags_all_rendered(void) {
    with_tmp_home("combo");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory,
                     on_history_reply_and_via_bot, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "reply+via_bot combo parse succeeds");
    ASSERT(n == 1, "combo message preserved");
    ASSERT(rows[0].id == 40001, "id preserved with two flag branches");
    ASSERT(strcmp(rows[0].text, "combo reply") == 0,
           "text survives after via_bot + reply header sequence");
    ASSERT(rows[0].complex == 0, "combo does not bail");

    transport_close(&t);
    mt_server_reset();
}

static void test_unresolvable_bot_falls_back_to_raw_id(void) {
    with_tmp_home("unresolvable-bot");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory,
                     on_history_via_bot_unresolvable, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "unresolved bot parse succeeds");
    ASSERT(n == 1, "message not dropped when bot id cannot be resolved");
    ASSERT(rows[0].id == 50001, "id preserved for orphan bot line");
    ASSERT(strcmp(rows[0].text, "orphan bot msg") == 0,
           "text preserved — graceful degradation, not drop");
    ASSERT(rows[0].complex == 0, "orphan-bot path does not flag complex");

    transport_close(&t);
    mt_server_reset();
}

void run_history_rich_metadata_tests(void) {
    RUN_TEST(test_forwarded_from_channel_labelled);
    RUN_TEST(test_forwarded_from_hidden_user_labelled);
    RUN_TEST(test_reply_to_labelled_with_msg_id);
    RUN_TEST(test_via_bot_labelled_with_username);
    RUN_TEST(test_via_business_bot_labelled);
    RUN_TEST(test_saved_peer_id_suffix);
    RUN_TEST(test_multiple_flags_all_rendered);
    RUN_TEST(test_unresolvable_bot_falls_back_to_raw_id);
}
