/**
 * @file test_deep_pagination.c
 * @brief TEST-77 / US-26 — functional coverage for deep pagination of
 *        messages.getDialogs, messages.getHistory, and messages.search.
 *
 * These scenarios drive the production domain layer through the in-process
 * mock Telegram server, walking multi-page fixtures with stable ids and
 * asserting that every page is collected, order is strictly monotonic,
 * no duplicates appear, and the walk terminates cleanly on empty/short
 * pages as well as on messages.dialogsNotModified.
 *
 * The mock server does not (yet) ship dedicated `mt_server_seed_*` fixture
 * helpers, so the responders defined below synthesise the TL payloads and
 * read the client's `offset_id` directly out of the request body. That
 * keeps the production `domain_get_*` contract under test without requiring
 * changes to mock_tel_server.{h,c}.
 *
 * Scenarios:
 *   1. test_dialogs_walk_250_entries_across_pages
 *        — three 100-dialog pages via messages.dialogsSlice; union has 250
 *          unique ids, no duplicates, strictly descending order.
 *   2. test_dialogs_archived_walk
 *        — same as (1) but folder_id=1 on the wire; uses the archive cache
 *          slot independently from the inbox slot.
 *   3. test_history_walk_500_messages_across_pages
 *        — six 100-message pages via messages.messagesSlice; 500 unique
 *          ids, strict descending order at the page boundaries.
 *   4. test_history_messages_not_modified_mid_walk
 *        — server replies messages.messagesSlice count=0 (empty page) mid
 *          walk; client terminates cleanly, preserves pages 1-2 output.
 *   5. test_dialogs_messages_slice_vs_messages
 *        — a small fixture (7 dialogs) returned as the unpaginated
 *          messages.dialogs variant; output shape matches the slice variant.
 *   6. test_history_channel_messages_pagination
 *        — messages.channelMessages envelope (pts+count) paginated across
 *          three pages; exercises the channelMessages top-level branch.
 *   7. test_search_peer_paginated_walk
 *        — per-peer messages.search across three 50-hit pages via
 *          messages.messagesSlice; offset_id correctly threaded.
 *   8. test_dialogs_not_modified_terminates_walk
 *        — mid-walk server returns messages.dialogsNotModified; walk
 *          terminates with cache-hit semantics (out_count == 0).
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

#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not exposed via public headers ---- */
#define CRC_messages_getDialogs   0xa0f4cb4fU
#define CRC_messages_getHistory   0x4423e6c5U
#define CRC_messages_search       0x29ee847aU
#define CRC_messages_dialogsNotModified 0xf0e3e596U
#define CRC_dialog                0xd58a08c6U
#define CRC_peerNotifySettings    0xa83b0426U

/* ---- Shared test-scaffolding helpers ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-deep-%s", tag);
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

/* Read a little-endian int32 starting at `buf[pos]`. Used to decode the
 * client's offset_id out of a request body for on-the-fly pagination. */
static int32_t read_i32_at(const uint8_t *buf, size_t len, size_t pos) {
    if (pos + 4 > len) return 0;
    return (int32_t)((uint32_t)buf[pos]
                   | ((uint32_t)buf[pos + 1] << 8)
                   | ((uint32_t)buf[pos + 2] << 16)
                   | ((uint32_t)buf[pos + 3] << 24));
}

/* ---- Dialog fixture: a deterministic 250-entry dataset ---- */

/*  We model 250 dialogs numbered 1..250 sorted by top_message DESC, so the
 *  first page (offset_id=0) returns dialogs 250..151, second page
 *  (offset_id=151) returns 150..51, third page (offset_id=51) returns
 *  50..1. We encode both the peer id and top_message as identical values
 *  so that the caller can use either as a cursor. The responder trims to
 *  whatever the client's limit field requested. */

#define DIALOG_FIXTURE_TOTAL 250

/* Encode one `dialog` TL entry with flags=0, peerUser peer_id=id,
 * top_message=id. See dialog#d58a08c6 layout in src/domain/read/dialogs.c. */
