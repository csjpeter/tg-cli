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
#include "domain/read/search.h"
#include "arg_parse.h"

/* for resolve cache flush */
extern void resolve_cache_flush(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not already surfaced by public headers ---- */
#define CRC_messages_search           0x29ee847aU
#define CRC_messages_searchGlobal     0x4bc6589aU
#define CRC_inputMessagesFilterEmpty  0x57e9a944U
#define CRC_users_getUsers            0x0d91a548U
#define CRC_inputUserSelf             0xf7c1b13fU
#define CRC_messages_getDialogs       0xa0f4cb4fU
#define CRC_dialog                    0xd58a08c6U
#define CRC_messages_getHistory       0x4423e6c5U
#define CRC_contacts_getContacts      0x5dd69e12U
#define CRC_contact                   0x145ade0bU
#define CRC_contacts_resolveUsername  0xf93ccba3U
#define CRC_users_getFullUser         0xb9f11a99U
#define CRC_users_userFull            0x3b6d152eU
/* inner userFull object — matches TL_userFull in tl_registry.h */
#define CRC_userFull_inner            0x93eadb53U
#define CRC_updates_getState          0xedd4882aU
#define CRC_updates_getDifference     0x19c2f763U
#define CRC_peerNotifySettings        0xa83b0426U

/* InputPeer CRCs (wire values for TEST-06 assertions). */
#define CRC_inputPeerSelf             0x7da07ec9U
#define CRC_inputPeerUser             0xdde8a54cU
#define CRC_inputPeerChannel          0x27bcbbfcU

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

/* Vector<User> with one full user that has premium flag set (flags2.3). */
static void on_get_self_premium(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    /* flags: has_first_name (1) | has_phone (4) */
    uint32_t flags = (1u << 1) | (1u << 4);
    tl_write_uint32(&w, flags);
    /* flags2: premium bit is flags2.3 */
    tl_write_uint32(&w, (1u << 3));
    tl_write_int64 (&w, 77002LL);       /* id */
    tl_write_string(&w, "Premium");     /* first_name */
    tl_write_string(&w, "+19995550001");/* phone */
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

/* messages.dialogsSlice#71e094f3 — two entries returned from a server that
 * has 50 total dialogs.  The first is a user peer (id=777, unread=3) and the
 * second is a channel peer (id=888, unread=0).  Users/chats vectors are
 * minimal (no access_hash on either) so the title join leaves titles empty —
 * we are testing the slice parse path, not the join. */
static void on_dialogs_slice(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 50);               /* count — total on server */

    /* dialogs: Vector<Dialog> with 2 entries */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);

    /* dialog 0: user peer id=777 unread=3 top=42 */
    tl_write_uint32(&w, CRC_dialog);
    tl_write_uint32(&w, 0);               /* flags=0 */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 777LL);
    tl_write_int32 (&w, 42);              /* top_message */
    tl_write_int32 (&w, 0);              /* read_inbox_max_id */
    tl_write_int32 (&w, 0);              /* read_outbox_max_id */
    tl_write_int32 (&w, 3);              /* unread_count */
    tl_write_int32 (&w, 0);              /* unread_mentions_count */
    tl_write_int32 (&w, 0);              /* unread_reactions_count */
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0);

    /* dialog 1: channel peer id=888 unread=0 top=99 */
    tl_write_uint32(&w, CRC_dialog);
    tl_write_uint32(&w, 0);               /* flags=0 */
    tl_write_uint32(&w, TL_peerChannel);
    tl_write_int64 (&w, 888LL);
    tl_write_int32 (&w, 99);             /* top_message */
    tl_write_int32 (&w, 0);
    tl_write_int32 (&w, 0);
    tl_write_int32 (&w, 0);             /* unread_count */
    tl_write_int32 (&w, 0);
    tl_write_int32 (&w, 0);
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0);

    /* messages vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* messages.dialogsNotModified#f0e3e596 count:int — server says nothing changed;
 * reports 37 total dialogs in the cache. */
