/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Unit tests for the history pane view-model. */

/**
 * @file tests/unit/test_tui_history_pane.c
 * @brief Unit tests for the history pane view-model (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/history_pane.h"

#include "tui_grid.h"

#include <stdlib.h>
#include <string.h>

/* --- Helpers --- */

static HistoryEntry mk_text(int32_t id, int outgoing, const char *text) {
    HistoryEntry e = {0};
    e.id = id;
    e.out = outgoing;
    if (text) {
        strncpy(e.text, text, HISTORY_TEXT_MAX - 1);
        e.text[HISTORY_TEXT_MAX - 1] = '\0';
    }
    return e;
}

static HistoryEntry mk_complex(int32_t id) {
    HistoryEntry e = {0};
    e.id = id;
    e.complex = 1;
    return e;
}

static HistoryEntry mk_media(int32_t id, MediaKind kind) {
    HistoryEntry e = {0};
    e.id = id;
    e.media = kind;
    return e;
}

static HistoryPeer mk_peer_self(void) {
    HistoryPeer p = { .kind = HISTORY_PEER_SELF };
    return p;
}

/* --- State tests --- */

static void test_init_is_empty_and_unloaded(void) {
    HistoryPane hp;
    history_pane_init(&hp);
    ASSERT(hp.count == 0, "no entries");
    ASSERT(hp.peer_loaded == 0, "not yet loaded");
    ASSERT(hp.lv.selected == -1, "no selection");
}

static void test_set_entries_marks_loaded(void) {
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer p = mk_peer_self();
    HistoryEntry src[2] = { mk_text(10, 1, "hi"), mk_text(9, 0, "hey") };
    history_pane_set_entries(&hp, &p, src, 2);
    ASSERT(hp.peer_loaded == 1, "loaded");
    ASSERT(hp.count == 2, "2 entries");
    ASSERT(hp.peer.kind == HISTORY_PEER_SELF, "peer copied");
    ASSERT(hp.lv.selected == 0, "selection at top");
}

static void test_set_entries_clamps_overflow(void) {
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer p = mk_peer_self();
    /* src must be at least HISTORY_PANE_MAX+1 so the clamp fires without
     * reading beyond the array bounds. */
    HistoryEntry src[HISTORY_PANE_MAX + 1];
    memset(src, 0, sizeof(src));
    for (int i = 0; i < HISTORY_PANE_MAX + 1; i++)
        src[i] = mk_text(i + 1, 0, "x");
    history_pane_set_entries(&hp, &p, src, HISTORY_PANE_MAX + 1);
    ASSERT(hp.count == HISTORY_PANE_MAX, "clamped to max");
    /* Setting count=0 should keep peer_loaded=1 but clear the entries. */
    history_pane_set_entries(&hp, &p, NULL, 0);
    ASSERT(hp.count == 0, "reset to empty");
    ASSERT(hp.peer_loaded == 1, "peer still loaded");
    ASSERT(hp.lv.selected == -1, "no selection on empty");
}

/* --- Render tests --- */

static void test_render_unloaded_shows_select_hint(void) {
    tui_screen *s = tui_screen_new(5, 25); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    tui_rect p = tui_rect_make(0, 0, 5, 25);
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, s, 1);
    char buf[64]; tgrid_row(s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(select a dialog)") != NULL, "hint shown");
    tui_screen_free(s);
}

static void test_render_loaded_empty_shows_no_messages(void) {
    tui_screen *s = tui_screen_new(5, 25); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    history_pane_set_entries(&hp, &peer, NULL, 0);
    tui_rect p = tui_rect_make(0, 0, 5, 25);
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, s, 1);
    char buf[64]; tgrid_row(s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(no messages - r reloads)") != NULL, "empty placeholder");
    tui_screen_free(s);
}

static void test_render_rows_show_direction_and_text(void) {
    tui_screen *s = tui_screen_new(4, 30); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[3] = {
        mk_text(100, 1, "hello"),   /* outgoing */
        mk_text(99, 0, "hi there"), /* incoming */
        mk_complex(98),
    };
    history_pane_set_entries(&hp, &peer, src, 3);
    tui_rect p = tui_rect_make(0, 0, 4, 30);
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, s, 1);
    char row0[64]; tgrid_row(s, 0, row0, sizeof(row0));
    char row1[64]; tgrid_row(s, 1, row1, sizeof(row1));
    char row2[64]; tgrid_row(s, 2, row2, sizeof(row2));
    ASSERT(row0[0] == '>', "outgoing arrow");
    ASSERT(strstr(row0, "[100]") != NULL, "id badge");
    ASSERT(strstr(row0, "hello") != NULL, "text rendered");
    ASSERT(row1[0] == '<', "incoming arrow");
    ASSERT(strstr(row2, "(complex)") != NULL, "complex marker");
    tui_screen_free(s);
}

static void test_render_media_without_text_shows_media_marker(void) {
    tui_screen *s = tui_screen_new(2, 30); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[1] = { mk_media(50, MEDIA_PHOTO) };
    history_pane_set_entries(&hp, &peer, src, 1);
    tui_rect p = tui_rect_make(0, 0, 2, 30);
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, s, 1);
    char row[64]; tgrid_row(s, 0, row, sizeof(row));
    ASSERT(strstr(row, "(media)") != NULL, "media marker");
    tui_screen_free(s);
}