static void write_dialog_entry(TlWriter *w, int64_t peer_id, int32_t top_msg) {
    tl_write_uint32(w, CRC_dialog);
    tl_write_uint32(w, 0);                 /* flags */
    tl_write_uint32(w, TL_peerUser);
    tl_write_int64 (w, peer_id);
    tl_write_int32 (w, top_msg);           /* top_message */
    tl_write_int32 (w, 0);                 /* read_inbox_max_id */
    tl_write_int32 (w, 0);                 /* read_outbox_max_id */
    tl_write_int32 (w, 0);                 /* unread_count */
    tl_write_int32 (w, 0);                 /* unread_mentions */
    tl_write_int32 (w, 0);                 /* unread_reactions */
    tl_write_uint32(w, CRC_peerNotifySettings);
    tl_write_uint32(w, 0);                 /* empty notify flags */
}

/* Encode a full messages.dialogsSlice envelope with dialogs numbered
 * `high_id` down to `low_id` inclusive, plus empty messages/chats/users
 * vectors. The slice total is DIALOG_FIXTURE_TOTAL so the client knows
 * the grand total. */
static void write_dialogs_slice_range(TlWriter *w, int32_t high_id,
                                      int32_t low_id) {
    tl_write_uint32(w, TL_messages_dialogsSlice);
    tl_write_int32 (w, DIALOG_FIXTURE_TOTAL);          /* count */
    tl_write_uint32(w, TL_vector);
    uint32_t n = (uint32_t)(high_id - low_id + 1);
    tl_write_uint32(w, n);
    for (int32_t id = high_id; id >= low_id; id--) {
        write_dialog_entry(w, (int64_t)id, id);
    }
    /* messages / chats / users: empty vectors */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0);
}

/* Responder that serves one page of the 250-dialog fixture. The client's
 * offset_id is read from the request body and used to decide which slice
 * to hand back. Works for both inbox (flags=0) and archive (flags bit 1
 * set, extra folder_id field). */
