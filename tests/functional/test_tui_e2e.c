/**
 * @file test_tui_e2e.c
 * @brief TEST-11 — TUI end-to-end functional tests.
 *
 * Drives the TUI view-model layer (dialog_pane, history_pane, tui_app_paint)
 * against the in-process mock Telegram server.  This sits one level above the
 * existing unit tests (test_tui_app.c etc.) which exercise the state machine
 * in total isolation: here the domain calls actually fire MTProto RPCs against
 * the mock, so the full path
 *
 *   dialog_pane_refresh → domain_get_dialogs → MTProto → mock responder
 *   history_pane_load   → domain_get_history → MTProto → mock responder
 *   tui_app_paint       → back-buffer contains dialog title + message text
 *
 * is exercised with real MTProto framing and TL parsing.
 *
 * No PTY is required: the Screen writes to a FILE* that is swapped to
 * /dev/null (we only inspect the back buffer, not the ANSI byte stream).
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

#include "tui/app.h"
#include "tui/dialog_pane.h"
#include "tui/history_pane.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- CRCs not surfaced in tl_registry.h ---- */
#define CRC_dialog                0xd58a08c6U
#define CRC_peerNotifySettings    0xa83b0426U
#define CRC_messages_getDialogs   0xa0f4cb4fU
#define CRC_messages_getHistory   0x4423e6c5U

/* ---- Helpers (same pattern as test_read_path.c) ---- */

static void with_tmp_home_tui(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-tui-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock_tui(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg_tui(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id   = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session_tui(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load session");
}

/* ---- Mock responders ---- */

/**
 * messages.dialogs with one user-peer dialog (id=555, unread=2, title="Alice").
 *
 * The users vector carries one user with:
 *   flags.0  → access_hash present
 *   flags.1  → first_name present  (used as title for user dialogs)
 * so the domain layer will join title = "Alice".
 */
static void on_dialogs_alice(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);

    /* dialogs: Vector<Dialog> with 1 entry */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_dialog);
    tl_write_uint32(&w, 0);              /* flags = 0, no optional fields */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 555LL);
    tl_write_int32 (&w, 1200);           /* top_message */
    tl_write_int32 (&w, 0);             /* read_inbox_max_id */
    tl_write_int32 (&w, 0);             /* read_outbox_max_id */
    tl_write_int32 (&w, 2);             /* unread_count */
    tl_write_int32 (&w, 0);             /* unread_mentions_count */
    tl_write_int32 (&w, 0);             /* unread_reactions_count */
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0);

    /* messages vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    /* users vector: one user — flags.0 (access_hash) | flags.1 (first_name) */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, (1u << 0) | (1u << 1)); /* flags */
    tl_write_uint32(&w, 0);                      /* flags2 */
    tl_write_int64 (&w, 555LL);
    tl_write_int64 (&w, 0xAABBCCDDEEFF0011LL);   /* access_hash (flags.0) */
    tl_write_string(&w, "Alice");                 /* first_name  (flags.1) */

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/**
 * messages.messages with one plain-text inbound message:
 *   id=42, text="Hello TUI world", date=1700000000
 */
static void on_history_one_text(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);

    /* messages vector: 1 entry */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0);              /* flags  = 0 (no from_id etc.) */
    tl_write_uint32(&w, 0);              /* flags2 = 0 */
    tl_write_int32 (&w, 42);            /* id */
    /* peer_id: peerUser id=555 (flags.28 off → no saved_peer) */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 555LL);
    tl_write_int32 (&w, 1700000000);    /* date */
    tl_write_string(&w, "Hello TUI world"); /* message */

    /* chats vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* users vector: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ---- Utility: scan the back buffer for a substring ---- */

/**
 * Return 1 if the string @p needle appears as consecutive codepoints in any
 * row of the Screen back buffer, 0 otherwise.
 */
static int screen_back_contains(const Screen *sc, const char *needle) {
    int nlen = (int)strlen(needle);
    if (nlen == 0) return 1;
    int total = sc->rows * sc->cols;
    for (int start = 0; start <= total - nlen; start++) {
        int match = 1;
        for (int k = 0; k < nlen && match; k++) {
            if (sc->back[start + k].cp != (uint32_t)(unsigned char)needle[k])
                match = 0;
        }
        if (match) return 1;
    }
    return 0;
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * TEST-11a: dialog_pane_refresh fires messages.getDialogs, parses the
 * response and stores the dialog entry.  The back buffer rendered by
 * tui_app_paint contains the dialog title "Alice".
 */
static void test_tui_dialog_refresh_paints_title(void) {
    with_tmp_home_tui("dlg-paint");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session_tui(&s);
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_alice, NULL);

    ApiConfig cfg; init_cfg_tui(&cfg);
    Transport t; connect_mock_tui(&t);

    TuiApp app;
    ASSERT(tui_app_init(&app, 24, 80) == 0, "app init");
    app.screen.out = fopen("/dev/null", "w");   /* silence ANSI bytes */

    ASSERT(dialog_pane_refresh(&app.dialogs, &cfg, &s, &t) == 0,
           "dialog_pane_refresh ok");
    app.dialogs.lv.rows_visible = app.layout.dialogs.rows;

    ASSERT(app.dialogs.count == 1, "one dialog loaded");
    ASSERT(strcmp(app.dialogs.entries[0].title, "Alice") == 0,
           "title == Alice");
    ASSERT(app.dialogs.entries[0].peer_id == 555LL, "peer_id == 555");
    ASSERT(app.dialogs.entries[0].unread_count == 2, "unread_count == 2");

    tui_app_paint(&app);

    ASSERT(screen_back_contains(&app.screen, "Alice"),
           "back buffer contains 'Alice' after paint");

    if (app.screen.out != stdout) fclose(app.screen.out);
    tui_app_free(&app);
    transport_close(&t);
    mt_server_reset();
}