static void test_render_respects_scroll(void) {
    tui_screen *s = tui_screen_new(3, 30); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[6];
    for (int i = 0; i < 6; i++) {
        char text[8]; snprintf(text, sizeof(text), "msg%d", i);
        src[i] = mk_text(1000 + i, 0, text);
    }
    history_pane_set_entries(&hp, &peer, src, 6);
    tui_rect p = tui_rect_make(0, 0, 3, 30);
    hp.lv.rows_visible = p.rows;
    tui_listview_end(&hp.lv);  /* scroll to bottom */
    history_pane_render(&hp, &p, s, 1);
    char row0[64]; tgrid_row(s, 0, row0, sizeof(row0));
    char row2[64]; tgrid_row(s, 2, row2, sizeof(row2));
    ASSERT(strstr(row0, "msg3") != NULL, "row 0 shows msg3 after scroll");
    ASSERT(strstr(row2, "msg5") != NULL, "row 2 shows last message");
    tui_screen_free(s);
}

static void test_render_complex_row_is_dimmed(void) {
    tui_screen *s = tui_screen_new(3, 20); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[2] = { mk_text(43, 0, "plain"), mk_complex(42) };
    history_pane_set_entries(&hp, &peer, src, 2);
    tui_rect p = tui_rect_make(0, 0, 3, 20);
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, s, 1);
    /* Row 0 is the selected one; the complex row below it keeps its dim. */
    ASSERT(tgrid_attrs(s, 1, 0) & TUI_ATTR_DIM, "complex row is dimmed");
    ASSERT(!(tgrid_attrs(s, 0, 0) & TUI_ATTR_DIM), "plain row is not dimmed");
    tui_screen_free(s);
}

static void test_render_selection_style_follows_focus(void) {
    tui_screen *s = tui_screen_new(3, 20); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[2] = { mk_text(2, 0, "one"), mk_text(1, 0, "two") };
    history_pane_set_entries(&hp, &peer, src, 2);
    tui_rect p = tui_rect_make(0, 0, 3, 20);
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");

    history_pane_render(&hp, &p, s, 1);
    tui_grid_capture(g, s);
    ASSERT(g->fg[0][0] == TUI_BLACK && g->bg[0][19] == TUI_CYAN
           && (g->attrs[0][0] & TUI_ATTR_BOLD), "selected row bold black on cyan");
    ASSERT(g->bg[1][0] == TUI_COLOR_DEFAULT, "other row not filled");

    history_pane_render(&hp, &p, s, 0);
    tui_grid_capture(g, s);
    ASSERT(g->bg[0][0] == TUI_COLOR_DEFAULT, "no fill when unfocused");
    ASSERT(g->attrs[0][0] & TUI_ATTR_UNDERLINE, "underlined when unfocused");
    free(g);
    tui_screen_free(s);
}

static void test_render_unreadable_history_says_so(void) {
    tui_screen *s = tui_screen_new(5, 40); ASSERT(s != NULL, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    history_pane_set_entries(&hp, &peer, NULL, 0);
    hp.load_failed = 1;
    tui_rect p = tui_rect_make(0, 0, 5, 40);
    history_pane_render(&hp, &p, s, 1);
    char buf[64]; tgrid_row(s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "could not read messages") != NULL, "says it could not read");
    ASSERT(strstr(buf, "no messages") == NULL, "not shown as empty");
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, s);
    int col = (int)(strstr(buf, "could") - buf);
    ASSERT(g->fg[2][col] == TUI_RED, "in the error colour");
    free(g);
    tui_screen_free(s);
}

static void test_render_null_args_noop(void) {
    tui_screen *s = tui_screen_new(2, 10); ASSERT(s != NULL, "init screen");
    tui_rect p = tui_rect_make(0, 0, 2, 10);
    HistoryPane hp; history_pane_init(&hp);
    history_pane_render(NULL, &p, s, 1);
    history_pane_render(&hp, NULL, s, 1);
    history_pane_render(&hp, &p, NULL, 1);
    ASSERT(tgrid_ch(s, 0, 0) == ' ', "back untouched");
    tui_screen_free(s);
}

void test_tui_history_pane_run(void) {
    RUN_TEST(test_init_is_empty_and_unloaded);
    RUN_TEST(test_set_entries_marks_loaded);
    RUN_TEST(test_set_entries_clamps_overflow);
    RUN_TEST(test_render_unloaded_shows_select_hint);
    RUN_TEST(test_render_loaded_empty_shows_no_messages);
    RUN_TEST(test_render_rows_show_direction_and_text);
    RUN_TEST(test_render_media_without_text_shows_media_marker);
    RUN_TEST(test_render_respects_scroll);
    RUN_TEST(test_render_complex_row_is_dimmed);
    RUN_TEST(test_render_selection_style_follows_focus);
    RUN_TEST(test_render_unreadable_history_says_so);
    RUN_TEST(test_render_null_args_noop);
}
