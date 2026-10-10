/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Unit tests for the TUI state machine. */

/**
 * @file tests/unit/test_tui_app.c
 * @brief Unit tests for the TUI state machine (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/app.h"
#include "tui_grid.h"

#include <stdlib.h>
#include <string.h>

/* ---- key events: what libtuikit's decoder yields for real terminal bytes ---- */

static tui_event ev_key(tui_key k, unsigned mods) {
    tui_event e;
    memset(&e, 0, sizeof(e));
    e.type = TUI_EV_KEY;
    e.key  = k;
    e.mods = mods;
    return e;
}

static tui_event ev_char(uint32_t cp, unsigned mods) {
    tui_event e = ev_key(TUI_KEY_CHAR, mods);
    e.cp = cp;
    e.utf8[0] = (char)cp;
    return e;
}

static TuiEvent press(TuiApp *app, tui_key k) {
    tui_event e = ev_key(k, 0);
    return tui_app_handle_key(app, &e);
}

static TuiEvent type_char(TuiApp *app, uint32_t cp) {
    tui_event e = ev_char(cp, 0);
    return tui_app_handle_key(app, &e);
}

/* Decode raw terminal bytes and hand every key event to the app; returns the
 * last non-NONE action. */
static TuiEvent feed_bytes(TuiApp *app, const char *bytes, int *n_events) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_decoder_feed(&d, bytes, strlen(bytes));
    TuiEvent last = TUI_EVENT_NONE;
    int n = 0;
    tui_event e;
    while (tui_decoder_next(&d, &e) > 0) {
        n++;
        TuiEvent a = tui_app_handle_key(app, &e);
        if (a != TUI_EVENT_NONE) last = a;
    }
    if (tui_decoder_pending(&d)) {
        if (tui_decoder_timeout(&d, &e) > 0) {
            n++;
            TuiEvent a = tui_app_handle_key(app, &e);
            if (a != TUI_EVENT_NONE) last = a;
        }
    }
    tui_decoder_free(&d);
    if (n_events) *n_events = n;
    return last;
}

static DialogEntry mk_entry(DialogPeerKind kind, int64_t id,
                             const char *title) {
    DialogEntry e = {0};
    e.kind = kind;
    e.peer_id = id;
    if (title) {
        strncpy(e.title, title, sizeof(e.title) - 1);
    }
    return e;
}

static void seed_dialogs(TuiApp *app, int n) {
    DialogEntry src[5];
    if (n > 5) n = 5;
    for (int i = 0; i < n; i++) {
        const char *title = (const char *[]){"Alice","Bob","Carol","Dan","Eve"}[i];
        src[i] = mk_entry(DIALOG_PEER_USER, 1000 + i, title);
    }
    dialog_pane_set_entries(&app->dialogs, src, n);
}

/* --- Init / resize --- */

static void test_init_succeeds_on_valid_size(void) {
    TuiApp app;
    ASSERT(tui_app_init(&app, 24, 80) == 0, "init ok");
    ASSERT(app.rows == 24 && app.cols == 80, "size recorded");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "start focus dialogs");
    ASSERT(pane_is_valid(&app.layout.dialogs), "dialogs pane valid");
    ASSERT(pane_is_valid(&app.layout.history), "history pane valid");
    ASSERT(pane_is_valid(&app.layout.status), "status pane valid");
    /* Viewport heights should match pane rows. */
    ASSERT(app.dialogs.lv.rows_visible == app.layout.dialogs.rows,
           "dialog viewport matches pane");
    ASSERT(app.history.lv.rows_visible == app.layout.history.rows,
           "history viewport matches pane");
    tui_app_free(&app);
}

static void test_init_rejects_too_small(void) {
    TuiApp app;
    ASSERT(tui_app_init(&app, 0, 80) != 0, "rows=0 rejected");
    ASSERT(tui_app_init(&app, 24, 0) != 0, "cols=0 rejected");
}

