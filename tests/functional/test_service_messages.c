/**
 * @file test_service_messages.c
 * @brief TEST-80 — functional coverage for messageService action variants.
 *
 * US-29 identifies seventeen `messageAction*` constructors whose wire
 * shape was previously dropped by history.c (messageService → complex=1).
 * The domain now renders each into a human-readable string so that group
 * histories carry their first-class events (join/leave/pin/video-chat
 * lifecycle/...).
 *
 * These scenarios craft a real messageService-shaped messages.Messages
 * payload for each action, drive domain_get_history() through the
 * in-process mock server, and assert the surfaced string fragment
 * matches the US-29 table. Unknown action CRCs fall through to a
 * "[service action 0x%08x]" label that keeps forward compatibility.
 *
 * A final scenario plumbs a messageService through updates.difference
 * (the `watch` code path) to prove service events reach the poll loop
 * and are no longer filtered out as complex.
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
#include "domain/read/updates.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not re-exposed from public headers ---- */
#define CRC_messages_getHistory   0x4423e6c5U
#define CRC_updates_getDifference 0x19c2f763U
#define CRC_messageReplyHeader    0xafbc09dbU

/* ---- MessageAction CRCs (duplicated locally so the test keeps working
 *      even if history.c renames them). */
#define AC_Empty                 0xb6aef7b0U
#define AC_ChatCreate            0xbd47cbadU
#define AC_ChatEditTitle         0xb5a1ce5aU
#define AC_ChatEditPhoto         0x7fcb13a8U
#define AC_ChatDeletePhoto       0x95e3fbefU
#define AC_ChatAddUser           0x15cefd00U
#define AC_ChatDeleteUser        0xa43f30ccU
#define AC_ChatJoinedByLink      0x031224c3U
#define AC_ChannelCreate         0x95d2ac92U
#define AC_ChatMigrateTo         0xe1037f92U
#define AC_ChannelMigrateFrom    0xea3948e9U
#define AC_PinMessage            0x94bd38edU
#define AC_HistoryClear          0x9fbab604U
#define AC_PhoneCall             0x80e11a7fU
#define AC_ScreenshotTaken       0x4792929bU
#define AC_CustomAction          0xfae69f56U
#define AC_GroupCall             0x7a0d7f42U
#define AC_GroupCallScheduled    0xb3a07661U
#define AC_InviteToGroupCall     0x502f92f7U

/* PhoneCall discard reasons. */
#define DR_Missed      0x85e42301U
#define DR_Disconnect  0xe095c1a0U
#define DR_Hangup      0x57adc690U
#define DR_Busy        0xfaf7e8c9U

/* inputGroupCall#d8aa840f id:long access_hash:long */
#define CRC_inputGroupCall 0xd8aa840fU

/* Message flag bits used for messageService here. */
#define MS_FLAG_REPLY_TO        (1u << 3)

/* Reply-header flag bits. */
#define REPLY_HAS_MSG_ID        (1u << 4)

/* ================================================================ */
/* Boilerplate (mirrors test_history_rich_metadata.c)               */
/* ================================================================ */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-svc-%s", tag);
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
 * with the caller providing the inner messageService bytes (starting at
 * TL_messageService). */
static void wrap_messages_messages(TlWriter *w, const uint8_t *msg_bytes,
                                    size_t msg_len) {
    tl_write_uint32(w, TL_messages_messages);
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    tl_write_raw(w, msg_bytes, msg_len);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* chats */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* users */
}

/* Build the messageService envelope preamble:
 *   TL_messageService | flags | id(i32) | peer_id(peerUser 1)
 * The caller then writes: [reply_to if flags.3] date action.
 * messageService on the current schema does NOT carry a flags2 field. */
static void write_service_preamble(TlWriter *w, uint32_t flags,
                                    int32_t id) {
    tl_write_uint32(w, TL_messageService);
    tl_write_uint32(w, flags);
    tl_write_int32 (w, id);
    tl_write_uint32(w, TL_peerUser);
    tl_write_int64 (w, 1LL);
}

/* Fetch one messageService row through the production domain. */
static void fetch_one(HistoryEntry *out_row) {
    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);
    MtProtoSession s; load_session(&s);
    HistoryEntry rows[2];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 2, rows, &n) == 0,
           "get_history succeeds for service row");
    ASSERT(n == 1, "exactly one service message parsed");
    *out_row = rows[0];
    transport_close(&t);
}

