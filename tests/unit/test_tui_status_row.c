/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Unit tests for the status row. */

/**
 * @file tests/unit/test_tui_status_row.c
 * @brief Unit tests for the status row (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/status_row.h"

#include "tui_grid.h"

#include <stdlib.h>
#include <string.h>

static void test_init_is_dialogs_mode(void) {
    StatusRow sr; status_row_init(&sr);
    ASSERT(sr.mode == STATUS_MODE_DIALOGS, "default mode is dialogs");
    ASSERT(sr.message[0] == '\0', "empty message");
}

static void test_set_message_truncates(void) {
    StatusRow sr; status_row_init(&sr);
    char big[STATUS_ROW_MSG_MAX + 50];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    status_row_set_message(&sr, big);
    ASSERT(strlen(sr.message) == STATUS_ROW_MSG_MAX - 1, "truncated");
    status_row_set_message(&sr, NULL);
    ASSERT(sr.message[0] == '\0', "null clears");
}

static void test_render_dialogs_mode_shows_hint(void) {
    tui_screen *s = tui_screen_new(1, 80); ASSERT(s != NULL, "init");
    StatusRow sr; status_row_init(&sr);
    tui_rect p = tui_rect_make(0, 0, 1, 80);
    status_row_render(&sr, &p, s);
    char buf[128]; tgrid_row(s, 0, buf, sizeof(buf));
    ASSERT(strstr(buf, "[dialogs]") != NULL, "dialogs prefix");
    ASSERT(strstr(buf, "Enter") != NULL, "enter hint");
    ASSERT(strstr(buf, "? help") != NULL, "help key hinted");
    ASSERT(strstr(buf, "r reload") != NULL, "reload key hinted");
    /* The hints are yellow on the terminal's own background: no fill. */
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    tui_grid_capture(g, s);
    ASSERT(g->fg[0][0] == TUI_YELLOW, "hint is yellow");
    ASSERT(g->fg[0][12] == TUI_YELLOW, "hint stays yellow");
    for (int c = 0; c < 80; c++) {
        ASSERT(g->bg[0][c] == TUI_COLOR_DEFAULT, "terminal background kept");
        ASSERT(!(g->attrs[0][c] & TUI_ATTR_REVERSE), "no reverse video");
    }
    free(g);
    tui_screen_free(s);
}

static void test_render_history_mode_shows_different_hint(void) {
    tui_screen *s = tui_screen_new(1, 80); ASSERT(s != NULL, "init");
    StatusRow sr; status_row_init(&sr);
    sr.mode = STATUS_MODE_HISTORY;
    tui_rect p = tui_rect_make(0, 0, 1, 80);
    status_row_render(&sr, &p, s);
    char buf[128]; tgrid_row(s, 0, buf, sizeof(buf));
    ASSERT(strstr(buf, "[history]") != NULL, "history prefix");
    ASSERT(strstr(buf, "Esc back") != NULL, "back hint");
    tui_screen_free(s);
}

static void render_with(StatusRow *sr, TuiGrid *g) {
    tui_screen *s = tui_screen_new(1, 80);
    tui_rect p = tui_rect_make(0, 0, 1, 80);
    status_row_render(sr, &p, s);
    tui_grid_capture(g, s);
    tui_screen_free(s);
}

static void test_message_replaces_hints_in_its_style(void) {
    TuiGrid *g = malloc(sizeof(*g)); ASSERT(g != NULL, "alloc");
    StatusRow sr; status_row_init(&sr);
    char line[128];

    status_row_set_message(&sr, "plain note");
    render_with(&sr, g);
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strncmp(line, "plain note", 10) == 0, "message at the left");
    ASSERT(strstr(line, "[dialogs]") == NULL, "message replaces the hints");
    ASSERT(g->fg[0][0] == TUI_COLOR_DEFAULT && g->attrs[0][0] == 0,
           "plain message in the default style");

    status_row_set_message_kind(&sr, "loading", STATUS_MSG_PROGRESS);
    render_with(&sr, g);
    ASSERT(g->fg[0][0] == TUI_YELLOW, "progress is yellow");
    ASSERT(g->attrs[0][0] & TUI_ATTR_ITALIC, "progress is italic");

    status_row_set_message_kind(&sr, "saved", STATUS_MSG_OK);
    render_with(&sr, g);
    ASSERT(g->fg[0][0] == TUI_GREEN && (g->attrs[0][0] & TUI_ATTR_BOLD),
           "ok is bold green");

    status_row_set_message_kind(&sr, "failed", STATUS_MSG_ERROR);
    render_with(&sr, g);
    ASSERT(g->fg[0][0] == TUI_RED && (g->attrs[0][0] & TUI_ATTR_BOLD),
           "error is bold red");

    status_row_set_message(&sr, NULL);
    render_with(&sr, g);
    tui_grid_row_text(g, 0, line, sizeof(line));
    ASSERT(strstr(line, "[dialogs]") != NULL, "hints come back");
    ASSERT(sr.kind == STATUS_MSG_PLAIN, "kind reset");
    free(g);
}

static void test_long_message_is_cut_at_the_row_end(void) {
    tui_screen *s = tui_screen_new(1, 20); ASSERT(s != NULL, "init");
    StatusRow sr; status_row_init(&sr);
    status_row_set_message_kind(&sr, "an error message much longer than the row",
                                STATUS_MSG_ERROR);
    tui_rect p = tui_rect_make(0, 0, 1, 20);
    status_row_render(&sr, &p, s);
    /* "an error message much longer than the row": column 19 is 'c' */
    ASSERT(tgrid_ch(s, 0, 19) == 'c', "cut at the last column, no wrap");
    tui_screen_free(s);
}

static void test_render_null_args_noop(void) {
    tui_screen *s = tui_screen_new(1, 20); ASSERT(s != NULL, "init");
    tui_rect p = tui_rect_make(0, 0, 1, 20);
    StatusRow sr; status_row_init(&sr);
    status_row_render(NULL, &p, s);
    status_row_render(&sr, NULL, s);
    status_row_render(&sr, &p, NULL);
    ASSERT(tgrid_ch(s, 0, 0) == ' ', "back untouched");
    tui_screen_free(s);
}

void test_tui_status_row_run(void) {
    RUN_TEST(test_init_is_dialogs_mode);
    RUN_TEST(test_set_message_truncates);
    RUN_TEST(test_render_dialogs_mode_shows_hint);
    RUN_TEST(test_render_history_mode_shows_different_hint);
    RUN_TEST(test_message_replaces_hints_in_its_style);
    RUN_TEST(test_long_message_is_cut_at_the_row_end);
    RUN_TEST(test_render_null_args_noop);
}