static void test_resize_recomputes_layout(void) {
    TuiApp app;
    ASSERT(tui_app_init(&app, 24, 80) == 0, "init");
    seed_dialogs(&app, 3);
    tui_listview_end(&app.dialogs.lv);  /* dirty scroll */
    ASSERT(tui_app_resize(&app, 40, 120) == 0, "resize ok");
    ASSERT(app.rows == 40 && app.cols == 120, "new size");
    ASSERT(app.layout.status.cols == 120, "status widened");
    ASSERT(app.dialogs.lv.rows_visible == app.layout.dialogs.rows,
           "viewport follows resize");
    tui_app_free(&app);
}

/* --- Key handling --- */

static void test_ctrl_c_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    tui_event cc = ev_char('c', TUI_MOD_CTRL);
    TuiEvent ev = tui_app_handle_key(&app, &cc);
    ASSERT(ev == TUI_EVENT_QUIT, "ctrl-c returns QUIT");
    tui_app_free(&app);
}

static void test_q_never_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    ASSERT(type_char(&app, 'q') == TUI_EVENT_NONE, "'q' on the top level does nothing");
    ASSERT(type_char(&app, 'Q') == TUI_EVENT_NONE, "'Q' is unbound");
    press(&app, TUI_KEY_RIGHT);
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "in the history");
    ASSERT(type_char(&app, 'q') == TUI_EVENT_REDRAW, "'q' goes back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "back on the dialogs");
    ASSERT(app.status.mode == STATUS_MODE_DIALOGS, "hints follow");
    tui_app_free(&app);
}

static void test_backspace_goes_back_like_esc(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    ASSERT(press(&app, TUI_KEY_BACKSPACE) == TUI_EVENT_NONE, "nothing on the top level");
    press(&app, TUI_KEY_RIGHT);
    ASSERT(press(&app, TUI_KEY_BACKSPACE) == TUI_EVENT_REDRAW, "back from the history");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "dialogs again");
    tui_app_free(&app);
}

static void test_back_keeps_the_row_it_was_left_on(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 4);
    type_char(&app, 'j'); type_char(&app, 'j');
    press(&app, TUI_KEY_ENTER);
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "opened");
    press(&app, TUI_KEY_ESC);
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "Esc goes back");
    ASSERT(app.dialogs.lv.selected == 2, "same row");
    tui_app_free(&app);
}

static void test_function_keys_are_unbound(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    for (int k = 1; k <= 12; k++) {
        tui_event e = ev_key((tui_key)(TUI_KEY_F1 + k - 1), 0);
        ASSERT(tui_app_handle_key(&app, &e) == TUI_EVENT_NONE, "F-key does nothing");
    }
    ASSERT(!app.help_open && app.focus == TUI_FOCUS_DIALOGS
           && app.dialogs.lv.selected == 0, "state untouched");
    tui_app_free(&app);
}

static void test_left_right_switch_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    /* Start in dialogs; RIGHT should switch to history. */
    TuiEvent ev = press(&app, TUI_KEY_RIGHT);
    ASSERT(ev == TUI_EVENT_REDRAW, "right triggers redraw");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus moved to history");
    ASSERT(app.status.mode == STATUS_MODE_HISTORY, "status mode sync");
    /* RIGHT again is a no-op. */
    ev = press(&app, TUI_KEY_RIGHT);
    ASSERT(ev == TUI_EVENT_NONE, "right is no-op when already in history");
    ev = press(&app, TUI_KEY_LEFT);
    ASSERT(ev == TUI_EVENT_REDRAW, "left switches back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus back on dialogs");
    tui_app_free(&app);
}

static void test_vim_keys_switch_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = type_char(&app, 'l');
    ASSERT(ev == TUI_EVENT_REDRAW, "'l' switches to history");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus history");
    ev = type_char(&app, 'h');
    ASSERT(ev == TUI_EVENT_REDRAW, "'h' switches back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus dialogs");
    tui_app_free(&app);
}