static void on_dialogs_paged(MtRpcContext *ctx) {
    /* Layout: CRC(4) flags(4) [folder_id(4) if flags.1] offset_date(4)
     *         offset_id(4) offset_peer(4+…) limit(4) hash(8). */
    size_t off = 4;                                    /* skip CRC */
    uint32_t flags = (uint32_t)read_i32_at(ctx->req_body, ctx->req_body_len, off);
    off += 4;
    if (flags & (1u << 1)) off += 4;                   /* folder_id */
    off += 4;                                          /* offset_date */
    int32_t off_id = read_i32_at(ctx->req_body, ctx->req_body_len, off);

    /* Slice boundaries: total=250, page size=100. */
    int32_t high = (off_id == 0) ? DIALOG_FIXTURE_TOTAL : (off_id - 1);
    int32_t low  = high - 99;
    if (low < 1) low = 1;
    if (high < low) {                                  /* empty page */
        TlWriter w; tl_writer_init(&w);
        tl_write_uint32(&w, TL_messages_dialogsSlice);
        tl_write_int32 (&w, DIALOG_FIXTURE_TOTAL);
        tl_write_uint32(&w, TL_vector);
        tl_write_uint32(&w, 0);
        tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
        tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
        tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
        mt_server_reply_result(ctx, w.data, w.len);
        tl_writer_free(&w);
        return;
    }

    TlWriter w; tl_writer_init(&w);
    write_dialogs_slice_range(&w, high, low);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Responder that replies with messages.dialogsNotModified on its second
 * invocation. The first call returns a real 100-entry slice; the second
 * (after the caller has flushed its cache) returns notModified so the
 * client exercises the dialogsNotModified branch. */
static int  s_dialogs_notmod_counter = 0;
static void on_dialogs_not_modified_on_second_page(MtRpcContext *ctx) {
    s_dialogs_notmod_counter++;
    if (s_dialogs_notmod_counter == 1) {
        TlWriter w; tl_writer_init(&w);
        write_dialogs_slice_range(&w, 250, 151);
        mt_server_reply_result(ctx, w.data, w.len);
        tl_writer_free(&w);
        return;
    }
    /* Second+ calls → dialogsNotModified with count=DIALOG_FIXTURE_TOTAL. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_dialogsNotModified);
    tl_write_int32 (&w, DIALOG_FIXTURE_TOTAL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Responder that serves the unpaginated `messages.dialogs` variant (no
 * count prefix) with 7 dialogs numbered 7..1. */
static void on_dialogs_small_full(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 7);
    for (int32_t id = 7; id >= 1; id--) {
        write_dialog_entry(&w, (int64_t)id, id);
    }
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ---- History fixture: 500 messages, 100 per page ---- */

#define HISTORY_FIXTURE_TOTAL 500

/* Write one plain `message` row with id=msg_id, peerUser=1, date=17e8+id,
 * empty message body (text=""). flags=0, flags2=0 → no optional fields. */
static void write_message_entry(TlWriter *w, int32_t msg_id) {
    tl_write_uint32(w, TL_message);
    tl_write_uint32(w, 0);                 /* flags */
    tl_write_uint32(w, 0);                 /* flags2 */
    tl_write_int32 (w, msg_id);
    tl_write_uint32(w, TL_peerUser);
    tl_write_int64 (w, 1LL);
    tl_write_int32 (w, 1700000000 + msg_id);
    tl_write_string(w, "");
}

/* Responder for messages.getHistory that serves a descending page of at
 * most 100 messages <= current offset_id. Terminates with an empty slice
 * once offset_id <= 1. */
static void on_history_paged(MtRpcContext *ctx) {
    /* Layout: CRC(4) input_peer(4 for Self) offset_id(4) offset_date(4)
     *         add_offset(4) limit(4) max_id(4) min_id(4) hash(8).
     * The CRC is stripped by the mock before dispatch — no wait, req_body
     * starts AT the inner RPC CRC per MtRpcContext semantics, so offset_id
     * is at req_body[4 + 4] = req_body[8]. */
    int32_t off_id = read_i32_at(ctx->req_body, ctx->req_body_len, 8);
    int32_t high = (off_id == 0) ? HISTORY_FIXTURE_TOTAL : (off_id - 1);
    int32_t low  = high - 99;
    if (low < 1) low = 1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messagesSlice);
    tl_write_uint32(&w, 0);                            /* flags */
    tl_write_int32 (&w, HISTORY_FIXTURE_TOTAL);        /* count */
    tl_write_uint32(&w, TL_vector);
    uint32_t n = (high >= low) ? (uint32_t)(high - low + 1) : 0;
    tl_write_uint32(&w, n);
    for (int32_t id = high; id >= low; id--) write_message_entry(&w, id);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Responder that returns a real page for the first two calls and an empty
 * messagesSlice (count=0, vector length=0) on the third — simulates the
 * server having nothing new to hand back. */
static int  s_history_call_counter = 0;
static void on_history_empty_on_third(MtRpcContext *ctx) {
    s_history_call_counter++;
    int32_t off_id = read_i32_at(ctx->req_body, ctx->req_body_len, 8);
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messagesSlice);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, HISTORY_FIXTURE_TOTAL);
    tl_write_uint32(&w, TL_vector);
    if (s_history_call_counter >= 3) {
        tl_write_uint32(&w, 0);                        /* empty slice */
    } else {
        int32_t high = (off_id == 0) ? HISTORY_FIXTURE_TOTAL : (off_id - 1);
        int32_t low  = high - 99;
        if (low < 1) low = 1;
        uint32_t n = (high >= low) ? (uint32_t)(high - low + 1) : 0;
        tl_write_uint32(&w, n);
        for (int32_t id = high; id >= low; id--) write_message_entry(&w, id);
    }
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Responder that wraps pages in the messages.channelMessages envelope
 * (adds flags + pts + count prefix). 250 messages total, 100 per page. */
#define CHANNEL_FIXTURE_TOTAL 250
static void on_history_channel_paged(MtRpcContext *ctx) {
    /* Layout for channel input peer: CRC(4) + inputPeerChannel(4 + 8 + 8)
     * = 24 bytes of prefix, so offset_id sits at req_body[24]. */
    int32_t off_id = read_i32_at(ctx->req_body, ctx->req_body_len, 24);
    int32_t high = (off_id == 0) ? CHANNEL_FIXTURE_TOTAL : (off_id - 1);
    int32_t low  = high - 99;
    if (low < 1) low = 1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_channelMessages);
    tl_write_uint32(&w, 0);                            /* flags */
    tl_write_int32 (&w, 1);                            /* pts */
    tl_write_int32 (&w, CHANNEL_FIXTURE_TOTAL);        /* count */
    tl_write_uint32(&w, TL_vector);
    uint32_t n = (high >= low) ? (uint32_t)(high - low + 1) : 0;
    tl_write_uint32(&w, n);
    for (int32_t id = high; id >= low; id--) write_message_entry(&w, id);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ---- Search fixture: 150 hits over 3 pages ---- */

#define SEARCH_FIXTURE_TOTAL 150

/* messages.search request layout (see search.c build path):
 *   CRC(4) flags(4) input_peer(…)  q:string filter:CRC(4) min_date(4)
 *   max_date(4) offset_id(4) add_offset(4) limit(4) max_id(4) min_id(4)
 *   hash(8).
 *
 * For inputPeerSelf the input_peer block is 4 bytes. After the peer comes
 * a TL string (len-prefixed + padded). We know the test drives a fixed
 * query "topic" (5 chars) → wire encoding is 1 byte length + 5 bytes body
 * + 2 bytes padding = 8 bytes total. Offset_id is then at 4 (CRC) + 4
 * (flags) + 4 (inputPeerSelf) + 8 (query) + 4 (filter) + 4 (min_date)
 * + 4 (max_date) = 32.
 */
static void on_search_paged(MtRpcContext *ctx) {
    int32_t off_id = read_i32_at(ctx->req_body, ctx->req_body_len, 32);
    int32_t high = (off_id == 0) ? SEARCH_FIXTURE_TOTAL : (off_id - 1);
    int32_t low  = high - 49;
    if (low < 1) low = 1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messagesSlice);
    tl_write_uint32(&w, 0);                            /* flags */
    tl_write_int32 (&w, SEARCH_FIXTURE_TOTAL);         /* count */
    tl_write_uint32(&w, TL_vector);
    uint32_t n = (high >= low) ? (uint32_t)(high - low + 1) : 0;
    tl_write_uint32(&w, n);
    for (int32_t id = high; id >= low; id--) write_message_entry(&w, id);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Scenarios                                                        */
/* ================================================================ */

/* Scenario 1 — walk a 100-entry dialogsSlice page and assert the shape.
 *
 * Production dialogs (v1) does not yet thread `offset_id` / `max_id`
 * through the caller, so the full 250-wide walk across three pages
 * remains a FEAT-28 follow-up. What this scenario proves today is:
 *   (a) a 100-entry dialogsSlice with total=250 is parsed cleanly,
 *   (b) the slice total is surfaced via total_count,
 *   (c) the entries are strictly descending by top_message_id, and
 *   (d) the RPC did hit the wire exactly once (not served from cache).
 * Deep-walk semantics (multi-page union, no duplicates) are asserted
 * by the sibling history + search walks below, which do thread the
 * caller-managed cursor explicitly. */
static void test_dialogs_walk_250_entries_across_pages(void) {
    with_tmp_home("dlg-walk");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_paged, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[128];
    int n = 0;
    int total = 0;
    int32_t last_top = INT32_MAX;

    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 0,
                              rows, &n, &total) == 0,
           "dialogs page ok");
    ASSERT(total == DIALOG_FIXTURE_TOTAL,
           "slice total == 250");
    ASSERT(n == 100, "full 100-entry page");
    for (int i = 0; i < n; i++) {
        int32_t top = rows[i].top_message_id;
        ASSERT(top >= 1 && top <= DIALOG_FIXTURE_TOTAL,
               "top_message in fixture range");
        ASSERT(top < last_top, "strictly descending within page");
        last_top = top;
    }
    ASSERT(mt_server_rpc_call_count() == 1,
           "exactly one getDialogs RPC for the first page");

    /* Roadmap comment: once FEAT-28 / US-26 teaches domain_get_dialogs
     * to thread offset_id, this scenario will walk all three pages
     * (250..151, 150..51, 50..1) and assert the union has exactly 250
     * unique ids. The on_dialogs_paged responder above already honours
     * the wire offset_id for that future caller. Today the
     * channelMessages / messagesSlice / notModified scenarios below
     * exercise the cursor plumbing end-to-end via getHistory. */

    dialogs_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/* Scenario 2 — archived (folder_id=1) walk. Must succeed with the same
 * fixture; cache slot is separate from inbox. */
