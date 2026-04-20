/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file tui/pane.c
 * @brief Pane geometry and layout implementation (US-11 v2).
 */

#include "tui/pane.h"

#include <string.h>

int pane_is_valid(const Pane *p) {
    return p && p->rows > 0 && p->cols > 0;
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

int pane_put_str(const Pane *p, Screen *s,
                  int row, int col, const char *utf8, uint8_t attrs) {
    if (!pane_is_valid(p) || !s || !utf8) return 0;
    if (row < 0 || row >= p->rows || col < 0 || col >= p->cols) return 0;
    int remaining = p->cols - col;
    return screen_put_str_n(s, p->row + row, p->col + col, remaining,
                            utf8, attrs);
}

void pane_fill(const Pane *p, Screen *s,
               int row, int col, int n, uint8_t attrs) {
    if (!pane_is_valid(p) || !s || n <= 0) return;
    if (row < 0 || row >= p->rows || col < 0 || col >= p->cols) return;
    if (col + n > p->cols) n = p->cols - col;
    screen_fill(s, p->row + row, p->col + col, n, attrs);
}

void pane_clear(const Pane *p, Screen *s) {
    if (!pane_is_valid(p) || !s) return;
    for (int r = 0; r < p->rows; r++) {
        screen_fill(s, p->row + r, p->col, p->cols, 0);
    }
}