static void on_dialogs_not_modified(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsNotModified);
    tl_write_int32 (&w, 37);    /* count */
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

/* users.userFull wrapper containing a minimal userFull with:
 *   about = "Test bio string"
 *   phone = "+15550001234"
 *   common_chats_count = 7
 *
 * userFull flags used:
 *   bit 4  → phone present
 *   bit 5  → about present
 *   bit 20 → common_chats_count present
 *
 * Layout written: flags(u32) id(i64) about(str) phone(str)
 *                 common_chats_count(i32)
 * (Matches the order parse_user_full() reads them.) */
static void on_get_full_user(MtRpcContext *ctx) {
    (void)ctx;
    uint32_t flags = (1u << 5) | (1u << 4) | (1u << 20);

    TlWriter w;
    tl_writer_init(&w);

    /* users.userFull wrapper */
    tl_write_uint32(&w, CRC_users_userFull);

    /* full_user:UserFull — inner userFull object */
    tl_write_uint32(&w, CRC_userFull_inner);
    tl_write_uint32(&w, flags);
    tl_write_int64 (&w, 8001LL);            /* id */
    tl_write_string(&w, "Test bio string"); /* about (flags.5) */
    tl_write_string(&w, "+15550001234");    /* phone (flags.4) */
    tl_write_int32 (&w, 7);                /* common_chats_count (flags.20) */

    /* chats:Vector<Chat> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users:Vector<User> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* contacts.resolvedPeer pointing at channel id 9001 with access_hash. */
static void on_resolve_channel(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerChannel);
    tl_write_int64 (&w, 9001LL);
    /* chats vector: one channel */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_channel);
    /* flags: bit 13 = has access_hash */
    tl_write_uint32(&w, (1u << 13));
    tl_write_uint32(&w, 0);                   /* flags2 */
    tl_write_int64 (&w, 9001LL);
    tl_write_int64 (&w, 0x0102030405060708LL);/* access_hash */
    /* users vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Capture the InputPeer CRC and first 8 bytes of peer args from a
 * getHistory request body (starts at CRC_messages_getHistory).
 *
 * Layout: [crc_getHistory:4][peer_crc:4][...peer_args...][offset_id:4]...
 * We expose this via a static so the responder can write it and the test
 * can read it after the call returns. */
typedef struct {
    uint32_t peer_crc;
    int64_t  peer_id;
    int64_t  peer_hash; /* valid only when peer_crc carries hash */
    int32_t  offset_id; /* first int32 after the peer */
} CapturedHistoryReq;

static CapturedHistoryReq g_captured_req;

static void on_history_capture(MtRpcContext *ctx) {
    /* req_body starts with CRC_messages_getHistory (4 bytes). Skip it. */
    if (ctx->req_body_len < 8) { on_history_empty(ctx); return; }
    const uint8_t *p = ctx->req_body + 4; /* skip getHistory CRC */
    size_t rem = ctx->req_body_len - 4;

    uint32_t pcrc = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                  | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    g_captured_req.peer_crc  = pcrc;
    g_captured_req.peer_id   = 0;
    g_captured_req.peer_hash = 0;
    g_captured_req.offset_id = 0;
    p += 4; rem -= 4;

    if (pcrc == CRC_inputPeerSelf) {
        /* No additional fields; offset_id follows. */
        if (rem >= 4) {
            int32_t oi = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                                  | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
            g_captured_req.offset_id = oi;
        }
    } else if (pcrc == CRC_inputPeerUser || pcrc == CRC_inputPeerChannel) {
        /* id:int64 + access_hash:int64 */
        if (rem < 16) { on_history_empty(ctx); return; }
        int64_t id = 0;
        for (int i = 0; i < 8; i++) id |= ((int64_t)p[i]) << (i * 8);
        p += 8; rem -= 8;
        int64_t hash = 0;
        for (int i = 0; i < 8; i++) hash |= ((int64_t)p[i]) << (i * 8);
        p += 8; rem -= 8;
        g_captured_req.peer_id   = id;
        g_captured_req.peer_hash = hash;
        if (rem >= 4) {
            int32_t oi = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                                  | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
            g_captured_req.offset_id = oi;
        }
    }

    on_history_empty(ctx);
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
    dialogs_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_empty, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = -1;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, NULL) == 0,
           "get_dialogs succeeds on empty");
    ASSERT(n == 0, "zero dialogs returned");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_one_user(void) {
    with_tmp_home("dlg-user");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_one_user, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, NULL) == 0,
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