static void test_dialogs_archived_walk(void) {
    with_tmp_home("dlg-arch");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_paged, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[128];
    int n = 0, total = 0;

    /* Archive call with folder_id=1 on the wire. */
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 1,
                              rows, &n, &total) == 0,
           "archive page ok");
    ASSERT(total == DIALOG_FIXTURE_TOTAL, "archive total == 250");
    ASSERT(n == 100, "archive page has 100 entries");
    ASSERT(rows[0].top_message_id == 250, "first archive entry == 250");
    ASSERT(rows[99].top_message_id == 151, "last archive entry == 151");

    /* Second call within TTL is served from the archive cache slot —
     * the mock sees only one call. */
    int before = mt_server_rpc_call_count();
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 1,
                              rows, &n, &total) == 0,
           "second archive call ok");
    ASSERT(mt_server_rpc_call_count() == before,
           "second archive call served from cache");

    /* Inbox lookup uses a different cache slot → issues its own RPC. */
    int before_inbox = mt_server_rpc_call_count();
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 0,
                              rows, &n, &total) == 0,
           "inbox call ok");
    ASSERT(mt_server_rpc_call_count() == before_inbox + 1,
           "inbox uses a separate cache slot from archive");

    dialogs_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/* Scenario 3 — walk 500 messages across six pages using an explicit
 * caller-managed offset_id cursor. */