/* ================================================================ */
/* Action-specific responders                                       */
/* ================================================================ */

static void on_chat_create(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1001);
    tl_write_int32 (&inner, 1700010000);         /* date */
    tl_write_uint32(&inner, AC_ChatCreate);
    tl_write_string(&inner, "Planning");         /* title */
    tl_write_uint32(&inner, TL_vector);
    tl_write_uint32(&inner, 2);                  /* 2 users */
    tl_write_int64 (&inner, 100LL);
    tl_write_int64 (&inner, 200LL);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_add_user(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1002);
    tl_write_int32 (&inner, 1700010100);
    tl_write_uint32(&inner, AC_ChatAddUser);
    tl_write_uint32(&inner, TL_vector);
    tl_write_uint32(&inner, 1);
    tl_write_int64 (&inner, 4242LL);             /* added user id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_delete_user(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1003);
    tl_write_int32 (&inner, 1700010200);
    tl_write_uint32(&inner, AC_ChatDeleteUser);
    tl_write_int64 (&inner, 4242LL);             /* user_id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_joined_by_link(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1004);
    tl_write_int32 (&inner, 1700010300);
    tl_write_uint32(&inner, AC_ChatJoinedByLink);
    tl_write_int64 (&inner, 9999LL);             /* inviter_id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_edit_title(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1005);
    tl_write_int32 (&inner, 1700010400);
    tl_write_uint32(&inner, AC_ChatEditTitle);
    tl_write_string(&inner, "Shipping");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* messageActionChatEditPhoto carries a photo:Photo body. For our test we
 * use the photoEmpty#2331b22d variant (crc + id:long) — just 12 bytes.
 * The renderer only cares that the photo skipper advances past it. */
static void on_chat_edit_photo(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1006);
    tl_write_int32 (&inner, 1700010500);
    tl_write_uint32(&inner, AC_ChatEditPhoto);
    tl_write_uint32(&inner, 0x2331b22dU);        /* photoEmpty */
    tl_write_int64 (&inner, 55555LL);            /* photo_id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Pinned target id travels on reply_to (MessageReplyHeader).
 * Layout after (flags, id, peer):
 *   reply_to = messageReplyHeader flags=REPLY_HAS_MSG_ID
 *                  reply_to_msg_id = 12345
 *   date
 *   action = messageActionPinMessage
 */
static void on_pin_message(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, MS_FLAG_REPLY_TO, 1007);
    /* reply_to */
    tl_write_uint32(&inner, CRC_messageReplyHeader);
    tl_write_uint32(&inner, REPLY_HAS_MSG_ID);
    tl_write_int32 (&inner, 12345);
    /* date + action */
    tl_write_int32 (&inner, 1700010600);
    tl_write_uint32(&inner, AC_PinMessage);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_history_clear(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1008);
    tl_write_int32 (&inner, 1700010700);
    tl_write_uint32(&inner, AC_HistoryClear);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_channel_create(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1009);
    tl_write_int32 (&inner, 1700010800);
    tl_write_uint32(&inner, AC_ChannelCreate);
    tl_write_string(&inner, "Releases");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_channel_migrate_from(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1010);
    tl_write_int32 (&inner, 1700010900);
    tl_write_uint32(&inner, AC_ChannelMigrateFrom);
    tl_write_string(&inner, "OldGroup");
    tl_write_int64 (&inner, 77777LL);            /* chat_id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_migrate_to(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1011);
    tl_write_int32 (&inner, 1700011000);
    tl_write_uint32(&inner, AC_ChatMigrateTo);
    tl_write_int64 (&inner, 88888LL);            /* channel_id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* messageActionGroupCall flags=0, call=inputGroupCall. */