static void test_arrow_keys_navigate_focused_pane(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 5);
    int initial = app.dialogs.lv.selected;
    press(&app, TUI_KEY_DOWN);
    ASSERT(app.dialogs.lv.selected == initial + 1, "down moved dialog cursor");
    press(&app, TUI_KEY_UP);
    ASSERT(app.dialogs.lv.selected == initial, "up reversed");
    press(&app, TUI_KEY_END);
    ASSERT(app.dialogs.lv.selected == 4, "end to last");
    press(&app, TUI_KEY_HOME);
    ASSERT(app.dialogs.lv.selected == 0, "home to first");
    tui_app_free(&app);
}

static void test_jk_chars_navigate(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    type_char(&app, 'j');
    ASSERT(app.dialogs.lv.selected == 1, "'j' is down");
    type_char(&app, 'k');
    ASSERT(app.dialogs.lv.selected == 0, "'k' is up");
    tui_app_free(&app);
}

static void test_navigation_targets_focused_pane_only(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 5);
    press(&app, TUI_KEY_RIGHT);  /* focus history */
    press(&app, TUI_KEY_DOWN);
    ASSERT(app.dialogs.lv.selected == 0, "dialogs cursor not moved");
    tui_app_free(&app);
}

static void test_enter_on_dialog_requests_load_and_shifts_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    TuiEvent ev = press(&app, TUI_KEY_ENTER);
    ASSERT(ev == TUI_EVENT_OPEN_DIALOG, "enter requests open");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus jumped to history");
    ASSERT(app.status.mode == STATUS_MODE_HISTORY, "status follows");
    tui_app_free(&app);
}

static void test_enter_with_no_dialog_is_noop(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = press(&app, TUI_KEY_ENTER);
    ASSERT(ev == TUI_EVENT_NONE, "enter on empty list is no-op");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus unchanged");
    tui_app_free(&app);
}

static void test_esc_on_top_level_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = press(&app, TUI_KEY_ESC);
    ASSERT(ev == TUI_EVENT_QUIT, "esc on the top level quits");
    tui_app_free(&app);
}

static void test_esc_in_history_goes_back_not_quit(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    press(&app, TUI_KEY_RIGHT);
    ASSERT(press(&app, TUI_KEY_ESC) == TUI_EVENT_REDRAW, "esc goes back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "dialogs focused");
    tui_app_free(&app);
}

static void test_r_requests_reload(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    type_char(&app, 'j');
    ASSERT(type_char(&app, 'r') == TUI_EVENT_RELOAD, "r reloads");
    ASSERT(app.dialogs.lv.selected == 1, "the cursor stays");
    tui_app_free(&app);
}

/* --- Help overlay --- */

static void test_help_overlay_is_bordered_titled_and_keeps_the_view(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    ASSERT(type_char(&app, '?') == TUI_EVENT_REDRAW, "? opens the help");
    ASSERT(app.help_open, "help is open");
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    ASSERT(tui_grid_contains(g, "Dialog list shortcuts"), "title names the view");
    ASSERT(tui_grid_contains(g, "ctrl-f"), "lists ctrl-f");
    ASSERT(tui_grid_contains(g, "reload"), "lists r");
    ASSERT(tui_grid_contains(g, "\xe2\x94\x8c") || tui_grid_contains(g, "+")
           || tui_grid_contains(g, "\xe2\x95\xad"), "has a border");
    char line[256];
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "Dialogs") != NULL, "the header stays visible");
    int cyan = 0;
    for (int r = 0; r < g->rows; r++)
        for (int c = 0; c < g->cols; c++)
            if (g->fg[r][c] == TUI_CYAN && (g->attrs[r][c] & TUI_ATTR_BOLD)) cyan = 1;
    ASSERT(cyan, "frame in the accent colour");
    free(g);
    tui_app_free(&app);
}