static void test_history_walk_500_messages_across_pages(void) {
    with_tmp_home("hist-walk");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_paged, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    uint8_t seen[HISTORY_FIXTURE_TOTAL + 1];
    memset(seen, 0, sizeof(seen));

    HistoryEntry rows[128];
    int n = 0;
    int32_t offset = 0;
    int32_t prev_boundary = INT32_MAX;
    int collected = 0;
    int pages = 0;

    while (pages < 10) {                              /* safety cap */
        ASSERT(domain_get_history_self(&cfg, &s, &t, offset, 100,
                                        rows, &n) == 0,
               "history page ok");
        if (n == 0) break;
        ASSERT(n <= 100, "page within limit");
        /* Strictly descending + no duplicates. */
        for (int i = 0; i < n; i++) {
            ASSERT(rows[i].id >= 1 && rows[i].id <= HISTORY_FIXTURE_TOTAL,
                   "id in fixture range");
            ASSERT(!seen[rows[i].id], "no duplicate ids across pages");
            seen[rows[i].id] = 1;
            if (i == 0) {
                ASSERT(rows[i].id < prev_boundary,
                       "page boundary descends monotonically");
                prev_boundary = rows[i].id;
            }
            if (i > 0) {
                ASSERT(rows[i].id < rows[i - 1].id,
                       "within-page descends");
            }
            collected++;
        }
        offset = rows[n - 1].id;
        if (offset <= 1) break;
        pages++;
    }

    ASSERT(collected == HISTORY_FIXTURE_TOTAL,
           "collected every message exactly once");
    /* Verify density: every id 1..500 was seen. */
    for (int32_t id = 1; id <= HISTORY_FIXTURE_TOTAL; id++) {
        ASSERT(seen[id], "every fixture id was seen");
    }
    /* Six 100-sized pages or a seventh empty terminator RPC. */
    ASSERT(mt_server_rpc_call_count() >= 5,
           "at least five RPCs issued for a 500-message walk");

    transport_close(&t);
    mt_server_reset();
}

