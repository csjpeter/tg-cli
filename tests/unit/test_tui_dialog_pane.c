/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Unit tests for the dialog pane view-model. */

/**
 * @file tests/unit/test_tui_dialog_pane.c
 * @brief Unit tests for the dialog pane view-model (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/dialog_pane.h"

#include "tui_grid.h"

#include <stdlib.h>
#include <string.h>

/* --- Helpers --- */

static DialogEntry mk_entry(DialogPeerKind kind, int64_t id,
                             const char *title, int unread) {
    DialogEntry e = {0};
    e.kind = kind;
    e.peer_id = id;
    e.unread_count = unread;
    if (title) {
        strncpy(e.title, title, sizeof(e.title) - 1);
        e.title[sizeof(e.title) - 1] = '\0';
    }
    return e;
}

/* --- State tests --- */

static void test_init_is_empty(void) {
    DialogPane dp;
    dialog_pane_init(&dp);
    ASSERT(dp.count == 0, "count 0");
    ASSERT(dp.lv.selected == -1, "selected -1");
    ASSERT(dialog_pane_selected(&dp) == NULL, "no selection returns NULL");
}

static void test_set_entries_populates_and_resets_selection(void) {
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "Alice", 0),
        mk_entry(DIALOG_PEER_CHANNEL, 2, "News", 5),
        mk_entry(DIALOG_PEER_CHAT, 3, "Team", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    ASSERT(dp.count == 3, "3 entries");
    ASSERT(dp.lv.selected == 0, "selected first");
    const DialogEntry *sel = dialog_pane_selected(&dp);
    ASSERT(sel != NULL && sel->peer_id == 1, "selected is Alice");
}

static void test_set_entries_clamps_overflow(void) {
    DialogPane dp; dialog_pane_init(&dp);
    /* src must be at least DIALOG_PANE_MAX+1 so the clamp actually fires
     * without reading beyond the array bounds. */
    DialogEntry src[DIALOG_PANE_MAX + 10];
    memset(src, 0, sizeof(src));
    for (int i = 0; i < DIALOG_PANE_MAX + 10; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, i + 1, "x", 0);
    /* Ask to copy DIALOG_PANE_MAX+10 — should clamp to DIALOG_PANE_MAX. */
    dialog_pane_set_entries(&dp, src, DIALOG_PANE_MAX + 10);
    ASSERT(dp.count == DIALOG_PANE_MAX, "clamped to max");
    ASSERT(dp.entries[0].peer_id == 1, "first entry copied");
    /* Verify set_entries with 0 resets to empty. */
    dialog_pane_set_entries(&dp, NULL, 0);
    ASSERT(dp.count == 0 && dp.lv.selected == -1, "reset to empty");
}

static void test_selected_after_navigation(void) {
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[4];
    for (int i = 0; i < 4; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, 100 + i, "u", 0);
    dialog_pane_set_entries(&dp, src, 4);
    tui_listview_move_down(&dp.lv);
    tui_listview_move_down(&dp.lv);
    const DialogEntry *sel = dialog_pane_selected(&dp);
    ASSERT(sel != NULL && sel->peer_id == 102, "selection follows list_view");
}

/* --- Render tests --- */

static void test_render_empty_pane_shows_placeholder(void) {
    tui_screen *s = tui_screen_new(5, 30); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    tui_rect p = tui_rect_make(0, 0, 5, 30);
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, s, /*focused*/ 1);

    /* Row 2 (middle of 5 rows) should hold the placeholder text. */
    char buf[64]; tgrid_row(s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(no dialogs - r reloads)") != NULL, "placeholder rendered");
    tui_screen_free(s);
}

static void test_render_writes_kind_prefix_and_title(void) {
    tui_screen *s = tui_screen_new(5, 30); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "Alice", 0),
        mk_entry(DIALOG_PEER_CHANNEL, 2, "News channel", 5),
        mk_entry(DIALOG_PEER_CHAT, 3, "Team", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    tui_rect p = tui_rect_make(0, 0, 5, 30);
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, s, /*focused*/ 0);

    char row0[64]; tgrid_row(s, 0, row0, sizeof(row0));
    ASSERT(row0[0] == 'u', "row 0 begins with 'u' (user)");
    ASSERT(strstr(row0, "Alice") != NULL, "row 0 contains title");

    char row1[64]; tgrid_row(s, 1, row1, sizeof(row1));
    ASSERT(row1[0] == 'c', "row 1 begins with 'c' (channel)");
    ASSERT(strstr(row1, "[5]") != NULL, "row 1 shows unread badge");
    ASSERT(strstr(row1, "News channel") != NULL, "row 1 contains title");

    char row2[64]; tgrid_row(s, 2, row2, sizeof(row2));
    ASSERT(row2[0] == 't', "row 2 begins with 't' (chat)");
    ASSERT(strstr(row2, "Team") != NULL, "row 2 contains title");
    tui_screen_free(s);
}

static void test_render_highlights_selection_when_focused(void) {
    tui_screen *s = tui_screen_new(3, 25); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "A", 0),
        mk_entry(DIALOG_PEER_USER, 2, "B", 0),
        mk_entry(DIALOG_PEER_USER, 3, "C", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    tui_listview_move_down(&dp.lv); /* select "B" on row 1 */
    tui_rect p = tui_rect_make(0, 0, 3, 25);
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, s, /*focused*/ 1);

    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, s);
    /* The selected row is bold reverse video across the full pane width. */
    for (int c = 0; c < p.cols; c++) {
        ASSERT(g->fg[1][c] == TUI_COLOR_DEFAULT && g->bg[1][c] == TUI_COLOR_DEFAULT,
               "row 1 on the default colours");
        ASSERT(g->attrs[1][c] & TUI_ATTR_REVERSE, "row 1 reverse");
        ASSERT(g->attrs[1][c] & TUI_ATTR_BOLD, "row 1 bold");
    }
    /* The other rows keep the terminal's own colours. */
    ASSERT(!(g->attrs[0][0] & TUI_ATTR_REVERSE) && !(g->attrs[2][0] & TUI_ATTR_REVERSE),
           "other rows not reversed");
    free(g);
    tui_screen_free(s);
}

