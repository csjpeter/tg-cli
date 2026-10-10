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
    /* Every cell on the row must have TUI_ATTR_REVERSE set. */
    for (int c = 0; c < 80; c++) {
        ASSERT(tgrid_attrs(s, 0, c) & TUI_ATTR_REVERSE, "full-row reverse");
    }
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
    ASSERT(strstr(buf, "Tab") != NULL, "tab hint");
    tui_screen_free(s);
}

static void test_render_message_right_aligned(void) {
    tui_screen *s = tui_screen_new(1, 80); ASSERT(s != NULL, "init");
    StatusRow sr; status_row_init(&sr);
    status_row_set_message(&sr, "loading");
    tui_rect p = tui_rect_make(0, 0, 1, 80);
    status_row_render(&sr, &p, s);
    /* "loading" is 7 chars; col = 80 - 7 - 1 = 72 */
    ASSERT(tgrid_ch(s, 0, 72) == 'l', "message starts at right-aligned col");
    ASSERT(tgrid_ch(s, 0, 78) == 'g', "message ends near right edge");
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
    RUN_TEST(test_render_message_right_aligned);
    RUN_TEST(test_render_null_args_noop);
}