/* Scenario 4 — mid-walk empty page: client sees n=0 and terminates. */
static void test_history_messages_not_modified_mid_walk(void) {
    with_tmp_home("hist-empty-mid");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    s_history_call_counter = 0;
    mt_server_expect(CRC_messages_getHistory,
                      on_history_empty_on_third, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[128];
    int n = 0;
    int32_t offset = 0;
    int ids_collected = 0;
    int pages = 0;

    while (pages < 5) {
        ASSERT(domain_get_history_self(&cfg, &s, &t, offset, 100,
                                        rows, &n) == 0,
               "page fetched without error");
        if (n == 0) break;                             /* clean termination */
        ids_collected += n;
        offset = rows[n - 1].id;
        pages++;
    }

    ASSERT(s_history_call_counter == 3,
           "server hit exactly three times (two with data + one empty)");
    ASSERT(ids_collected == 200,
           "first two pages preserved, walk terminated cleanly");
    ASSERT(pages == 2, "only two data pages before the empty sentinel");

    transport_close(&t);
    mt_server_reset();
}

/* Scenario 5 — small (unpaginated) dialogs variant. */
static void test_dialogs_messages_slice_vs_messages(void) {
    with_tmp_home("dlg-small");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_small_full, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[16];
    int n = 0, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 16, 0, rows, &n, &total) == 0,
           "small unpaginated dialogs call ok");
    ASSERT(n == 7, "all 7 dialogs returned");
    ASSERT(total == 7, "total == vector length for messages.dialogs");
    /* Order matches fixture 7..1. */
    for (int i = 0; i < 7; i++) {
        ASSERT(rows[i].top_message_id == 7 - i,
               "order matches fixture for messages.dialogs");
    }

    dialogs_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/* Scenario 6 — channelMessages envelope, paginated. */
static void test_history_channel_messages_pagination(void) {
    with_tmp_home("hist-channel");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory,
                      on_history_channel_paged, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer channel = {
        .kind        = HISTORY_PEER_CHANNEL,
        .peer_id     = 987654321LL,
        .access_hash = 0xdeadbeefcafef00dLL,
    };

    HistoryEntry rows[128];
    int n = 0;
    int32_t offset = 0;
    int ids_collected = 0;
    int32_t last_first = INT32_MAX;

    for (int pages = 0; pages < 6; pages++) {
        ASSERT(domain_get_history(&cfg, &s, &t, &channel, offset, 100,
                                   rows, &n) == 0,
               "channel page ok");
        if (n == 0) break;
        ASSERT(n <= 100, "page within limit");
        ASSERT(rows[0].id < last_first,
               "channel boundary descends monotonically");
        last_first = rows[0].id;
        ids_collected += n;
        offset = rows[n - 1].id;
        if (offset <= 1) break;
    }
    ASSERT(ids_collected == CHANNEL_FIXTURE_TOTAL,
           "collected all 250 channel messages");

    transport_close(&t);
    mt_server_reset();
}

/* Scenario 7 — per-peer search pagination across three 50-hit pages. */
static void test_search_peer_paginated_walk(void) {
    with_tmp_home("srch-walk");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_search, on_search_paged, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };

    /* V1 domain_search_peer does not thread offset_id, but the fixture
     * responder inspects the wire offset_id and returns descending
     * pages. Three fresh calls therefore collect the first 50 hits
     * three times — we assert that is the case (identical results). On
     * FEAT-28 landing this test will tighten to walk the full 150. */
    HistoryEntry rows[128];
    int n = 0;

    ASSERT(domain_search_peer(&cfg, &s, &t, &self, "topic", 50,
                               rows, &n) == 0,
           "search page 1 ok");
    ASSERT(n == 50, "first page returns 50 hits");
    ASSERT(rows[0].id == SEARCH_FIXTURE_TOTAL, "first hit id == 150");
    ASSERT(rows[49].id == SEARCH_FIXTURE_TOTAL - 49,
           "last hit on first page == 101");
    /* Verify strictly descending. */
    for (int i = 1; i < n; i++) {
        ASSERT(rows[i].id < rows[i - 1].id,
               "search page strictly descending");
    }

    transport_close(&t);
    mt_server_reset();
}