static void test_help_closes_with_esc_q_backspace_and_does_not_quit(void) {
    const tui_key keys[] = { TUI_KEY_ESC, TUI_KEY_BACKSPACE, TUI_KEY_CHAR };
    for (int i = 0; i < 3; i++) {
        TuiApp app; tui_app_init(&app, 24, 80);
        seed_dialogs(&app, 3);
        type_char(&app, '?');
        TuiEvent r = keys[i] == TUI_KEY_CHAR ? type_char(&app, 'q')
                                             : press(&app, keys[i]);
        ASSERT(r == TUI_EVENT_REDRAW, "closing redraws, never quits");
        ASSERT(!app.help_open, "help closed");
        ASSERT(app.focus == TUI_FOCUS_DIALOGS, "the view is where it was");
        tui_app_free(&app);
    }
}

static void test_help_other_keys_change_nothing_and_ctrl_c_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    type_char(&app, '?');
    type_char(&app, 'x'); type_char(&app, 'r'); type_char(&app, 'l');
    ASSERT(app.help_open, "still open");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS && app.dialogs.lv.selected == 0,
           "nothing moved behind it");
    tui_event cc = ev_char('c', TUI_MOD_CTRL);
    ASSERT(tui_app_handle_key(&app, &cc) == TUI_EVENT_QUIT, "ctrl-c quits at once");
    tui_app_free(&app);
}

static void test_history_help_names_its_view(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    press(&app, TUI_KEY_RIGHT);
    type_char(&app, '?');
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    ASSERT(tui_grid_contains(g, "Message history shortcuts"), "history title");
    free(g);
    tui_app_free(&app);
}

/* --- Paint --- */

static void test_paint_renders_all_three_panes(void) {
    TuiApp app; tui_app_init(&app, 10, 50);
    seed_dialogs(&app, 3);
    tui_app_paint(&app);
    TuiGrid g;
    tui_grid_capture(&g, app.screen);
    char line[256];

    /* Dialog pane: first row under the header is "u     Alice". */
    tui_grid_row_text(&g, 1, line, sizeof(line));
    ASSERT(strncmp(line, "u     Alice", 11) == 0, "dialog pane painted");

    /* History pane shows the placeholder on the middle row. */
    int mid = app.layout.history.row + app.layout.history.rows / 2;
    tui_grid_row_text(&g, mid, line, sizeof(line));
    ASSERT(strstr(line, "(select a dialog)") != NULL, "history pane shows hint");

    /* Status row carries the hints, in yellow, on the default background. */
    int status_row = app.layout.status.row;
    ASSERT(status_row == 9, "status on the last row");
    tui_grid_row_text(&g, status_row, line, sizeof(line));
    ASSERT(strstr(line, "[dialogs]") != NULL, "status row shows dialogs hint");
    ASSERT(g.fg[status_row][0] == TUI_YELLOW, "hints are yellow");
    ASSERT(g.bg[status_row][0] == TUI_COLOR_DEFAULT, "default background");
    tui_app_free(&app);
}

static void test_header_bar_text_and_colours(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    char line[256];
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "Dialogs") != NULL, "says where we are");
    ASSERT(strstr(line, "1 of 3") != NULL, "counts the rows");
    ASSERT(strstr(line, "tg-tui") != NULL, "names the program");
    ASSERT(strstr(line, "tg-tui") > strstr(line, "Dialogs"), "name on the right");
    for (int c = 0; c < 80; c++) {
        ASSERT(g->fg[0][c] == TUI_WHITE && g->bg[0][c] == TUI_BLUE,
               "white on blue across the row");
        ASSERT(g->attrs[0][c] & TUI_ATTR_BOLD, "bold");
    }
    ASSERT(g->bg[1][40] == TUI_COLOR_DEFAULT, "terminal background below");

    type_char(&app, 'j');
    press(&app, TUI_KEY_RIGHT);
    tui_app_paint(&app);
    tui_grid_capture(g, app.screen);
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "Messages") != NULL, "history view named");
    free(g);
    tui_app_free(&app);
}

