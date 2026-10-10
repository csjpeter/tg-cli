/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Test helper: read a libtuikit screen back as a grid of cells. */

#ifndef TESTS_COMMON_TUI_GRID_H
#define TESTS_COMMON_TUI_GRID_H

/**
 * @file tests/common/tui_grid.h
 * @brief Reads a tui_screen back as text, attributes and colours.
 *
 * A tui_screen is opaque.  The way to see what it shows is what a terminal
 * sees: tui_screen_render() of an invalidated screen.  tui_grid_capture()
 * does that and interprets the bytes (cursor home, CUP, SGR, erase, UTF-8
 * text) into a grid, so tests can assert on the text of a row, on the
 * attributes of a cell and on its colours.
 */

#include <tuikit/buf.h>
#include <tuikit/screen.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TGRID_MAX_ROWS 64
#define TGRID_MAX_COLS 200

typedef struct {
    int      rows, cols;
    char     cell[TGRID_MAX_ROWS][TGRID_MAX_COLS][16]; /* "" blank or wide trailer; a base + its zero-width marks */
    uint16_t attrs[TGRID_MAX_ROWS][TGRID_MAX_COLS];   /* TUI_ATTR_* */
    uint32_t fg[TGRID_MAX_ROWS][TGRID_MAX_COLS];      /* tui_color */
    uint32_t bg[TGRID_MAX_ROWS][TGRID_MAX_COLS];
    int      cursor_visible;
} TuiGrid;

static inline void tgrid_sgr(const char *p, size_t n, uint16_t *attrs,
                             uint32_t *fg, uint32_t *bg) {
    int v[16], k = 0, cur = -1;
    for (size_t i = 0; i <= n && k < 16; i++) {
        if (i == n || p[i] == ';') {
            v[k++] = cur < 0 ? 0 : cur;
            cur = -1;
        } else if (p[i] >= '0' && p[i] <= '9') {
            cur = (cur < 0 ? 0 : cur * 10) + (p[i] - '0');
        }
    }
    for (int i = 0; i < k; i++) {
        int c = v[i];
        if (c == 0) { *attrs = 0; *fg = TUI_COLOR_DEFAULT; *bg = TUI_COLOR_DEFAULT; }
        else if (c == 1) *attrs |= TUI_ATTR_BOLD;
        else if (c == 2) *attrs |= TUI_ATTR_DIM;
        else if (c == 3) *attrs |= TUI_ATTR_ITALIC;
        else if (c == 4) *attrs |= TUI_ATTR_UNDERLINE;
        else if (c == 7) *attrs |= TUI_ATTR_REVERSE;
        else if (c >= 30 && c <= 37) *fg = TUI_COLOR_16(c - 30);
        else if (c >= 90 && c <= 97) *fg = TUI_COLOR_16(c - 90 + 8);
        else if (c >= 40 && c <= 47) *bg = TUI_COLOR_16(c - 40);
        else if (c >= 100 && c <= 107) *bg = TUI_COLOR_16(c - 100 + 8);
        else if (c == 39) *fg = TUI_COLOR_DEFAULT;
        else if (c == 49) *bg = TUI_COLOR_DEFAULT;
        else if ((c == 38 || c == 48) && i + 2 < k && v[i + 1] == 5) {
            uint32_t col = TUI_COLOR_256(v[i + 2]);
            if (c == 38) *fg = col; else *bg = col;
            i += 2;
        } else if ((c == 38 || c == 48) && i + 4 < k && v[i + 1] == 2) {
            uint32_t col = TUI_COLOR_RGB(v[i + 2], v[i + 3], v[i + 4]);
            if (c == 38) *fg = col; else *bg = col;
            i += 4;
        }
    }
}

static inline int tgrid_is_wide(const char *u) {
    const unsigned char *d = (const unsigned char *)u;
    unsigned cp = 0;
    if (d[0] >= 0xf0) cp = ((d[0] & 0x07) << 18) | ((d[1] & 0x3f) << 12)
                         | ((d[2] & 0x3f) << 6) | (d[3] & 0x3f);
    else if (d[0] >= 0xe0) cp = ((d[0] & 0x0f) << 12) | ((d[1] & 0x3f) << 6)
                              | (d[2] & 0x3f);
    else return 0;
    return (cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf)
        || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xff00 && cp <= 0xff60)
        || (cp >= 0x1f300 && cp <= 0x1faff);
}

