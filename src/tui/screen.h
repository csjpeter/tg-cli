/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#ifndef TUI_SCREEN_H
#define TUI_SCREEN_H

/**
 * @file tui/screen.h
 * @brief Double-buffered terminal screen for the curses-style TUI (US-11 v2).
 *
 * The screen owns two cell grids: @c back (what we want drawn) and @c front
 * (what the terminal currently shows). Writers stage changes into @c back via
 * screen_put_str() / screen_fill(); screen_flip() diffs back vs front and
 * emits only the ANSI bytes needed to bring the terminal in sync.
 *
 * The screen writes ANSI bytes through its @c out FILE* so tests can redirect
 * output to a memstream. Production code leaves @c out = stdout.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    SCREEN_ATTR_NORMAL  = 0,
    SCREEN_ATTR_BOLD    = 1u << 0,
    SCREEN_ATTR_DIM     = 1u << 1,
    SCREEN_ATTR_REVERSE = 1u << 2,
} ScreenAttr;

typedef struct {
    uint32_t cp;     /* Unicode codepoint (0 treated as blank).        */
    uint8_t  width;  /* 0 = trailer of wide cell, 1 = narrow, 2 = wide */
    uint8_t  attrs;  /* bitmask of ScreenAttr                          */
    uint16_t _pad;
} ScreenCell;

typedef struct {
    int         rows;
    int         cols;
    ScreenCell *front;
    ScreenCell *back;
    FILE       *out;        /* byte sink — defaults to stdout       */
    int         force_full; /* next flip must emit every cell        */
} Screen;

/** Allocate the front/back grids (rows x cols). Sets @c out = stdout.
 *  Returns 0 on success, -1 on allocation failure or bad arguments. */
int  screen_init(Screen *s, int rows, int cols);

/** Release both grids. Does not close @c out. Safe on a partially-inited
 *  Screen. Zeroes the struct on return. */
void screen_free(Screen *s);

/** Reset the back grid to blank cells with normal attributes. */
void screen_clear_back(Screen *s);

/** Fill @p n cells starting at (row, col) of the back grid with spaces and
 *  @p attrs. Clips at the right edge; no-op on negative coords. */
void screen_fill(Screen *s, int row, int col, int n, uint8_t attrs);

/** Write a UTF-8 string into the back grid starting at (row, col) with
 *  @p attrs. Clips at the right edge (partial wide glyphs are not emitted).
 *  Returns the number of columns consumed. Non-printable codepoints
 *  (wcwidth <= 0) are skipped. */
int  screen_put_str(Screen *s, int row, int col,
                    const char *utf8, uint8_t attrs);

/** Same as screen_put_str() but stops after at most @p max_cols columns of
 *  visible output. Useful for writing inside a sub-rect (pane) narrower than
 *  the screen. Pass max_cols <= 0 to disable the extra cap. */
int  screen_put_str_n(Screen *s, int row, int col, int max_cols,
                      const char *utf8, uint8_t attrs);

/** Force the next screen_flip() to emit every non-blank cell again, as if
 *  the terminal's state were unknown (e.g. after SIGWINCH or an external
 *  process wrote to the tty). */
void screen_invalidate(Screen *s);

/** Diff back vs front, write ANSI bytes + UTF-8 cell contents to @c out,
 *  and copy back into front. Returns the number of bytes written. */
size_t screen_flip(Screen *s);

/** Write an ANSI cursor-position (1-based) sequence to @c out and flush. */
void screen_cursor(Screen *s, int row, int col);

/** Show (1) or hide (0) the terminal cursor via DECTCEM. */
void screen_cursor_visible(Screen *s, int visible);

#endif /* TUI_SCREEN_H */