static void test_header_clips_left_before_the_name(void) {
    TuiApp app; tui_app_init(&app, 10, 40);
    seed_dialogs(&app, 3);
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    char line[256];
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "tg-tui") != NULL, "name still there");
    free(g);
    tui_app_free(&app);
}

static void test_selected_dialog_row_is_filled_only_when_focused(void) {
    TuiApp app; tui_app_init(&app, 10, 50);
    seed_dialogs(&app, 3);
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    int w = app.layout.dialogs.cols;
    for (int c = 0; c < w; c++) {
        ASSERT(g->fg[1][c] == TUI_BLACK && g->bg[1][c] == TUI_CYAN
               && (g->attrs[1][c] & TUI_ATTR_BOLD),
               "selected row bold black on cyan across the pane width");
    }
    ASSERT(g->bg[2][0] == TUI_COLOR_DEFAULT, "other rows not highlighted");

    press(&app, TUI_KEY_RIGHT);          /* focus history */
    tui_app_paint(&app);
    tui_grid_capture(g, app.screen);
    ASSERT(g->bg[1][0] == TUI_COLOR_DEFAULT, "no fill when the pane lost focus");
    ASSERT(g->attrs[1][0] & TUI_ATTR_UNDERLINE, "still marked by an attribute");
    free(g);
    tui_app_free(&app);
}

static void test_status_message_is_cleared_by_the_next_key(void) {
    TuiApp app; tui_app_init(&app, 10, 80);
    seed_dialogs(&app, 3);
    status_row_set_message_kind(&app.status, "history: load failed",
                                STATUS_MSG_ERROR);
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    char line[256];
    tui_grid_row_text(g, 9, line, sizeof(line));
    ASSERT(strncmp(line, "history: load failed", 20) == 0, "message shown");
    ASSERT(g->fg[9][0] == TUI_RED && (g->attrs[9][0] & TUI_ATTR_BOLD),
           "in the error style");
    ASSERT(strstr(line, "[dialogs]") == NULL, "instead of the hints");

    ASSERT(type_char(&app, 'x') == TUI_EVENT_REDRAW, "an unbound key clears it");
    tui_app_paint(&app);
    tui_grid_capture(g, app.screen);
    tui_grid_row_text(g, 9, line, sizeof(line));
    ASSERT(strstr(line, "[dialogs]") != NULL, "hints are back");
    ASSERT(type_char(&app, 'x') == TUI_EVENT_NONE, "then it changes nothing");
    free(g);
    tui_app_free(&app);
}

static void test_too_small_terminal_shows_a_notice(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    ASSERT(tui_app_resize(&app, 2, 30) == 0, "resize");
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    ASSERT(tui_grid_contains(g, "terminal too small"), "notice shown");
    free(g);
    tui_app_free(&app);
}

static void test_empty_dialog_list_names_the_key(void) {
    TuiApp app; tui_app_init(&app, 10, 80);
    tui_app_paint(&app);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, app.screen);
    ASSERT(tui_grid_contains(g, "no dialogs - r reloads"), "says it is empty");
    char line[256];
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "Dialogs \xc2\xb7 0") != NULL, "header counts zero");
    free(g);
    tui_app_free(&app);
}

static void test_unread_dialog_is_bold(void) {
    TuiApp app; tui_app_init(&app, 10, 50);
    DialogEntry e = mk_entry(DIALOG_PEER_CHANNEL, 7, "News");
    e.unread_count = 12;
    dialog_pane_set_entries(&app.dialogs, &e, 1);
    tui_app_paint(&app);
    TuiGrid g;
    tui_grid_capture(&g, app.screen);
    char line[256];
    tui_grid_row_text(&g, 1, line, sizeof(line));
    ASSERT(strncmp(line, "c [12] News", 11) == 0, "unread count shown");
    ASSERT(g.attrs[1][0] & TUI_ATTR_BOLD, "unread row bold");
    tui_app_free(&app);
}