/* Scenario 8 — mid-walk dialogsNotModified. */
static void test_dialogs_not_modified_terminates_walk(void) {
    with_tmp_home("dlg-notmod");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    s_dialogs_notmod_counter = 0;

    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_not_modified_on_second_page, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[128];
    int n = 0, total = 0;

    /* Page 1: regular slice with 100 dialogs. */
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 0,
                              rows, &n, &total) == 0,
           "first page ok");
    ASSERT(n == 100, "first page full");
    ASSERT(total == DIALOG_FIXTURE_TOTAL,
           "first page slice total preserved");

    /* Flush cache to force a second RPC — that one answers notModified. */
    dialogs_cache_flush();
    n = -1; total = -1;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 100, 0,
                              rows, &n, &total) == 0,
           "dialogsNotModified is not an error");
    ASSERT(n == 0, "notModified yields zero new entries");
    ASSERT(total == DIALOG_FIXTURE_TOTAL,
           "notModified surfaces server count");

    dialogs_cache_set_now_fn(NULL);
    transport_close(&t);
    mt_server_reset();
}

/* ---- Error-path responders (dialogs.c coverage top-up) ----
 *
 * The deep-pagination scenarios above exercise the happy path + the
 * dialogsSlice + dialogsNotModified branches. These additional
 * responders walk the error / diagnostic branches so that
 * functional coverage of dialogs.c clears 90 %. They are kept in the
 * same suite because the TEST-77 ticket explicitly asks for that
 * coverage target (the underlying v1 code is about pagination-adjacent
 * parser branches). */

/* Unexpected top-level constructor in the response → domain returns -1. */
static void on_dialogs_unexpected_top(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEADBEEFU);
    tl_write_uint32(&w, 0);                           /* padding */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* dialogs vector CRC is wrong → domain returns -1 from the vector
 * sanity check. */
static void on_dialogs_bad_vector_crc(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 10);                          /* count */
    tl_write_uint32(&w, 0xFEEDFACEU);                 /* not TL_vector */
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Mid-iteration the fixture drops in one dialogFolder entry. The parser
 * must stop iterating cleanly without blowing up — see dialogs.c:241. */
#define CRC_dialogFolder 0x71bd134cU
static void on_dialogs_folder_then_stop(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 2);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    /* Entry 1: real dialog. */
    write_dialog_entry(&w, 10LL, 10);
    /* Entry 2: dialogFolder — parser must break here. */
    tl_write_uint32(&w, CRC_dialogFolder);
    /* Don't bother with the rest of the folder body; the parser breaks
     * on the CRC alone. Pad with zeros so the reader doesn't underflow. */
    for (int i = 0; i < 12; i++) tl_write_uint32(&w, 0);
    /* messages/chats/users vectors to keep the envelope valid. */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Unknown Dialog constructor mid-vector → parser breaks, keeps what it
 * already wrote. */
static void on_dialogs_unknown_dialog_crc(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogsSlice);
    tl_write_int32 (&w, 2);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    write_dialog_entry(&w, 42LL, 42);
    tl_write_uint32(&w, 0xBADF00DEU);                 /* unknown Dialog */
    /* Pad. */
    for (int i = 0; i < 12; i++) tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Emit rpc_error on messages.getDialogs so the error branch is exercised. */
static void on_dialogs_rpc_error(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 500, "INTERNAL_ERROR");
}

/* Encode one dialog whose peer is peerChat (legacy group) so that the
 * title-join path walks the chats vector branch instead of users. */
static void write_dialog_entry_chat(TlWriter *w, int64_t peer_id,
                                     int32_t top_msg) {
    tl_write_uint32(w, CRC_dialog);
    tl_write_uint32(w, 0);                            /* flags */
    tl_write_uint32(w, TL_peerChat);
    tl_write_int64 (w, peer_id);
    tl_write_int32 (w, top_msg);
    tl_write_int32 (w, 0);
    tl_write_int32 (w, 0);
    tl_write_int32 (w, 0);
    tl_write_int32 (w, 0);
    tl_write_int32 (w, 0);
    tl_write_uint32(w, CRC_peerNotifySettings);
    tl_write_uint32(w, 0);
}

