#ifndef TUI_PANE_H
#define TUI_PANE_H

/**
 * @file tui/pane.h
 * @brief Pane geometry + layout for the curses-style TUI (US-11 v2).
 *
 * A Pane is a viewport rectangle on the screen. It owns no drawing state —
 * it is a coordinate-translation utility that clips writes inside its rect.
 * The Layout computes the standard three-pane split for the v2 TUI:
 *
 *   +---------------+-------------------------------------+
 *   |  dialog list  |  message history of selected chat   |
 *   |               |                                     |
 *   +---------------+-------------------------------------+
 *   |  status / command buffer (single row)               |
 *   +-----------------------------------------------------+
 */

#include "tui/screen.h"

#include <stdint.h>

typedef struct {
    int row;   /* top-left row, 0-based                      */
    int col;   /* top-left column, 0-based                   */
    int rows;  /* number of rows (> 0 when valid, 0 otherwise) */
    int cols;  /* number of columns (> 0 when valid, 0 otherwise) */
} Pane;

/** Layout for the classic three-section TUI. Every rect is relative to the
 *  full screen origin (0,0) — no nested coordinate systems. */
typedef struct {
    Pane dialogs;  /* left pane                              */
    Pane history;  /* right pane                             */
    Pane status;   /* bottom single-row pane (full width)    */
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

/** Returns non-zero when @p p has positive dimensions (was computable). */
int pane_is_valid(const Pane *p);

/** Write @p utf8 into the screen at pane-relative (row, col) with @p attrs.
 *  Coordinates are clipped to the pane rect. Returns columns consumed. */
int  pane_put_str(const Pane *p, Screen *s,
                  int row, int col, const char *utf8, uint8_t attrs);

/** Fill @p n cells with spaces at pane-relative (row, col), using @p attrs.
 *  Clips to the pane rect. */
void pane_fill(const Pane *p, Screen *s,
               int row, int col, int n, uint8_t attrs);

/** Clear the entire pane (fill every cell with spaces, normal attrs). */
void pane_clear(const Pane *p, Screen *s);

#endif /* TUI_PANE_H */
