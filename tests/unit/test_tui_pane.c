/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Unit tests for the TUI pane + layout geometry. */

/**
 * @file tests/unit/test_tui_pane.c
 * @brief Unit tests for the TUI pane + layout geometry (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/pane.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Layout tests --- */

static void test_layout_standard_80x24(void) {
    Layout L; layout_compute(&L, 24, 80, 30);
    ASSERT(pane_is_valid(&L.dialogs), "dialogs pane valid");
    ASSERT(pane_is_valid(&L.history), "history pane valid");
    ASSERT(pane_is_valid(&L.status),  "status pane valid");
    ASSERT(L.dialogs.row == 0 && L.dialogs.col == 0, "dialogs at (0,0)");
    ASSERT(L.dialogs.rows == 23, "dialogs rows = screen_rows - 1");
    ASSERT(L.dialogs.cols == 30, "dialogs cols = hint");
    ASSERT(L.history.row == 0 && L.history.col == 30, "history right of dialogs");
    ASSERT(L.history.cols == 50, "history fills remainder");
    ASSERT(L.status.row == 23 && L.status.col == 0, "status on last row");
    ASSERT(L.status.cols == 80 && L.status.rows == 1, "status is full width, 1 row");
}

static void test_layout_clamps_left_width_hint(void) {
    Layout L;
    layout_compute(&L, 24, 100, 5);           /* hint below minimum */
    ASSERT(L.dialogs.cols == TUI_MIN_LEFT_WIDTH, "hint clamped to min");
    layout_compute(&L, 24, 100, 200);         /* hint above maximum */
    ASSERT(L.dialogs.cols == TUI_MAX_LEFT_WIDTH, "hint clamped to max");
}

static void test_layout_shrinks_left_on_narrow_terminal(void) {
    /* 45 cols, hint 35 — history would be 10 cols, too narrow. Left shrinks
     * so history gets at least min_right (20), so left = 25 is expected. */
    Layout L; layout_compute(&L, 24, 45, 35);
    ASSERT(L.history.cols >= 20, "history keeps at least 20 cols");
    ASSERT(L.dialogs.cols + L.history.cols == 45, "left + right == cols");
}

static void test_layout_rejects_too_small_screen(void) {
    Layout L;
    layout_compute(&L, 2, 80, 30);
    ASSERT(!pane_is_valid(&L.dialogs), "too few rows rejected");
    layout_compute(&L, 24, 10, 5);
    ASSERT(!pane_is_valid(&L.dialogs), "too few cols rejected");
}

static void test_layout_null_out_is_noop(void) {
    layout_compute(NULL, 24, 80, 30); /* must not crash */
    ASSERT(1, "null layout handled");
}

void test_tui_pane_run(void) {
    RUN_TEST(test_layout_standard_80x24);
    RUN_TEST(test_layout_clamps_left_width_hint);
    RUN_TEST(test_layout_shrinks_left_on_narrow_terminal);
    RUN_TEST(test_layout_rejects_too_small_screen);
    RUN_TEST(test_layout_null_out_is_noop);
}
