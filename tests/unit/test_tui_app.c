/**
 * @file tests/unit/test_tui_app.c
 * @brief Unit tests for the TUI state machine (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/app.h"

#include <string.h>

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
    list_view_end(&app.dialogs.lv);  /* dirty scroll */
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
    TuiEvent ev = tui_app_handle_key(&app, TERM_KEY_QUIT);
    ASSERT(ev == TUI_EVENT_QUIT, "ctrl-c returns QUIT");
    tui_app_free(&app);
}

static void test_q_char_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = tui_app_handle_char(&app, 'q');
    ASSERT(ev == TUI_EVENT_QUIT, "'q' returns QUIT");
    ev = tui_app_handle_char(&app, 'Q');
    ASSERT(ev == TUI_EVENT_QUIT, "'Q' returns QUIT");
    tui_app_free(&app);
}

static void test_left_right_switch_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    /* Start in dialogs; RIGHT should switch to history. */
    TuiEvent ev = tui_app_handle_key(&app, TERM_KEY_RIGHT);
    ASSERT(ev == TUI_EVENT_REDRAW, "right triggers redraw");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus moved to history");
    ASSERT(app.status.mode == STATUS_MODE_HISTORY, "status mode sync");
    /* RIGHT again is a no-op. */
    ev = tui_app_handle_key(&app, TERM_KEY_RIGHT);
    ASSERT(ev == TUI_EVENT_NONE, "right is no-op when already in history");
    ev = tui_app_handle_key(&app, TERM_KEY_LEFT);
    ASSERT(ev == TUI_EVENT_REDRAW, "left switches back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus back on dialogs");
    tui_app_free(&app);
}

static void test_vim_keys_switch_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = tui_app_handle_char(&app, 'l');
    ASSERT(ev == TUI_EVENT_REDRAW, "'l' switches to history");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus history");
    ev = tui_app_handle_char(&app, 'h');
    ASSERT(ev == TUI_EVENT_REDRAW, "'h' switches back");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus dialogs");
    tui_app_free(&app);
}

static void test_arrow_keys_navigate_focused_pane(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 5);
    int initial = app.dialogs.lv.selected;
    tui_app_handle_key(&app, TERM_KEY_NEXT_LINE);
    ASSERT(app.dialogs.lv.selected == initial + 1, "down moved dialog cursor");
    tui_app_handle_key(&app, TERM_KEY_PREV_LINE);
    ASSERT(app.dialogs.lv.selected == initial, "up reversed");
    tui_app_handle_key(&app, TERM_KEY_END);
    ASSERT(app.dialogs.lv.selected == 4, "end to last");
    tui_app_handle_key(&app, TERM_KEY_HOME);
    ASSERT(app.dialogs.lv.selected == 0, "home to first");
    tui_app_free(&app);
}

static void test_jk_chars_navigate(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    tui_app_handle_char(&app, 'j');
    ASSERT(app.dialogs.lv.selected == 1, "'j' is down");
    tui_app_handle_char(&app, 'k');
    ASSERT(app.dialogs.lv.selected == 0, "'k' is up");
    tui_app_free(&app);
}

static void test_navigation_targets_focused_pane_only(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 5);
    tui_app_handle_key(&app, TERM_KEY_RIGHT);  /* focus history */
    tui_app_handle_key(&app, TERM_KEY_NEXT_LINE);
    ASSERT(app.dialogs.lv.selected == 0, "dialogs cursor not moved");
    tui_app_free(&app);
}

static void test_enter_on_dialog_requests_load_and_shifts_focus(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    seed_dialogs(&app, 3);
    TuiEvent ev = tui_app_handle_key(&app, TERM_KEY_ENTER);
    ASSERT(ev == TUI_EVENT_OPEN_DIALOG, "enter requests open");
    ASSERT(app.focus == TUI_FOCUS_HISTORY, "focus jumped to history");
    ASSERT(app.status.mode == STATUS_MODE_HISTORY, "status follows");
    tui_app_free(&app);
}