static void on_group_call(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1012);
    tl_write_int32 (&inner, 1700011100);
    tl_write_uint32(&inner, AC_GroupCall);
    tl_write_uint32(&inner, 0);                  /* flags */
    tl_write_uint32(&inner, CRC_inputGroupCall);
    tl_write_int64 (&inner, 111LL);
    tl_write_int64 (&inner, 222LL);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_group_call_scheduled(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1013);
    tl_write_int32 (&inner, 1700011200);
    tl_write_uint32(&inner, AC_GroupCallScheduled);
    tl_write_uint32(&inner, CRC_inputGroupCall);
    tl_write_int64 (&inner, 111LL);
    tl_write_int64 (&inner, 222LL);
    tl_write_int32 (&inner, 1700020000);         /* schedule_date */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_invite_to_group_call(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1014);
    tl_write_int32 (&inner, 1700011300);
    tl_write_uint32(&inner, AC_InviteToGroupCall);
    tl_write_uint32(&inner, CRC_inputGroupCall);
    tl_write_int64 (&inner, 111LL);
    tl_write_int64 (&inner, 222LL);
    tl_write_uint32(&inner, TL_vector);
    tl_write_uint32(&inner, 1);
    tl_write_int64 (&inner, 3030LL);             /* invited user id */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* messageActionPhoneCall flags=0x3 (has reason + duration), video=no,
 * call_id=0xDEADBEEF, reason=hangup, duration=42. */
static void on_phone_call(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1015);
    tl_write_int32 (&inner, 1700011400);
    tl_write_uint32(&inner, AC_PhoneCall);
    tl_write_uint32(&inner, 0x3);                /* flags: reason + duration */
    tl_write_int64 (&inner, 0xdeadbeefLL);       /* call_id */
    tl_write_uint32(&inner, DR_Hangup);
    tl_write_int32 (&inner, 42);                 /* duration */

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_screenshot_taken(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1016);
    tl_write_int32 (&inner, 1700011500);
    tl_write_uint32(&inner, AC_ScreenshotTaken);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_custom_action(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1017);
    tl_write_int32 (&inner, 1700011600);
    tl_write_uint32(&inner, AC_CustomAction);
    tl_write_string(&inner, "custom boxed action");

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Supplementary action responders — keep the coverage of each branch
 * of parse_service_action honest without expanding the US-29 table. */

static void on_action_empty(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1101);
    tl_write_int32 (&inner, 1700020000);
    tl_write_uint32(&inner, AC_Empty);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

static void on_chat_delete_photo(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1102);
    tl_write_int32 (&inner, 1700020100);
    tl_write_uint32(&inner, AC_ChatDeletePhoto);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* PhoneCall with reason=missed + no duration flag — exercises alternate
 * branches in the reason switch and verifies duration defaults to 0s. */
static void on_phone_call_missed(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1103);
    tl_write_int32 (&inner, 1700020200);
    tl_write_uint32(&inner, AC_PhoneCall);
    tl_write_uint32(&inner, 0x1);                /* flags: reason only */
    tl_write_int64 (&inner, 42LL);               /* call_id */
    tl_write_uint32(&inner, DR_Missed);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Fake action CRC — must be labelled safely with its hex. */
#define AC_Fake 0xdeadcafeU
static void on_unknown_action(MtRpcContext *ctx) {
    TlWriter inner; tl_writer_init(&inner);
    write_service_preamble(&inner, 0, 1018);
    tl_write_int32 (&inner, 1700011700);
    tl_write_uint32(&inner, AC_Fake);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* updates.difference payload carrying one messageActionPinMessage to
 * prove that the watch poll loop surfaces service events (acceptance
 * criterion 3). */
static void on_updates_diff_with_service(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_difference);

    /* new_messages: Vector<Message>{1} — one messageService. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    write_service_preamble(&w, MS_FLAG_REPLY_TO, 2001);
    tl_write_uint32(&w, CRC_messageReplyHeader);
    tl_write_uint32(&w, REPLY_HAS_MSG_ID);
    tl_write_int32 (&w, 777);                    /* pinned id */
    tl_write_int32 (&w, 1700012000);             /* date */
    tl_write_uint32(&w, AC_PinMessage);

    /* Remaining difference fields — all empty. */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* new_encrypted */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* other_updates */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    /* state */
    tl_write_uint32(&w, TL_updates_state);
    tl_write_int32 (&w, 200);                    /* pts */
    tl_write_int32 (&w, 10);                     /* qts */
    tl_write_int32 (&w, 1700012000);             /* date */
    tl_write_int32 (&w, 3);                      /* seq */
    tl_write_int32 (&w, 0);                      /* unread */

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Tests — one per US-29 row                                        */
/* ================================================================ */

static void test_chat_create(void) {
    with_tmp_home("chat-create");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_create, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1001, "id preserved");
    ASSERT(row.date == 1700010000, "date preserved");
    ASSERT(row.is_service == 1, "row flagged as service");
    ASSERT(row.complex == 0, "service row not flagged complex");
    ASSERT(strstr(row.text, "created group 'Planning'") != NULL,
           "renderer surfaces group title");
    mt_server_reset();
}

