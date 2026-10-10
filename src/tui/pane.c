/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Pane geometry and layout implementation. */

/**
 * @file tui/pane.c
 * @brief Pane geometry and layout implementation (US-11 v2).
 */

#include "tui/pane.h"

#include <string.h>

int pane_is_valid(const tui_rect *r) {
    return r && !tui_rect_is_empty(*r);
}

static void zero_layout(Layout *out) { memset(out, 0, sizeof(*out)); }

void layout_compute(Layout *out, int screen_rows, int screen_cols,
                    int left_width_hint) {
    if (!out) return;
    if (screen_rows < TUI_MIN_SCREEN_ROWS || screen_cols < TUI_MIN_SCREEN_COLS) {
        zero_layout(out);
        return;
    }

    int left = left_width_hint;
    if (left < TUI_MIN_LEFT_WIDTH) left = TUI_MIN_LEFT_WIDTH;
    if (left > TUI_MAX_LEFT_WIDTH) left = TUI_MAX_LEFT_WIDTH;

    /* History pane always needs at least 20 cols to be useful — shrink the
     * left pane if the terminal is too narrow to accommodate both. */
    int min_right = 20;
    if (screen_cols - left < min_right) {
        left = screen_cols - min_right;
        if (left < TUI_MIN_LEFT_WIDTH) left = TUI_MIN_LEFT_WIDTH;
    }

    int top_rows = screen_rows - 1; /* reserve last row for status line */

    out->dialogs.row  = 0;
    out->dialogs.col  = 0;
    out->dialogs.rows = top_rows;
    out->dialogs.cols = left;

    out->history.row  = 0;
    out->history.col  = left;
    out->history.rows = top_rows;
    out->history.cols = screen_cols - left;

    out->status.row   = screen_rows - 1;
    out->status.col   = 0;
    out->status.rows  = 1;
    out->status.cols  = screen_cols;
}