static void test_paint_does_not_write_to_the_terminal(void) {
    /* The screen is rendered by the caller (tui_screen_render); painting
     * only stages cells: a fresh render shows them, and a second render with
     * no change in between emits nothing. */
    TuiApp app; tui_app_init(&app, 10, 50);
    seed_dialogs(&app, 2);
    tui_app_paint(&app);
    tui_buf out;
    tui_buf_init(&out);
    ASSERT(tui_screen_render(app.screen, &out) > 0, "first render has bytes");
    tui_buf_reset(&out);
    tui_app_paint(&app);
    long n = tui_screen_render(app.screen, &out);
    ASSERT(n >= 0, "second render ok");
    ASSERT(strstr(out.data ? out.data : "", "Alice") == NULL,
           "an unchanged screen is not rewritten");
    tui_buf_free(&out);
    tui_app_free(&app);
}

/* TG-2: non-ASCII titles are measured and drawn correctly even in the C locale. */
static void test_non_ascii_titles_in_c_locale(void) {
    TuiApp app; tui_app_init(&app, 10, 50);
    DialogEntry e[2];
    e[0] = mk_entry(DIALOG_PEER_USER, 1, "Árvíztűrő tükörfúrógép");
    e[1] = mk_entry(DIALOG_PEER_USER, 2, "日本語");
    dialog_pane_set_entries(&app.dialogs, e, 2);
    tui_app_paint(&app);
    TuiGrid g;
    tui_grid_capture(&g, app.screen);
    char line[256];
    tui_grid_row_text(&g, 1, line, sizeof(line));
    ASSERT(strstr(line, "Árvíztűrő tükörfúrógép") != NULL,
           "accented title drawn whole");
    tui_grid_row_text(&g, 2, line, sizeof(line));
    ASSERT(strstr(line, "日本語") != NULL, "wide title drawn");
    /* the status row starts at column 0 of the last row: wide cells did not
     * shift the layout */
    tui_grid_row_text(&g, app.layout.status.row, line, sizeof(line));
    ASSERT(strncmp(line, "[dialogs]", 9) == 0, "status row intact");
    tui_app_free(&app);
}

/* TG-3: modified cursor / editing keys are consumed whole, never leaking
 * their tail ('B', '~', ';', digits) as typed characters or commands. */
static void test_modified_keys_do_not_leak_characters(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 5);
    int n = 0;
    /* Ctrl-Down, Shift-Up, Alt-Right, Ctrl-PgDn, Ctrl-Delete, Shift-F5 */
    const char *seqs[] = {
        "\x1b[1;5B", "\x1b[1;2A", "\x1b[1;3C", "\x1b[6;5~",
        "\x1b[3;5~", "\x1b[15;2~",
    };
    for (size_t i = 0; i < sizeof(seqs) / sizeof(seqs[0]); i++) {
        TuiEvent a = feed_bytes(&app, seqs[i], &n);
        ASSERT(n == 1, "one key event per modified sequence");
        ASSERT(a != TUI_EVENT_QUIT, "modified key must not quit");
        ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus unchanged");
    }
    /* 'j'/'k'/'q'-like tails would have moved the selection or quit. */
    ASSERT(app.dialogs.lv.selected == 0, "selection untouched");
    /* A plain key after them still works. */
    ASSERT(feed_bytes(&app, "\x1b[B", &n) == TUI_EVENT_REDRAW, "plain Down works");
    ASSERT(app.dialogs.lv.selected == 1, "plain Down moved");
    tui_app_free(&app);
}