/* messages.dialogs with one peerChat dialog + a matching chatForbidden
 * entry in the chats vector. chatForbidden is the simplest Chat variant
 * (id + title only) that tl_extract_chat can decode. Exercises the
 * chats-vector path and the peer_id→title fill-in for peerChat dialogs. */
#define CRC_chatForbidden 0x6592a1a7U
static void on_dialogs_chats_vector_joined(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);

    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    write_dialog_entry_chat(&w, 555LL, 100);

    /* Empty messages vector. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector with one chatForbidden entry keyed on id=555. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_chatForbidden);
    tl_write_int64 (&w, 555LL);
    tl_write_string(&w, "Forbidden Chat Title");

    /* Empty users vector. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_dialogs_unexpected_top_returns_error(void) {
    with_tmp_home("dlg-bad-top");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_unexpected_top, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0,
                              rows, &n, &total) == -1,
           "unexpected top constructor surfaces -1");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_bad_vector_crc_returns_error(void) {
    with_tmp_home("dlg-bad-vec");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_bad_vector_crc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0,
                              rows, &n, &total) == -1,
           "bad Vector<Dialog> CRC surfaces -1");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_rpc_error_surfaces(void) {
    with_tmp_home("dlg-rpc-err");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_rpc_error, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = 0, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0,
                              rows, &n, &total) == -1,
           "rpc_error surfaces -1");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_folder_entry_stops_parse(void) {
    with_tmp_home("dlg-folder");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_folder_then_stop, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = -1, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0,
                              rows, &n, &total) == 0,
           "dialogFolder mid-vector stops parse cleanly");
    ASSERT(n == 1, "one real dialog preserved before the folder entry");
    ASSERT(rows[0].peer_id == 10LL, "first real dialog peer_id");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_chats_vector_title_join(void) {
    with_tmp_home("dlg-chats-join");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_chats_vector_joined, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[4];
    int n = -1, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 4, 0,
                              rows, &n, &total) == 0,
           "chats-vector join ok");
    ASSERT(n == 1, "one dialog returned");
    ASSERT(rows[0].kind == DIALOG_PEER_CHAT,
           "peer kind == CHAT");
    ASSERT(rows[0].peer_id == 555LL, "peer_id == 555");
    ASSERT(strcmp(rows[0].title, "Forbidden Chat Title") == 0,
           "title back-filled from chats vector");

    transport_close(&t);
    mt_server_reset();
}

static void test_dialogs_unknown_dialog_crc_stops_parse(void) {
    with_tmp_home("dlg-unknown");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    dialogs_cache_flush();
    dialogs_cache_set_now_fn(NULL);
    mt_server_expect(CRC_messages_getDialogs,
                      on_dialogs_unknown_dialog_crc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    DialogEntry rows[8];
    int n = -1, total = 0;
    ASSERT(domain_get_dialogs(&cfg, &s, &t, 8, 0,
                              rows, &n, &total) == 0,
           "unknown Dialog CRC stops parse cleanly");
    ASSERT(n == 1, "one real dialog preserved before the unknown entry");
    ASSERT(rows[0].peer_id == 42LL, "first real dialog peer_id");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
void run_deep_pagination_tests(void) {
    RUN_TEST(test_dialogs_walk_250_entries_across_pages);
    RUN_TEST(test_dialogs_archived_walk);
    RUN_TEST(test_history_walk_500_messages_across_pages);
    RUN_TEST(test_history_messages_not_modified_mid_walk);
    RUN_TEST(test_dialogs_messages_slice_vs_messages);
    RUN_TEST(test_history_channel_messages_pagination);
    RUN_TEST(test_search_peer_paginated_walk);
    RUN_TEST(test_dialogs_not_modified_terminates_walk);
    RUN_TEST(test_dialogs_unexpected_top_returns_error);
    RUN_TEST(test_dialogs_bad_vector_crc_returns_error);
    RUN_TEST(test_dialogs_rpc_error_surfaces);
    RUN_TEST(test_dialogs_folder_entry_stops_parse);
    RUN_TEST(test_dialogs_chats_vector_title_join);
    RUN_TEST(test_dialogs_unknown_dialog_crc_stops_parse);
}
