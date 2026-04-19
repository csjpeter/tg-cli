/**
 * @file test_read_path.c
 * @brief FT-04 — read-path functional tests through the mock server.
 *
 * Covers the minimum viable read surface: self profile, dialogs,
 * history, contacts, resolve-username, and updates.state /
 * updates.getDifference. Every test wires real production parser code
 * (domain_*) against in-process responders that emit canonical TL
 * envelopes — the bytes the client sees are byte-for-byte what Telegram
 * would put on the wire for that constructor.
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

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/contacts.h"
#include "domain/read/user_info.h"
#include "domain/read/updates.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not already surfaced by public headers ---- */
#define CRC_users_getUsers            0x0d91a548U
#define CRC_inputUserSelf             0xf7c1b13fU
#define CRC_messages_getDialogs       0xa0f4cb4fU
#define CRC_dialog                    0xd58a08c6U
#define CRC_messages_getHistory       0x4423e6c5U
#define CRC_contacts_getContacts      0x5dd69e12U
#define CRC_contact                   0x145ade0bU
#define CRC_contacts_resolveUsername  0xf93ccba3U
#define CRC_updates_getState          0xedd4882aU
#define CRC_updates_getDifference     0x19c2f763U
#define CRC_peerNotifySettings        0xa83b0426U

/* ---- helpers ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-read-%s", tag);
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

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

/* Vector<User> with one userEmpty (simplest user: id only). */
static void on_get_self(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_userEmpty);
    tl_write_int64 (&w, 99001LL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.dialogs with 0 dialogs / messages / chats / users. */
static void on_dialogs_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* dialogs */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* messages */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.dialogs with one user-peer dialog (id=555, unread=7). */
static void on_dialogs_one_user(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);

    /* dialogs: Vector<Dialog> with 1 entry */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_dialog);
    tl_write_uint32(&w, 0);                  /* flags=0 — no optional fields */
    tl_write_uint32(&w, TL_peerUser);        /* peer */
    tl_write_int64 (&w, 555LL);
    tl_write_int32 (&w, 1200);               /* top_message */
    tl_write_int32 (&w, 0);                  /* read_inbox_max_id */
    tl_write_int32 (&w, 0);                  /* read_outbox_max_id */
    tl_write_int32 (&w, 7);                  /* unread_count */
    tl_write_int32 (&w, 0);                  /* unread_mentions_count */
    tl_write_int32 (&w, 0);                  /* unread_reactions_count */
    /* peerNotifySettings with flags=0 — no sub-fields. */
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0);

    /* messages vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users vector: one user with access_hash only (flags.0=1, flags2=0) */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 1u);                 /* flags: has access_hash */
    tl_write_uint32(&w, 0);                  /* flags2 */
    tl_write_int64 (&w, 555LL);              /* id */
    tl_write_int64 (&w, 0xAABBCCDDEEFF0011LL);/* access_hash */

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.messages empty. */
static void on_history_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* messages */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.messages with one messageEmpty (id=42, no peer). */
static void on_history_one_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);                   /* flags */
    tl_write_int32 (&w, 42);                  /* id */
    /* No peer (flags.0 off) */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* contacts.contacts with empty vector. */
static void on_contacts_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_contacts);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* contacts */
    tl_write_uint32(&w, 0);                                 /* saved_count */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* contacts.contacts with two entries (mutual + non-mutual). */
static void on_contacts_two(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_contacts);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    /* contact#145ade0b user_id:long mutual:Bool */
    tl_write_uint32(&w, CRC_contact);
    tl_write_int64 (&w, 101LL);
    tl_write_uint32(&w, TL_boolTrue);
    tl_write_uint32(&w, CRC_contact);
    tl_write_int64 (&w, 202LL);
    tl_write_uint32(&w, TL_boolFalse);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* contacts.resolvedPeer pointing at user id 8001 with access_hash. */
static void on_resolve_user(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 8001LL);
    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* users vector: one user */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 1u);                 /* flags.0 → access_hash */
    tl_write_uint32(&w, 0);                  /* flags2 */
    tl_write_int64 (&w, 8001LL);
    tl_write_int64 (&w, 0xDEADBEEFCAFEBABEULL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_resolve_not_found(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "USERNAME_NOT_OCCUPIED");
}

/* updates.state pts=100 qts=5 date=1700000000 seq=1 unread=3 */
static void on_updates_state(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_state);
    tl_write_int32 (&w, 100);
    tl_write_int32 (&w, 5);
    tl_write_int32 (&w, 1700000000);
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 3);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* updates.differenceEmpty — the trivial "nothing changed" reply. */
static void on_updates_diff_empty(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_differenceEmpty);
    tl_write_int32 (&w, 1700000500);             /* date */
    tl_write_int32 (&w, 2);                      /* seq */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Generic handler for asserting RPC errors propagate. */
static void on_generic_500(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 500, "INTERNAL_SERVER_ERROR");
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