static void test_chat_add_user(void) {
    with_tmp_home("chat-add-user");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_add_user, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1002, "id preserved");
    ASSERT(row.is_service == 1, "service flag");
    ASSERT(strstr(row.text, "added @4242") != NULL,
           "renderer surfaces added user id");
    mt_server_reset();
}

static void test_chat_delete_user(void) {
    with_tmp_home("chat-del-user");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_delete_user, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1003, "id preserved");
    ASSERT(strstr(row.text, "removed @4242") != NULL,
           "renderer surfaces removed user id");
    mt_server_reset();
}

static void test_chat_joined_by_link(void) {
    with_tmp_home("chat-joined-link");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_joined_by_link, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1004, "id preserved");
    ASSERT(strstr(row.text, "joined via invite link") != NULL,
           "renderer surfaces invite-link join");
    mt_server_reset();
}

static void test_chat_edit_title(void) {
    with_tmp_home("chat-edit-title");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_edit_title, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1005, "id preserved");
    ASSERT(strstr(row.text, "changed title to 'Shipping'") != NULL,
           "renderer surfaces new title");
    mt_server_reset();
}

static void test_chat_edit_photo(void) {
    with_tmp_home("chat-edit-photo");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_edit_photo, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1006, "id preserved");
    ASSERT(strstr(row.text, "changed group photo") != NULL,
           "renderer labels photo edit");
    mt_server_reset();
}

static void test_pin_message(void) {
    with_tmp_home("pin-msg");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_pin_message, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1007, "id preserved");
    ASSERT(strstr(row.text, "pinned message 12345") != NULL,
           "renderer surfaces pinned target id from reply_to");
    mt_server_reset();
}

static void test_history_clear(void) {
    with_tmp_home("hist-clear");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_history_clear, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1008, "id preserved");
    ASSERT(strstr(row.text, "history cleared") != NULL,
           "renderer surfaces history-clear");
    mt_server_reset();
}

static void test_channel_create(void) {
    with_tmp_home("chan-create");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_channel_create, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1009, "id preserved");
    ASSERT(strstr(row.text, "created channel 'Releases'") != NULL,
           "renderer surfaces channel title");
    mt_server_reset();
}

static void test_channel_migrate_from(void) {
    with_tmp_home("chan-migfrom");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_channel_migrate_from, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1010, "id preserved");
    ASSERT(strstr(row.text, "migrated from group") != NULL,
           "renderer labels channel-migrate-from");
    ASSERT(strstr(row.text, "77777") != NULL, "chat_id surfaced");
    mt_server_reset();
}

static void test_chat_migrate_to(void) {
    with_tmp_home("chat-migto");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_migrate_to, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1011, "id preserved");
    ASSERT(strstr(row.text, "migrated to channel 88888") != NULL,
           "renderer surfaces channel_id");
    mt_server_reset();
}

static void test_group_call(void) {
    with_tmp_home("group-call");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_group_call, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1012, "id preserved");
    ASSERT(strstr(row.text, "started video chat") != NULL,
           "renderer labels group call start");
    mt_server_reset();
}

static void test_group_call_scheduled(void) {
    with_tmp_home("group-call-sched");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_group_call_scheduled, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1013, "id preserved");
    ASSERT(strstr(row.text, "scheduled video chat for") != NULL,
           "renderer labels scheduled call");
    ASSERT(strstr(row.text, "1700020000") != NULL, "schedule_date surfaced");
    mt_server_reset();
}

static void test_invite_to_group_call(void) {
    with_tmp_home("invite-call");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_invite_to_group_call, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1014, "id preserved");
    ASSERT(strstr(row.text, "invited to video chat") != NULL,
           "renderer labels video-chat invite");
    mt_server_reset();
}

static void test_phone_call(void) {
    with_tmp_home("phone-call");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_phone_call, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1015, "id preserved");
    ASSERT(strstr(row.text, "called") != NULL,
           "renderer opens with 'called'");
    ASSERT(strstr(row.text, "42s") != NULL, "duration present");
    ASSERT(strstr(row.text, "hangup") != NULL, "reason present");
    mt_server_reset();
}

static void test_screenshot_taken(void) {
    with_tmp_home("screenshot");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_screenshot_taken, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1016, "id preserved");
    ASSERT(strstr(row.text, "took screenshot") != NULL,
           "renderer labels screenshot event");
    mt_server_reset();
}