static void test_enter_with_no_dialog_is_noop(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = tui_app_handle_key(&app, TERM_KEY_ENTER);
    ASSERT(ev == TUI_EVENT_NONE, "enter on empty list is no-op");
    ASSERT(app.focus == TUI_FOCUS_DIALOGS, "focus unchanged");
    tui_app_free(&app);
}

static void test_esc_also_quits(void) {
    TuiApp app; tui_app_init(&app, 24, 80);
    TuiEvent ev = tui_app_handle_key(&app, TERM_KEY_ESC);
    ASSERT(ev == TUI_EVENT_QUIT, "esc quits");
    tui_app_free(&app);
}

/* --- Paint --- */

static void test_paint_renders_all_three_panes(void) {
    TuiApp app; tui_app_init(&app, 10, 50);
    seed_dialogs(&app, 3);
    tui_app_paint(&app);
    /* Something should be drawn in each pane — check a known cell in each. */
    int dialog_row = app.layout.dialogs.row;
    ASSERT(app.screen.back[dialog_row * 50 + 0].cp == 'u',
           "dialog pane painted");
    /* History pane should show the "(select a dialog)" hint on peer_loaded=0. */
    int hist_mid = app.layout.history.row
                 + app.layout.history.rows / 2;
    int hit = 0;
    for (int c = 0; c < app.layout.history.cols; c++) {
        if (app.screen.back[hist_mid * 50 + app.layout.history.col + c].cp
            == '(') { hit = 1; break; }
    }
    ASSERT(hit, "history pane shows hint");
    /* Status row is reverse-video full width. */
    int status_row = app.layout.status.row;
    int rev = 1;
    for (int c = 0; c < 50 && rev; c++) {
        if (!(app.screen.back[status_row * 50 + c].attrs & SCREEN_ATTR_REVERSE))
            rev = 0;
    }
    ASSERT(rev, "status row fully reversed");
    tui_app_free(&app);
}

static void test_paint_does_not_flip_stdout(void) {
    /* If paint were to call screen_flip/fwrite, this test would produce
     * visible noise during the suite run. Tests print their own names, but
     * stray ANSI would show up. Sanity check: back buffer has content,
     * front buffer is still blank (flip hasn't happened). */
    TuiApp app; tui_app_init(&app, 10, 50);
    seed_dialogs(&app, 2);
    tui_app_paint(&app);
    int any_back = 0, any_front_nonblank = 0;
    for (int i = 0; i < 10 * 50; i++) {
        if (app.screen.back[i].cp && app.screen.back[i].cp != ' ') any_back = 1;
        if (app.screen.front[i].cp && app.screen.front[i].cp != ' ')
            any_front_nonblank = 1;
    }
    ASSERT(any_back, "paint filled back");
    ASSERT(!any_front_nonblank, "paint did not touch front (no flip)");
    tui_app_free(&app);
}

void test_tui_app_run(void) {
    RUN_TEST(test_init_succeeds_on_valid_size);
    RUN_TEST(test_init_rejects_too_small);
    RUN_TEST(test_resize_recomputes_layout);
    RUN_TEST(test_ctrl_c_quits);
    RUN_TEST(test_q_char_quits);
    RUN_TEST(test_left_right_switch_focus);
    RUN_TEST(test_vim_keys_switch_focus);
    RUN_TEST(test_arrow_keys_navigate_focused_pane);
    RUN_TEST(test_jk_chars_navigate);
    RUN_TEST(test_navigation_targets_focused_pane_only);
    RUN_TEST(test_enter_on_dialog_requests_load_and_shifts_focus);
    RUN_TEST(test_enter_with_no_dialog_is_noop);
    RUN_TEST(test_esc_also_quits);
    RUN_TEST(test_paint_renders_all_three_panes);
    RUN_TEST(test_paint_does_not_flip_stdout);
}