static void test_render_marks_selection_without_focus_by_attribute(void) {
    tui_screen *s = tui_screen_new(2, 20); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[2] = {
        mk_entry(DIALOG_PEER_USER, 1, "A", 0),
        mk_entry(DIALOG_PEER_USER, 2, "B", 0),
    };
    dialog_pane_set_entries(&dp, src, 2);
    tui_rect p = tui_rect_make(0, 0, 2, 20);
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, s, /*focused*/ 0);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, s);
    ASSERT(g->bg[0][0] == TUI_COLOR_DEFAULT, "no fill when unfocused");
    ASSERT(g->attrs[0][0] & TUI_ATTR_UNDERLINE, "selection underlined");
    ASSERT(!(g->attrs[1][0] & TUI_ATTR_UNDERLINE), "other row plain");
    free(g);
    tui_screen_free(s);
}

static void test_render_unreadable_list_is_not_shown_as_empty(void) {
    tui_screen *s = tui_screen_new(5, 40); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    dp.load_failed = 1;
    tui_rect p = tui_rect_make(0, 0, 5, 40);
    dialog_pane_render(&dp, &p, s, 1);
    char buf[64]; tgrid_row(s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "could not read dialogs") != NULL, "says it could not read");
    ASSERT(strstr(buf, "no dialogs") == NULL, "not shown as empty");
    char hint[64]; tgrid_row(s, 3, hint, sizeof(hint));
    ASSERT(strstr(hint, "r retries") != NULL, "retry hint on the next row");
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, s);
    int col = (int)(strstr(buf, "could") - buf);
    ASSERT(g->fg[2][col] == TUI_RED && (g->attrs[2][col] & TUI_ATTR_BOLD),
           "in the error style");
    free(g);
    tui_screen_free(s);
}

static void test_render_bolds_unread_rows(void) {
    tui_screen *s = tui_screen_new(2, 25); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[2] = {
        mk_entry(DIALOG_PEER_USER, 1, "ReadRow", 0),
        mk_entry(DIALOG_PEER_USER, 2, "HasUnread", 3),
    };
    dialog_pane_set_entries(&dp, src, 2);
    tui_rect p = tui_rect_make(0, 0, 2, 25);
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, s, /*focused*/ 0);
    ASSERT((tgrid_attrs(s, 0, 0) & TUI_ATTR_BOLD) == 0, "no unread → not bold");
    ASSERT(tgrid_attrs(s, 1, 0) & TUI_ATTR_BOLD, "unread row is bold");
    tui_screen_free(s);
}

static void test_render_respects_scroll_top(void) {
    tui_screen *s = tui_screen_new(3, 20); ASSERT(s != NULL, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[6];
    for (int i = 0; i < 6; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, i + 1,
                          (char[]){ (char)('A' + i), '\0' }, 0);
    dialog_pane_set_entries(&dp, src, 6);
    tui_rect p = tui_rect_make(0, 0, 3, 20);
    dp.lv.rows_visible = p.rows;
    /* Scroll down so rows 3,4,5 are visible. */
    tui_listview_end(&dp.lv);
    dialog_pane_render(&dp, &p, s, /*focused*/ 1);
    char row0[32]; tgrid_row(s, 0, row0, sizeof(row0));
    char row2[32]; tgrid_row(s, 2, row2, sizeof(row2));
    ASSERT(strstr(row0, "D") != NULL, "row 0 shows D (index 3)");
    ASSERT(strstr(row2, "F") != NULL, "row 2 shows F (index 5)");
    tui_screen_free(s);
}

static void test_render_null_args_are_noops(void) {
    tui_screen *s = tui_screen_new(2, 10); ASSERT(s != NULL, "init screen");
    tui_rect p = tui_rect_make(0, 0, 2, 10);
    DialogPane dp; dialog_pane_init(&dp);
    dialog_pane_render(NULL, &p, s, 0);
    dialog_pane_render(&dp, NULL, s, 0);
    dialog_pane_render(&dp, &p, NULL, 0);
    /* No crash; back grid untouched. */
    ASSERT(tgrid_ch(s, 0, 0) == ' ', "back untouched");
    tui_screen_free(s);
}

void test_tui_dialog_pane_run(void) {
    RUN_TEST(test_init_is_empty);
    RUN_TEST(test_set_entries_populates_and_resets_selection);
    RUN_TEST(test_set_entries_clamps_overflow);
    RUN_TEST(test_selected_after_navigation);
    RUN_TEST(test_render_empty_pane_shows_placeholder);
    RUN_TEST(test_render_writes_kind_prefix_and_title);
    RUN_TEST(test_render_highlights_selection_when_focused);
    RUN_TEST(test_render_marks_selection_without_focus_by_attribute);
    RUN_TEST(test_render_unreadable_list_is_not_shown_as_empty);
    RUN_TEST(test_render_bolds_unread_rows);
    RUN_TEST(test_render_respects_scroll_top);
    RUN_TEST(test_render_null_args_are_noops);
}