static void test_custom_action(void) {
    with_tmp_home("custom-action");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_custom_action, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1017, "id preserved");
    ASSERT(strcmp(row.text, "custom boxed action") == 0,
           "custom action message passed through verbatim");
    mt_server_reset();
}

static void test_unknown_action_labelled(void) {
    with_tmp_home("unknown-action");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_unknown_action, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1018, "id preserved");
    ASSERT(row.is_service == 1, "service flag set");
    ASSERT(strstr(row.text, "[service action 0x") != NULL,
           "unknown action carries hex-labelled placeholder");
    ASSERT(strstr(row.text, "deadcafe") != NULL || strstr(row.text, "DEADCAFE") != NULL,
           "placeholder includes the unknown CRC");
    mt_server_reset();
}

/* Supplementary tests — exercise remaining parse_service_action branches
 * so the service block reaches >90% line coverage. Not counted against
 * the 19-row US-29 table. */

static void test_action_empty_renders_blank(void) {
    with_tmp_home("act-empty");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_action_empty, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1101, "id preserved for empty action");
    ASSERT(row.is_service == 1, "service flag set for actionEmpty");
    ASSERT(row.text[0] == '\0',
           "actionEmpty rendered as an empty string (drop-in stub)");
    mt_server_reset();
}

static void test_chat_delete_photo(void) {
    with_tmp_home("chat-del-photo");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_chat_delete_photo, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1102, "id preserved");
    ASSERT(strstr(row.text, "removed group photo") != NULL,
           "renderer labels chat-delete-photo");
    mt_server_reset();
}

static void test_phone_call_missed_zero_duration(void) {
    with_tmp_home("phone-missed");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_messages_getHistory, on_phone_call_missed, NULL);

    HistoryEntry row;
    fetch_one(&row);
    ASSERT(row.id == 1103, "id preserved");
    ASSERT(strstr(row.text, "missed") != NULL,
           "missed-call reason surfaces");
    ASSERT(strstr(row.text, "0s") != NULL,
           "duration defaults to 0s when flag.1 is clear");
    mt_server_reset();
}

/* Plumb a messageService through updates.difference — acceptance criterion 3. */
static void test_service_shows_in_watch(void) {
    with_tmp_home("watch-service");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_updates_getDifference,
                     on_updates_diff_with_service, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    UpdatesState prev = { .pts = 100, .qts = 0, .date = 0, .seq = 0 };
    UpdatesDifference diff = {0};
    ASSERT(domain_updates_difference(&cfg, &s, &t, &prev, &diff) == 0,
           "updates.difference parse succeeds with service message");
    ASSERT(diff.new_messages_count == 1,
           "service message surfaced — not filtered as complex");
    const HistoryEntry *m = &diff.new_messages[0];
    ASSERT(m->id == 2001, "service msg id preserved across watch path");
    ASSERT(m->is_service == 1, "watch entry flagged as service");
    ASSERT(strstr(m->text, "pinned message 777") != NULL,
           "watch surfaces the rendered action string");

    transport_close(&t);
    mt_server_reset();
}

void run_service_messages_tests(void) {
    RUN_TEST(test_chat_create);
    RUN_TEST(test_chat_add_user);
    RUN_TEST(test_chat_delete_user);
    RUN_TEST(test_chat_joined_by_link);
    RUN_TEST(test_chat_edit_title);
    RUN_TEST(test_chat_edit_photo);
    RUN_TEST(test_pin_message);
    RUN_TEST(test_history_clear);
    RUN_TEST(test_channel_create);
    RUN_TEST(test_channel_migrate_from);
    RUN_TEST(test_chat_migrate_to);
    RUN_TEST(test_group_call);
    RUN_TEST(test_group_call_scheduled);
    RUN_TEST(test_invite_to_group_call);
    RUN_TEST(test_phone_call);
    RUN_TEST(test_screenshot_taken);
    RUN_TEST(test_custom_action);
    RUN_TEST(test_unknown_action_labelled);
    RUN_TEST(test_service_shows_in_watch);
    /* Supplementary — coverage of Empty / DeletePhoto / alt PhoneCall
     * branches that the US-29 table does not prescribe. */
    RUN_TEST(test_action_empty_renders_blank);
    RUN_TEST(test_chat_delete_photo);
    RUN_TEST(test_phone_call_missed_zero_duration);
}