/* TEST-02: messages.dialogsSlice variant — two entries in the batch, server
 * reports 50 total.  Verify that the batch entries are parsed correctly and
 * that total_count surfaces the server-side count rather than the batch
 * size. */
static void test_dialogs_slice_variant(void) {
    with_tmp_home("dlg-slice");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_slice, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0;
    int total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, &total) == 0,
           "get_dialogs slice succeeds");
    ASSERT(n == 2, "two dialogs in batch");
    ASSERT(total == 50, "total_count from slice header");

    /* First entry: user peer */
    ASSERT(rows[0].kind == DIALOG_PEER_USER, "first is user peer");
    ASSERT(rows[0].peer_id == 777LL, "user peer_id");
    ASSERT(rows[0].top_message_id == 42, "user top_message");
    ASSERT(rows[0].unread_count == 3, "user unread_count");

    /* Second entry: channel peer */
    ASSERT(rows[1].kind == DIALOG_PEER_CHANNEL, "second is channel peer");
    ASSERT(rows[1].peer_id == 888LL, "channel peer_id");
    ASSERT(rows[1].top_message_id == 99, "channel top_message");
    ASSERT(rows[1].unread_count == 0, "channel unread_count");

    transport_close(&t);
    mt_server_reset();
}

/* TEST-03: messages.dialogsNotModified variant — server returns the not-modified
 * constructor with a count field.  The domain should return success with zero
 * entries and surface the server count via total_count so callers know their
 * cached list is still valid. */
static void test_dialogs_not_modified_variant(void) {
    with_tmp_home("dlg-notmod");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_not_modified, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = -1;
    int total = -1;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0, rows, &n, &total) == 0,
           "get_dialogs succeeds on not-modified");
    /* Zero entries — caller must consult its cache. */
    ASSERT(n == 0, "zero entries on not-modified");
    /* Server-reported count must propagate so the caller knows cache is valid. */
    ASSERT(total == 37, "total_count carries server count");

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

/* TEST-22: resolveUsername returns TL_channel — verify ResolvedPeer is populated
 * with kind=CHANNEL, correct id and access_hash.  No follow-up getHistory call
 * is made; this exercises the channel branch of domain_resolve_username alone. */
static void test_resolve_username_channel(void) {
    with_tmp_home("resolve-chan");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_channel, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@mychannel", &rp) == 0,
           "resolve channel ok");
    ASSERT(rp.kind == RESOLVED_KIND_CHANNEL, "kind == RESOLVED_KIND_CHANNEL");
    ASSERT(rp.id == 9001LL, "channel id == 9001");
    ASSERT(rp.have_hash == 1, "have_hash set for channel");
    ASSERT((uint64_t)rp.access_hash == 0x0102030405060708ULL,
           "channel access_hash value matches");

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

/* TEST-05a: premium bit (flags2.3) decoded correctly from a full user
 * record returned by users.getUsers. */
static void test_get_self_premium(void) {
    with_tmp_home("self-prem");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_users_getUsers, on_get_self_premium, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    SelfInfo si = {0};
    ASSERT(domain_get_self(&cfg, &s, &t, &si) == 0, "premium get_self succeeds");
    ASSERT(si.id == 77002LL, "id == 77002");
    ASSERT(strcmp(si.first_name, "Premium") == 0, "first_name == Premium");
    ASSERT(si.is_premium == 1, "is_premium flag set");
    ASSERT(si.is_bot == 0, "is_bot not set");

    transport_close(&t);
    mt_server_reset();
}