/**
 * TEST-11b: history_pane_load fires messages.getHistory for the selected
 * dialog's peer and stores the message.  After tui_app_paint the back buffer
 * contains the message text.
 */
static void test_tui_history_load_paints_message(void) {
    with_tmp_home_tui("hist-paint");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session_tui(&s);

    /* Seed two expected RPC calls: dialogs then history. */
    mt_server_expect(CRC_messages_getDialogs, on_dialogs_alice, NULL);
    mt_server_expect(CRC_messages_getHistory, on_history_one_text, NULL);

    ApiConfig cfg; init_cfg_tui(&cfg);
    Transport t; connect_mock_tui(&t);

    TuiApp app;
    ASSERT(tui_app_init(&app, 24, 80) == 0, "app init");
    app.screen.out = fopen("/dev/null", "w");

    /* Phase 1: refresh dialogs. */
    ASSERT(dialog_pane_refresh(&app.dialogs, &cfg, &s, &t) == 0,
           "dialog_pane_refresh ok");
    app.dialogs.lv.rows_visible = app.layout.dialogs.rows;

    /* Phase 2: simulate Enter — open first dialog's history. */
    const DialogEntry *d = dialog_pane_selected(&app.dialogs);
    ASSERT(d != NULL, "a dialog is selected");

    HistoryPeer peer = {0};
    peer.kind        = HISTORY_PEER_USER;
    peer.peer_id     = d->peer_id;
    peer.access_hash = d->access_hash;

    ASSERT(history_pane_load(&app.history, &cfg, &s, &t, &peer) == 0,
           "history_pane_load ok");
    app.history.lv.rows_visible = app.layout.history.rows;

    ASSERT(app.history.count == 1, "one message loaded");
    ASSERT(app.history.entries[0].id == 42, "message id == 42");
    ASSERT(strcmp(app.history.entries[0].text, "Hello TUI world") == 0,
           "message text correct");

    tui_app_paint(&app);

    ASSERT(screen_back_contains(&app.screen, "Hello TUI world"),
           "back buffer contains message text after paint");

    if (app.screen.out != stdout) fclose(app.screen.out);
    tui_app_free(&app);
    transport_close(&t);
    mt_server_reset();
}

/**
 * TEST-11c: keypress sequence j → Enter → q exercises the full TUI event
 * loop against real MTProto data.  After Enter the history pane is loaded
 * (via the expected server responders), 'q' returns TUI_EVENT_QUIT, and
 * the final paint keeps both panes populated.
 */
static void test_tui_keypress_sequence_j_enter_q(void) {
    with_tmp_home_tui("key-seq");
    mt_server_init(); mt_server_reset();
    dialogs_cache_flush();
    MtProtoSession s; load_session_tui(&s);

    mt_server_expect(CRC_messages_getDialogs, on_dialogs_alice, NULL);
    mt_server_expect(CRC_messages_getHistory, on_history_one_text, NULL);

    ApiConfig cfg; init_cfg_tui(&cfg);
    Transport t; connect_mock_tui(&t);

    TuiApp app;
    ASSERT(tui_app_init(&app, 24, 80) == 0, "app init");
    app.screen.out = fopen("/dev/null", "w");

    /* Refresh dialogs (simulates TUI startup). */
    ASSERT(dialog_pane_refresh(&app.dialogs, &cfg, &s, &t) == 0,
           "dialog refresh");
    app.dialogs.lv.rows_visible = app.layout.dialogs.rows;

    /* 'j' — move selection down (from 0 to 1, clamped to 0 since only one). */
    TuiEvent ev = tui_app_handle_char(&app, 'j');
    ASSERT(ev == TUI_EVENT_REDRAW || ev == TUI_EVENT_NONE,
           "'j' returns REDRAW or NONE");

    /* Enter — open the selected dialog. */
    ev = tui_app_handle_key(&app, TERM_KEY_ENTER);
    ASSERT(ev == TUI_EVENT_OPEN_DIALOG, "Enter returns OPEN_DIALOG");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus shifted to history");

    /* Simulate the caller's response to OPEN_DIALOG: load history. */
    const DialogEntry *d = &app.dialogs.entries[0];
    HistoryPeer peer = {0};
    peer.kind        = HISTORY_PEER_USER;
    peer.peer_id     = d->peer_id;
    peer.access_hash = d->access_hash;
    ASSERT(history_pane_load(&app.history, &cfg, &s, &t, &peer) == 0,
           "history loaded after OPEN_DIALOG");
    app.history.lv.rows_visible = app.layout.history.rows;

    /* Final paint — both panes should now carry real data. */
    tui_app_paint(&app);
    ASSERT(screen_back_contains(&app.screen, "Alice"),
           "back buffer has dialog title");
    ASSERT(screen_back_contains(&app.screen, "Hello TUI world"),
           "back buffer has message text");

    /* 'q' — quit. */
    ev = tui_app_handle_char(&app, 'q');
    ASSERT(ev == TUI_EVENT_QUIT, "'q' returns QUIT");

    if (app.screen.out != stdout) fclose(app.screen.out);
    tui_app_free(&app);
    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_tui_e2e_tests(void) {
    RUN_TEST(test_tui_dialog_refresh_paints_title);
    RUN_TEST(test_tui_history_load_paints_message);
    RUN_TEST(test_tui_keypress_sequence_j_enter_q);
}