static void test_get_self(void) {
    with_tmp_home("self");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_users_getUsers, on_get_self, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    SelfInfo si = {0};
    ASSERT(domain_get_self(&cfg, &s, &t, &si) == 0, "get_self succeeds");
    ASSERT(si.id == 99001LL, "id == 99001");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_empty(void) {
    with_tmp_home("dlg-empty");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = -1;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, rows, &n) == 0,
           "get_dialogs succeeds on empty");
    ASSERT(n == 0, "zero dialogs returned");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_one_user(void) {
    with_tmp_home("dlg-user");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_one_user, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, rows, &n) == 0,
           "get_dialogs succeeds");
    ASSERT(n == 1, "one dialog parsed");
    ASSERT(rows[0].kind == DIALOG_PEER_USER, "user peer kind");
    ASSERT(rows[0].peer_id == 555LL, "peer_id roundtrips");
    ASSERT(rows[0].top_message_id == 1200, "top_message roundtrips");
    ASSERT(rows[0].unread_count == 7, "unread_count roundtrips");
    /* access_hash comes from the users vector join — the user carried
     * flags.0 so have_access_hash should be set. */
    ASSERT(rows[0].have_access_hash == 1, "access_hash joined from users vec");
    ASSERT(rows[0].access_hash == (int64_t)0xAABBCCDDEEFF0011LL,
           "access_hash value");

    transport_close(&t);
    mt_server_reset();
}

static void test_history_empty(void) {
    with_tmp_home("hist-empty");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = -1;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "get_history_self empty ok");
    ASSERT(n == 0, "zero messages");

    transport_close(&t);
    mt_server_reset();
}

static void test_history_one_message_empty(void) {
    with_tmp_home("hist-msg");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_one_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4];
    int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "get_history_self ok");
    /* domain_get_history only records entries that have id or text; a
     * messageEmpty with id=42 has id set, so it should land. */
    ASSERT(n == 1, "one messageEmpty parsed");
    ASSERT(rows[0].id == 42, "id == 42");

    transport_close(&t);
    mt_server_reset();
}

static void test_contacts_empty(void) {
    with_tmp_home("cont-empty");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_getContacts, on_contacts_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ContactEntry rows[8];
    int n = -1;
    ASSERT(domain_get_contacts(&cfg, &s, &t, rows, 8, &n) == 0,
           "contacts empty ok");
    ASSERT(n == 0, "zero contacts");

    transport_close(&t);
    mt_server_reset();
}

static void test_contacts_two(void) {
    with_tmp_home("cont-two");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_getContacts, on_contacts_two, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ContactEntry rows[8];
    int n = 0;
    ASSERT(domain_get_contacts(&cfg, &s, &t, rows, 8, &n) == 0,
           "contacts ok");
    ASSERT(n == 2, "two contacts");
    ASSERT(rows[0].user_id == 101 && rows[0].mutual == 1, "first is mutual");
    ASSERT(rows[1].user_id == 202 && rows[1].mutual == 0, "second not mutual");

    transport_close(&t);
    mt_server_reset();
}

static void test_resolve_username_happy(void) {
    with_tmp_home("resolve-ok");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@somebody", &rp) == 0,
           "resolve ok");
    ASSERT(rp.kind == RESOLVED_KIND_USER, "USER kind");
    ASSERT(rp.id == 8001LL, "id 8001");
    ASSERT(rp.have_hash == 1, "have access_hash");
    ASSERT((uint64_t)rp.access_hash == 0xDEADBEEFCAFEBABEULL,
           "access_hash value");
    ASSERT(strcmp(rp.username, "somebody") == 0, "'@' stripped");

    transport_close(&t);
    mt_server_reset();
}

static void test_resolve_username_not_found(void) {
    with_tmp_home("resolve-nf");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_not_found, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@nonexistent", &rp) == -1,
           "resolve returns -1 on RPC error");

    transport_close(&t);
    mt_server_reset();
}

static void test_updates_state(void) {
    with_tmp_home("upd-state");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_updates_getState, on_updates_state, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    UpdatesState st = {0};
    ASSERT(domain_updates_state(&cfg, &s, &t, &st) == 0, "state ok");
    ASSERT(st.pts == 100, "pts");
    ASSERT(st.qts == 5, "qts");
    ASSERT(st.date == 1700000000, "date");
    ASSERT(st.seq == 1, "seq");
    ASSERT(st.unread_count == 3, "unread_count");

    transport_close(&t);
    mt_server_reset();
}

static void test_updates_difference_empty(void) {
    with_tmp_home("upd-diff");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_updates_getDifference, on_updates_diff_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    UpdatesState prev = { .pts = 100, .qts = 5, .date = 1700000000, .seq = 1 };
    UpdatesDifference diff = {0};
    ASSERT(domain_updates_difference(&cfg, &s, &t, &prev, &diff) == 0,
           "getDifference ok");
    ASSERT(diff.is_empty == 1, "marked empty");
    ASSERT(diff.next_state.date == 1700000500, "date advanced");
    ASSERT(diff.next_state.seq == 2, "seq advanced");
    ASSERT(diff.new_messages_count == 0, "no new messages");

    transport_close(&t);
    mt_server_reset();
}

static void test_rpc_error_propagation(void) {
    with_tmp_home("rpc-err");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    /* Any read method — use get_self as the canary. */
    mt_server_expect(CRC_users_getUsers, on_generic_500, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    SelfInfo si = {0};
    ASSERT(domain_get_self(&cfg, &s, &t, &si) == -1,
           "domain_get_self -1 on rpc_error");

    transport_close(&t);
    mt_server_reset();
}

void run_read_path_tests(void) {
    RUN_TEST(test_get_self);
    RUN_TEST(test_dialogs_empty);
    RUN_TEST(test_dialogs_one_user);
    RUN_TEST(test_history_empty);
    RUN_TEST(test_history_one_message_empty);
    RUN_TEST(test_contacts_empty);
    RUN_TEST(test_contacts_two);
    RUN_TEST(test_resolve_username_happy);
    RUN_TEST(test_resolve_username_not_found);
    RUN_TEST(test_updates_state);
    RUN_TEST(test_updates_difference_empty);
    RUN_TEST(test_rpc_error_propagation);
}