/* TEST-05b: arg_parse maps the "self" alias to CMD_ME (same as "me"). */
static void test_self_alias_maps_to_cmd_me(void) {
    const char *argv_self[] = {"tg-cli", "self"};
    ArgResult   ar_self = {0};
    int rc_self = arg_parse(2, (char **)argv_self, &ar_self);
    ASSERT(rc_self == 0, "arg_parse(self) succeeds");
    ASSERT(ar_self.command == CMD_ME, "self alias maps to CMD_ME");

    const char *argv_me[] = {"tg-cli", "me"};
    ArgResult   ar_me = {0};
    int rc_me = arg_parse(2, (char **)argv_me, &ar_me);
    ASSERT(rc_me == 0, "arg_parse(me) succeeds");
    ASSERT(ar_me.command == CMD_ME, "me maps to CMD_ME");
}

/* ================================================================ */
/* TEST-06: history peer variants                                   */
/* ================================================================ */

/* Case 1 — history self: getHistory must carry inputPeerSelf. */
static void test_history_self(void) {
    with_tmp_home("hist-self");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    memset(&g_captured_req, 0, sizeof(g_captured_req));
    mt_server_expect(CRC_messages_getHistory, on_history_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    HistoryEntry rows[4]; int n = -1;
    ASSERT(domain_get_history(&cfg, &s, &t, &peer, 0, 4, rows, &n) == 0,
           "history_self ok");
    ASSERT(g_captured_req.peer_crc == CRC_inputPeerSelf,
           "wire carries inputPeerSelf");

    transport_close(&t);
    mt_server_reset();
}

/* Case 2 — history numeric user id: getHistory must carry inputPeerUser
 * with id=123 and access_hash=0. */
static void test_history_user_numeric_id(void) {
    with_tmp_home("hist-uid");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    memset(&g_captured_req, 0, sizeof(g_captured_req));
    mt_server_expect(CRC_messages_getHistory, on_history_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer peer = { .kind = HISTORY_PEER_USER, .peer_id = 123, .access_hash = 0 };
    HistoryEntry rows[4]; int n = -1;
    ASSERT(domain_get_history(&cfg, &s, &t, &peer, 0, 4, rows, &n) == 0,
           "history numeric id ok");
    ASSERT(g_captured_req.peer_crc == CRC_inputPeerUser,
           "wire carries inputPeerUser");
    ASSERT(g_captured_req.peer_id == 123LL, "peer_id == 123");
    ASSERT(g_captured_req.peer_hash == 0LL, "access_hash == 0");

    transport_close(&t);
    mt_server_reset();
}

/* Case 3 — history @foo: resolveUsername fires, then getHistory carries
 * inputPeerUser with id=8001 and the resolved access_hash. */
static void test_history_username_resolve(void) {
    with_tmp_home("hist-uname");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    memset(&g_captured_req, 0, sizeof(g_captured_req));
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user, NULL);
    mt_server_expect(CRC_messages_getHistory, on_history_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    /* Resolve @foo first, then pass the result into history. */
    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@foo", &rp) == 0,
           "resolve @foo ok");
    ASSERT(rp.kind == RESOLVED_KIND_USER, "resolved as USER");
    ASSERT(rp.id == 8001LL, "resolved id == 8001");

    HistoryPeer peer = {
        .kind        = HISTORY_PEER_USER,
        .peer_id     = rp.id,
        .access_hash = rp.access_hash,
    };
    HistoryEntry rows[4]; int n = -1;
    ASSERT(domain_get_history(&cfg, &s, &t, &peer, 0, 4, rows, &n) == 0,
           "getHistory after resolve ok");
    ASSERT(g_captured_req.peer_crc == CRC_inputPeerUser,
           "wire carries inputPeerUser");
    ASSERT(g_captured_req.peer_id == 8001LL, "peer_id == 8001");
    ASSERT((uint64_t)g_captured_req.peer_hash == 0xDEADBEEFCAFEBABEULL,
           "access_hash threaded through");

    transport_close(&t);
    mt_server_reset();
}

/* Case 4 — history @channel: resolved as channel, access_hash threads to
 * getHistory via inputPeerChannel. */
static void test_history_channel_access_hash(void) {
    with_tmp_home("hist-chan");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    memset(&g_captured_req, 0, sizeof(g_captured_req));
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_channel, NULL);
    mt_server_expect(CRC_messages_getHistory, on_history_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    ResolvedPeer rp = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@mychannel", &rp) == 0,
           "resolve @mychannel ok");
    ASSERT(rp.kind == RESOLVED_KIND_CHANNEL, "resolved as CHANNEL");
    ASSERT(rp.id == 9001LL, "channel id == 9001");
    ASSERT((uint64_t)rp.access_hash == 0x0102030405060708ULL,
           "channel access_hash");

    HistoryPeer peer = {
        .kind        = HISTORY_PEER_CHANNEL,
        .peer_id     = rp.id,
        .access_hash = rp.access_hash,
    };
    HistoryEntry rows[4]; int n = -1;
    ASSERT(domain_get_history(&cfg, &s, &t, &peer, 0, 4, rows, &n) == 0,
           "getHistory channel ok");
    ASSERT(g_captured_req.peer_crc == CRC_inputPeerChannel,
           "wire carries inputPeerChannel");
    ASSERT(g_captured_req.peer_id == 9001LL, "channel peer_id == 9001");
    ASSERT((uint64_t)g_captured_req.peer_hash == 0x0102030405060708ULL,
           "channel access_hash on wire");

    transport_close(&t);
    mt_server_reset();
}