static void test_page_keys_and_ctrl_f_b(void) {
    TuiApp app; tui_app_init(&app, 6, 50);   /* 5 list rows */
    DialogEntry src[40];
    for (int i = 0; i < 40; i++) src[i] = mk_entry(DIALOG_PEER_USER, i, "X");
    dialog_pane_set_entries(&app.dialogs, src, 40);
    ASSERT(press(&app, TUI_KEY_PGDN) == TUI_EVENT_REDRAW, "PgDn redraws");
    int after_page = app.dialogs.lv.selected;
    ASSERT(after_page >= 4, "PgDn moves one viewport");
    press(&app, TUI_KEY_PGUP);
    ASSERT(app.dialogs.lv.selected < after_page, "PgUp moves back");
    tui_event cf = ev_char('f', TUI_MOD_CTRL);
    ASSERT(tui_app_handle_key(&app, &cf) == TUI_EVENT_REDRAW, "Ctrl-F pages");
    tui_app_free(&app);
}

static void test_non_key_events_are_ignored(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    tui_event e;
    memset(&e, 0, sizeof(e));
    e.type = TUI_EV_RESIZE;
    ASSERT(tui_app_handle_key(&app, &e) == TUI_EVENT_NONE, "resize is not a key");
    ASSERT(tui_app_handle_key(&app, NULL) == TUI_EVENT_NONE, "NULL event");
    ASSERT(tui_app_handle_key(NULL, &e) == TUI_EVENT_NONE, "NULL app");
    tui_app_free(&app);
}

void test_tui_app_run(void) {
    RUN_TEST(test_init_succeeds_on_valid_size);
    RUN_TEST(test_init_rejects_too_small);
    RUN_TEST(test_resize_recomputes_layout);
    RUN_TEST(test_ctrl_c_quits);
    RUN_TEST(test_q_never_quits);
    RUN_TEST(test_backspace_goes_back_like_esc);
    RUN_TEST(test_back_keeps_the_row_it_was_left_on);
    RUN_TEST(test_function_keys_are_unbound);
    RUN_TEST(test_left_right_switch_focus);
    RUN_TEST(test_vim_keys_switch_focus);
    RUN_TEST(test_arrow_keys_navigate_focused_pane);
    RUN_TEST(test_jk_chars_navigate);
    RUN_TEST(test_navigation_targets_focused_pane_only);
    RUN_TEST(test_enter_on_dialog_requests_load_and_shifts_focus);
    RUN_TEST(test_enter_with_no_dialog_is_noop);
    RUN_TEST(test_esc_on_top_level_quits);
    RUN_TEST(test_esc_in_history_goes_back_not_quit);
    RUN_TEST(test_r_requests_reload);
    RUN_TEST(test_help_overlay_is_bordered_titled_and_keeps_the_view);
    RUN_TEST(test_help_closes_with_esc_q_backspace_and_does_not_quit);
    RUN_TEST(test_help_other_keys_change_nothing_and_ctrl_c_quits);
    RUN_TEST(test_history_help_names_its_view);
    RUN_TEST(test_header_bar_text_and_colours);
    RUN_TEST(test_header_clips_left_before_the_name);
    RUN_TEST(test_status_message_is_cleared_by_the_next_key);
    RUN_TEST(test_too_small_terminal_shows_a_notice);
    RUN_TEST(test_empty_dialog_list_names_the_key);
    RUN_TEST(test_paint_renders_all_three_panes);
    RUN_TEST(test_selected_dialog_row_is_filled_only_when_focused);
    RUN_TEST(test_unread_dialog_is_bold);
    RUN_TEST(test_paint_does_not_write_to_the_terminal);
    RUN_TEST(test_non_ascii_titles_in_c_locale);
    RUN_TEST(test_modified_keys_do_not_leak_characters);
    RUN_TEST(test_page_keys_and_ctrl_f_b);
    RUN_TEST(test_non_key_events_are_ignored);
}
