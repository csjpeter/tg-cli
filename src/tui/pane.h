/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Pane geometry + layout for the curses-style TUI. */

#ifndef TUI_PANE_H
#define TUI_PANE_H

/**
 * @file tui/pane.h
 * @brief Pane geometry + layout for the curses-style TUI (US-11 v2).
 *
 * A pane is a tui_rect of the screen (libtuikit); drawing goes through a
 * tui_canvas (tui_canvas_sub) that clips to it. The Layout computes the standard three-pane split for the v2 TUI:
 *
 *   +---------------+-------------------------------------+
 *   |  dialog list  |  message history of selected chat   |
 *   |               |                                     |
 *   +---------------+-------------------------------------+
 *   |  status / command buffer (single row)               |
 *   +-----------------------------------------------------+
 */

#include <tuikit/canvas.h>

/** Layout for the classic three-section TUI. Every rect is relative to the
 *  full screen origin (0,0) — no nested coordinate systems. */
typedef struct {
    tui_rect dialogs;  /* left pane                              */
    tui_rect history;  /* right pane                             */
    tui_rect status;   /* bottom single-row pane (full width)    */
} Layout;

/** Compute the default three-pane layout for the given screen size.
 *  @p left_width_hint is a desired width for the dialogs pane; the function
 *  clamps it to [MIN_LEFT_WIDTH, MAX_LEFT_WIDTH] and shrinks further if the
 *  terminal is too narrow. When the terminal is smaller than the absolute
 *  minimum (e.g. rows < 3 or cols < 20), every pane rect is zeroed so callers
 *  can detect "too small" with pane_is_valid(). */
void layout_compute(Layout *out, int screen_rows, int screen_cols,
                    int left_width_hint);

#define TUI_MIN_LEFT_WIDTH   20
#define TUI_MAX_LEFT_WIDTH   40
#define TUI_MIN_SCREEN_ROWS   3
#define TUI_MIN_SCREEN_COLS  40

/** Returns non-zero when @p r has positive dimensions (was computable). */
int pane_is_valid(const tui_rect *r);

#endif /* TUI_PANE_H */