/* Case 5 — --offset flag: offset_id=50 lands on the wire. */
static void test_history_offset_flag(void) {
    with_tmp_home("hist-off");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    memset(&g_captured_req, 0, sizeof(g_captured_req));
    mt_server_expect(CRC_messages_getHistory, on_history_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    HistoryEntry rows[4]; int n = -1;
    ASSERT(domain_get_history(&cfg, &s, &t, &peer, 50, 4, rows, &n) == 0,
           "history offset ok");
    ASSERT(g_captured_req.peer_crc == CRC_inputPeerSelf,
           "peer is inputPeerSelf");
    ASSERT(g_captured_req.offset_id == 50, "offset_id == 50 on wire");

    transport_close(&t);
    mt_server_reset();
}

/* Case 6 — resolve cache hit: two consecutive calls fire one RPC.
 * The second call must return the same data from cache. */
static void test_history_cache_hit(void) {
    with_tmp_home("hist-cache");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    /* First call: goes to the wire. */
    ResolvedPeer rp1 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@cached_user", &rp1) == 0,
           "first resolve ok");
    int calls_after_first = mt_server_rpc_call_count();

    /* Second call: must be served from cache — no new RPC. */
    ResolvedPeer rp2 = {0};
    ASSERT(domain_resolve_username(&cfg, &s, &t, "@cached_user", &rp2) == 0,
           "second resolve ok (from cache)");
    ASSERT(mt_server_rpc_call_count() == calls_after_first,
           "no additional RPC for cache hit");
    ASSERT(rp2.id == rp1.id, "cached id matches");
    ASSERT(rp2.access_hash == rp1.access_hash, "cached hash matches");

    transport_close(&t);
    mt_server_reset();
}

/* TEST-09: users.getFullUser happy path.
 * Fires contacts.resolveUsername (→ user id 8001) followed by
 * users.getFullUser (→ minimal userFull with about/phone/common_chats).
 * Asserts that domain_get_user_info surfaces all three fields. */