/** Render @p s completely and read the result into @p g (cleared first). */
static inline void tui_grid_capture(TuiGrid *g, tui_screen *s) {
    memset(g, 0, sizeof(*g));
    g->rows = tui_screen_rows(s);
    g->cols = tui_screen_cols(s);
    if (g->rows > TGRID_MAX_ROWS) g->rows = TGRID_MAX_ROWS;
    if (g->cols > TGRID_MAX_COLS) g->cols = TGRID_MAX_COLS;
    g->cursor_visible = 1;

    tui_buf b;
    tui_buf_init(&b);
    tui_screen_invalidate(s);
    tui_screen_render(s, &b);

    int row = 0, col = 0;
    uint16_t attrs = 0;
    uint32_t fg = TUI_COLOR_DEFAULT, bg = TUI_COLOR_DEFAULT;
    const unsigned char *d = (const unsigned char *)b.data;
    size_t n = b.len;
    for (size_t i = 0; i < n;) {
        if (d[i] == 0x1b && i + 1 < n && d[i + 1] == '[') {
            size_t j = i + 2;
            while (j < n && !(d[j] >= 0x40 && d[j] <= 0x7e)) j++;
            if (j >= n) break;
            const char *par = (const char *)d + i + 2;
            size_t plen = j - (i + 2);
            char fin = (char)d[j];
            if (plen && par[0] == '?') {
                if (plen == 3 && memcmp(par, "?25", 3) == 0)
                    g->cursor_visible = (fin == 'h');
            } else if (fin == 'm') {
                tgrid_sgr(par, plen, &attrs, &fg, &bg);
            } else if (fin == 'H') {
                int r = 1, c = 1;
                if (plen) {
                    char tmp[32];
                    size_t m = plen < 31 ? plen : 31;
                    memcpy(tmp, par, m);
                    tmp[m] = '\0';
                    if (sscanf(tmp, "%d;%d", &r, &c) < 2) c = 1;
                }
                row = r - 1;
                col = c - 1;
            } else if (fin == 'C') {
                int c = plen ? atoi(par) : 1;
                col += c > 0 ? c : 1;
            } else if (fin == 'J') {
                /* erase the display: every cell is already blank */
            }
            i = j + 1;
            continue;
        }
        if (d[i] == '\r') { col = 0; i++; continue; }
        if (d[i] == '\n') { row++; i++; continue; }
        if (d[i] < 0x20) { i++; continue; }
        /* one UTF-8 character */
        size_t len = d[i] < 0x80 ? 1 : d[i] >= 0xf0 ? 4 : d[i] >= 0xe0 ? 3 : 2;
        if (i + len > n) break;
        /* display width: 0 for combining / zero-width / format marks,
         * 2 for the East Asian wide and emoji blocks, else 1 */
        unsigned cp = 0;
        if (len == 1) cp = d[i];
        else if (len == 2) cp = ((d[i] & 0x1f) << 6) | (d[i + 1] & 0x3f);
        else if (len == 3) cp = ((d[i] & 0x0f) << 12) | ((d[i + 1] & 0x3f) << 6)
                              | (d[i + 2] & 0x3f);
        else cp = ((d[i] & 0x07) << 18) | ((d[i + 1] & 0x3f) << 12)
                | ((d[i + 2] & 0x3f) << 6) | (d[i + 3] & 0x3f);
        int w = 1;
        if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x200b && cp <= 0x200f)
            || (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2060 && cp <= 0x2064)
            || cp == 0xfeff || cp == 0x061c || (cp >= 0xfe00 && cp <= 0xfe0f))
            w = 0;
        else if ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf)
                 || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xff00 && cp <= 0xff60)
                 || (cp >= 0x1f300 && cp <= 0x1faff))
            w = 2;
        if (w == 0) {
            /* attach to the previous cell */
            int pc = col - 1;
            while (pc > 0 && g->cell[row][pc][0] == '\0') pc--;
            if (row >= 0 && row < g->rows && pc >= 0 && pc < g->cols) {
                size_t have = strlen(g->cell[row][pc]);
                if (have + len < sizeof(g->cell[row][pc]))
                    memcpy(g->cell[row][pc] + have, d + i, len + 1 > 0 ? len : 0),
                    g->cell[row][pc][have + len] = '\0';
            }
            i += len;
            continue;
        }
        if (row >= 0 && row < g->rows && col >= 0 && col < g->cols) {
            memcpy(g->cell[row][col], d + i, len);
            g->cell[row][col][len] = '\0';
            for (int k = 0; k < w && col + k < g->cols; k++) {
                g->attrs[row][col + k] = attrs;
                g->fg[row][col + k] = fg;
                g->bg[row][col + k] = bg;
            }
        }
        col += w;
        i += len;
    }
    tui_buf_free(&b);
}

/** The text of row @p r, blanks as spaces, trailing blanks trimmed. */
static inline void tui_grid_row_text(const TuiGrid *g, int r,
                                     char *out, size_t cap) {
    size_t o = 0;
    if (cap == 0) return;
    int last = -1;
    for (int c = 0; c < g->cols; c++)
        if (g->cell[r][c][0]) last = c;
    for (int c = 0; c <= last; c++) {
        const char *s = g->cell[r][c];
        size_t l = s[0] ? strlen(s) : 0;
        if (!l) {
            /* blank, or the trailer of a wide character */
            if (c > 0 && g->cell[r][c - 1][0]
                && (unsigned char)g->cell[r][c - 1][0] >= 0xe0
                && g->cell[r][c - 1][1] != '\0' && g->cell[r][c + 0][0] == '\0'
                && tgrid_is_wide(g->cell[r][c - 1])) continue;
            if (o + 1 < cap) out[o++] = ' ';
            continue;
        }
        if (o + l >= cap) break;
        memcpy(out + o, s, l);
        o += l;
    }
    out[o] = '\0';
}

/** 1 if some row of the grid contains @p needle. */
static inline int tui_grid_contains(const TuiGrid *g, const char *needle) {
    char line[TGRID_MAX_COLS * 4 + 1];
    for (int r = 0; r < g->rows; r++) {
        tui_grid_row_text(g, r, line, sizeof(line));
        if (strstr(line, needle)) return 1;
    }
    return 0;
}

/** Attributes (TUI_ATTR_*) of the cell at (row, col) of @p s. */
static inline unsigned tgrid_attrs(tui_screen *s, int row, int col) {
    static TuiGrid g;
    tui_grid_capture(&g, s);
    return g.attrs[row][col];
}

/** The first byte of the character at (row, col): ' ' for a blank cell. */
static inline char tgrid_ch(tui_screen *s, int row, int col) {
    static TuiGrid g;
    tui_grid_capture(&g, s);
    return g.cell[row][col][0] ? g.cell[row][col][0] : ' ';
}

/** Text of row @p row of @p s (blanks as spaces, trailing blanks trimmed). */
static inline void tgrid_row(tui_screen *s, int row, char *out, size_t cap) {
    static TuiGrid g;
    tui_grid_capture(&g, s);
    tui_grid_row_text(&g, row, out, cap);
}

#endif /* TESTS_COMMON_TUI_GRID_H */