static void test_get_full_user_happy(void) {
    with_tmp_home("full-user");
    mt_server_init(); mt_server_reset();
    resolve_cache_flush();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_contacts_resolveUsername, on_resolve_user, NULL);
    mt_server_expect(CRC_users_getFullUser,        on_get_full_user, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    UserFullInfo fi = {0};
    ASSERT(domain_get_user_info(&cfg, &s, &t, "@testuser", &fi) == 0,
           "get_user_info ok");
    ASSERT(fi.id == 8001LL, "id == 8001");
    ASSERT(strcmp(fi.bio, "Test bio string") == 0, "bio decoded");
    ASSERT(strcmp(fi.phone, "+15550001234") == 0, "phone decoded");
    ASSERT(fi.common_chats_count == 7, "common_chats_count == 7");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* TEST-10: search functional tests                                 */
/* ================================================================ */

/* Helper: write a minimal messages.messages with N plain text messages.
 * Each message uses TL_message constructor with:
 *   flags=0, flags2=0 (no optional fields), out=0
 *   id = base_id + i, peer = inputPeerSelf (skipped by parser as from_id)
 *   date = 1700000000 + i, message = text[i]
 *
 * Actual wire layout for a message with flags=0, flags2=0:
 *   crc(4) flags(4) flags2(4) id(4)
 *   [no from_id — flags.8 off]
 *   peer_id: peerUser id(4+8)   (flags.28 off → no saved_peer)
 *   [no fwd_header]
 *   date(4)  message:string
 */
static void write_messages_messages(TlWriter *w, int count, int base_id,
                                    int base_date, const char **texts) {
    tl_write_uint32(w, TL_messages_messages);
    /* messages vector */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, (uint32_t)count);
    for (int i = 0; i < count; i++) {
        tl_write_uint32(w, TL_message);
        tl_write_uint32(w, 0);              /* flags = 0 */
        tl_write_uint32(w, 0);              /* flags2 = 0 */
        tl_write_int32 (w, base_id + i);    /* id */
        /* peer_id: peerUser with id=1 (flags.28 off, flags.8 off) */
        tl_write_uint32(w, TL_peerUser);
        tl_write_int64 (w, 1LL);
        tl_write_int32 (w, base_date + i);  /* date */
        tl_write_string(w, texts[i]);       /* message */
    }
    /* chats vector: empty */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 0);
    /* users vector: empty */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 0);
}

/* Responder for messages.searchGlobal — returns 3 messages. */
static void on_search_global_three(MtRpcContext *ctx) {
    static const char *texts[3] = { "hello world", "second hit", "third one" };
    TlWriter w;
    tl_writer_init(&w);
    write_messages_messages(&w, 3, 1001, 1700100000, texts);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Responder for messages.search (per-peer) — returns 2 messages. */
static void on_search_peer_two(MtRpcContext *ctx) {
    static const char *texts[2] = { "peer match one", "peer match two" };
    TlWriter w;
    tl_writer_init(&w);
    write_messages_messages(&w, 2, 2001, 1700200000, texts);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Capture state for search request bytes. */
typedef struct {
    uint32_t crc;            /* first CRC in request body */
    int32_t  limit;          /* limit field */
    char     query[128];     /* query string (UTF-8) */
    uint32_t peer_crc;       /* inputPeer CRC (per-peer only, 0 for global) */
} CapturedSearchReq;

static CapturedSearchReq g_search_req;

/* Read a TL string from a byte buffer (little-endian, Pascal-style).
 * Returns number of bytes consumed (including length byte(s) + padding),
 * or 0 on error. Writes up to dst_max-1 bytes into dst. */
static size_t read_tl_string_raw(const uint8_t *p, size_t rem,
                                  char *dst, size_t dst_max) {
    if (rem < 1) return 0;
    size_t slen, hdr;
    if (p[0] < 254) {
        slen = p[0]; hdr = 1;
    } else if (p[0] == 254) {
        if (rem < 4) return 0;
        slen = (size_t)p[1] | ((size_t)p[2] << 8) | ((size_t)p[3] << 16);
        hdr = 4;
    } else {
        return 0;
    }
    if (rem < hdr + slen) return 0;
    size_t copy = slen < dst_max - 1 ? slen : dst_max - 1;
    memcpy(dst, p + hdr, copy);
    dst[copy] = '\0';
    size_t total = hdr + slen;
    /* round up to 4-byte boundary */
    if (total % 4) total += 4 - (total % 4);
    return total;
}

/* Responder that captures global-search request fields. */
static void on_search_global_capture(MtRpcContext *ctx) {
    memset(&g_search_req, 0, sizeof(g_search_req));
    if (ctx->req_body_len < 4) { on_search_global_three(ctx); return; }

    const uint8_t *p = ctx->req_body;
    size_t rem = ctx->req_body_len;

    /* CRC (4 bytes) */
    g_search_req.crc = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4; rem -= 4;

    /* flags (4 bytes) */
    if (rem < 4) { on_search_global_three(ctx); return; }
    p += 4; rem -= 4;

    /* query string */
    size_t adv = read_tl_string_raw(p, rem, g_search_req.query,
                                    sizeof(g_search_req.query));
    if (adv == 0) { on_search_global_three(ctx); return; }
    p += adv; rem -= adv;

    /* filter CRC (4) + min_date (4) + max_date (4) + offset_rate (4) */
    if (rem < 16) { on_search_global_three(ctx); return; }
    p += 16; rem -= 16;

    /* offset_peer CRC (4) + skip TL_inputPeerEmpty (no extra fields) */
    if (rem < 4) { on_search_global_three(ctx); return; }
    p += 4; rem -= 4;

    /* offset_id (4) */
    if (rem < 4) { on_search_global_three(ctx); return; }
    p += 4; rem -= 4;

    /* limit (4) */
    if (rem >= 4) {
        g_search_req.limit = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    }

    on_search_global_three(ctx);
}

/* Responder that captures per-peer search request fields. */
static void on_search_peer_capture(MtRpcContext *ctx) {
    memset(&g_search_req, 0, sizeof(g_search_req));
    if (ctx->req_body_len < 4) { on_search_peer_two(ctx); return; }

    const uint8_t *p = ctx->req_body;
    size_t rem = ctx->req_body_len;

    /* CRC (4) */
    g_search_req.crc = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4; rem -= 4;

    /* flags (4) */
    if (rem < 4) { on_search_peer_two(ctx); return; }
    p += 4; rem -= 4;

    /* peer CRC (4) */
    if (rem < 4) { on_search_peer_two(ctx); return; }
    g_search_req.peer_crc = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                          | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4; rem -= 4;

    /* skip peer args: inputPeerUser → id(8) + access_hash(8) */
    if (g_search_req.peer_crc == TL_inputPeerUser ||
        g_search_req.peer_crc == TL_inputPeerChannel) {
        if (rem < 16) { on_search_peer_two(ctx); return; }
        p += 16; rem -= 16;
    } else if (g_search_req.peer_crc == TL_inputPeerChat) {
        if (rem < 8) { on_search_peer_two(ctx); return; }
        p += 8; rem -= 8;
    }
    /* inputPeerSelf: no extra bytes */

    /* query string */
    size_t adv = read_tl_string_raw(p, rem, g_search_req.query,
                                    sizeof(g_search_req.query));
    if (adv == 0) { on_search_peer_two(ctx); return; }
    p += adv; rem -= adv;

    /* filter CRC (4) + min_date (4) + max_date (4) + offset_id (4) +
       add_offset (4) */
    if (rem < 20) { on_search_peer_two(ctx); return; }
    p += 20; rem -= 20;

    /* limit (4) */
    if (rem >= 4) {
        g_search_req.limit = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    }

    on_search_peer_two(ctx);
}

/* TEST-10a: messages.searchGlobal — three results come back, request CRC
 * is correct, and the query string lands on the wire. */
static void test_search_global_happy(void) {
    with_tmp_home("srch-global");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_searchGlobal, on_search_global_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry hits[8];
    int n = -1;
    ASSERT(domain_search_global(&cfg, &s, &t, "hello", 10, hits, &n) == 0,
           "search_global succeeds");
    ASSERT(n == 3, "three hits returned");
    ASSERT(hits[0].id == 1001, "first hit id == 1001");
    ASSERT(hits[1].id == 1002, "second hit id == 1002");
    ASSERT(hits[2].id == 1003, "third hit id == 1003");
    ASSERT(strcmp(hits[0].text, "hello world") == 0, "first hit text");
    ASSERT(hits[0].date == 1700100000, "first hit date");
    ASSERT(g_search_req.crc == CRC_messages_searchGlobal,
           "request CRC is searchGlobal");
    ASSERT(strcmp(g_search_req.query, "hello") == 0,
           "query string threaded to wire");

    transport_close(&t);
    mt_server_reset();
}

/* TEST-10b: messages.search per-peer — two results, inputPeerUser on wire. */
static void test_search_per_peer_happy(void) {
    with_tmp_home("srch-peer");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_search, on_search_peer_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer peer = {
        .kind        = HISTORY_PEER_USER,
        .peer_id     = 5555LL,
        .access_hash = 0xABCDEF1234567890LL,
    };
    HistoryEntry hits[8];
    int n = -1;
    ASSERT(domain_search_peer(&cfg, &s, &t, &peer, "find me", 5, hits, &n) == 0,
           "search_peer succeeds");
    ASSERT(n == 2, "two hits returned");
    ASSERT(hits[0].id == 2001, "first hit id == 2001");
    ASSERT(hits[1].id == 2002, "second hit id == 2002");
    ASSERT(strcmp(hits[0].text, "peer match one") == 0, "first hit text");
    ASSERT(hits[0].date == 1700200000, "first hit date");
    ASSERT(g_search_req.crc == CRC_messages_search,
           "request CRC is messages.search");
    ASSERT(g_search_req.peer_crc == TL_inputPeerUser,
           "peer field carries inputPeerUser");
    ASSERT(strcmp(g_search_req.query, "find me") == 0,
           "query string threaded to wire");

    transport_close(&t);
    mt_server_reset();
}

/* TEST-10c: limit field equals what was passed (FEAT-08). */
static void test_search_limit_respected(void) {
    with_tmp_home("srch-limit");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_searchGlobal, on_search_global_capture, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry hits[8];
    int n = -1;
    ASSERT(domain_search_global(&cfg, &s, &t, "test", 7, hits, &n) == 0,
           "search_global with limit=7 succeeds");
    ASSERT(g_search_req.limit == 7, "limit == 7 on wire");

    transport_close(&t);
    mt_server_reset();
}

void run_read_path_tests(void) {
    RUN_TEST(test_get_self);
    RUN_TEST(test_get_self_premium);
    RUN_TEST(test_self_alias_maps_to_cmd_me);
    RUN_TEST(test_dialogs_empty);
    RUN_TEST(test_dialogs_one_user);
    RUN_TEST(test_dialogs_slice_variant);
    RUN_TEST(test_dialogs_not_modified_variant);
    RUN_TEST(test_history_empty);
    RUN_TEST(test_history_one_message_empty);
    RUN_TEST(test_history_self);
    RUN_TEST(test_history_user_numeric_id);
    RUN_TEST(test_history_username_resolve);
    RUN_TEST(test_history_channel_access_hash);
    RUN_TEST(test_history_offset_flag);
    RUN_TEST(test_history_cache_hit);
    RUN_TEST(test_contacts_empty);
    RUN_TEST(test_contacts_two);
    RUN_TEST(test_resolve_username_happy);
    RUN_TEST(test_resolve_username_not_found);
    RUN_TEST(test_resolve_username_channel);
    RUN_TEST(test_get_full_user_happy);
    RUN_TEST(test_updates_state);
    RUN_TEST(test_updates_difference_empty);
    RUN_TEST(test_rpc_error_propagation);
    RUN_TEST(test_search_global_happy);
    RUN_TEST(test_search_per_peer_happy);
    RUN_TEST(test_search_limit_respected);
}
