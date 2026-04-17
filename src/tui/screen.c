/**
 * @file tui/screen.c
 * @brief Double-buffered terminal screen implementation (US-11 v2).
 */

#include "tui/screen.h"
#include "platform/terminal.h"

#include <stdlib.h>
#include <string.h>

static ScreenCell blank_cell(uint8_t attrs) {
    ScreenCell c = { .cp = ' ', .width = 1, .attrs = attrs, ._pad = 0 };
    return c;
}

int screen_init(Screen *s, int rows, int cols) {
    if (!s || rows <= 0 || cols <= 0) return -1;
    memset(s, 0, sizeof(*s));
    size_t n = (size_t)rows * (size_t)cols;
    s->rows = rows;
    s->cols = cols;
    s->front = (ScreenCell *)calloc(n, sizeof(ScreenCell));
    s->back  = (ScreenCell *)calloc(n, sizeof(ScreenCell));
    if (!s->front || !s->back) {
        free(s->front); free(s->back);
        memset(s, 0, sizeof(*s));
        return -1;
    }
    /* front is "unknown" (cp=0, width=1) so the first flip emits everything.
     * back starts as a blank canvas so writers can just paint over it. */
    for (size_t i = 0; i < n; i++) s->back[i] = blank_cell(0);
    s->out = stdout;
    s->force_full = 1;
    return 0;
}

void screen_free(Screen *s) {
    if (!s) return;
    free(s->front); free(s->back);
    memset(s, 0, sizeof(*s));
}

static ScreenCell *back_at(Screen *s, int r, int c) {
    return &s->back[(size_t)r * (size_t)s->cols + (size_t)c];
}

void screen_clear_back(Screen *s) {
    if (!s) return;
    size_t n = (size_t)s->rows * (size_t)s->cols;
    for (size_t i = 0; i < n; i++) s->back[i] = blank_cell(0);
}

void screen_fill(Screen *s, int row, int col, int n, uint8_t attrs) {
    if (!s || row < 0 || row >= s->rows || col < 0 || col >= s->cols || n <= 0)
        return;
    if (col + n > s->cols) n = s->cols - col;
    for (int i = 0; i < n; i++) *back_at(s, row, col + i) = blank_cell(attrs);
}

/* Decode one UTF-8 codepoint from @p p. Returns bytes consumed, or 0 at end
 * of string. Writes U+FFFD on malformed input and consumes a single byte so
 * the caller always makes progress. */
static int utf8_decode(const char *p, uint32_t *out_cp) {
    unsigned char c = (unsigned char)p[0];
    if (c == 0) { *out_cp = 0; return 0; }
    if (c < 0x80) { *out_cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) {
        unsigned char c1 = (unsigned char)p[1];
        if ((c1 & 0xC0) != 0x80) { *out_cp = 0xFFFD; return 1; }
        *out_cp = (uint32_t)((c & 0x1F) << 6) | (c1 & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        unsigned char c1 = (unsigned char)p[1], c2 = (unsigned char)p[2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            *out_cp = 0xFFFD; return 1;
        }
        *out_cp = (uint32_t)((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        unsigned char c1 = (unsigned char)p[1], c2 = (unsigned char)p[2],
                      c3 = (unsigned char)p[3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            *out_cp = 0xFFFD; return 1;
        }
        *out_cp = (uint32_t)((c & 0x07) << 18) | ((c1 & 0x3F) << 12)
                | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        return 4;
    }
    *out_cp = 0xFFFD;
    return 1;
}

int screen_put_str_n(Screen *s, int row, int col, int max_cols,
                      const char *utf8, uint8_t attrs) {
    if (!s || !utf8 || row < 0 || row >= s->rows
        || col < 0 || col >= s->cols) return 0;
    int start = col;
    int hard_stop = s->cols;
    if (max_cols > 0 && col + max_cols < hard_stop) hard_stop = col + max_cols;
    while (*utf8 && col < hard_stop) {
        uint32_t cp;
        int n = utf8_decode(utf8, &cp);
        if (n <= 0) break;
        utf8 += n;
        int w = terminal_wcwidth(cp);
        if (w <= 0) continue;
        if (col + w > hard_stop) break;
        ScreenCell *lead = back_at(s, row, col);
        lead->cp = cp; lead->width = (uint8_t)w; lead->attrs = attrs; lead->_pad = 0;
        if (w == 2 && col + 1 < s->cols) {
            ScreenCell *tr = back_at(s, row, col + 1);
            tr->cp = cp; tr->width = 0; tr->attrs = attrs; tr->_pad = 0;
        }
        col += w;
    }
    return col - start;
}

int screen_put_str(Screen *s, int row, int col,
                    const char *utf8, uint8_t attrs) {
    return screen_put_str_n(s, row, col, 0, utf8, attrs);
}

void screen_invalidate(Screen *s) {
    if (s) s->force_full = 1;
}

/* Encode one codepoint to UTF-8. Returns bytes written (1..4). */
static size_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Emit CSI m sequence for @p attrs. Always begins with a reset so the
 * previous cell's attributes do not leak through. */
static size_t sgr_encode(uint8_t attrs, char buf[16]) {
    size_t i = 0;
    buf[i++] = '\033'; buf[i++] = '['; buf[i++] = '0';
    if (attrs & SCREEN_ATTR_BOLD)    { buf[i++] = ';'; buf[i++] = '1'; }
    if (attrs & SCREEN_ATTR_DIM)     { buf[i++] = ';'; buf[i++] = '2'; }
    if (attrs & SCREEN_ATTR_REVERSE) { buf[i++] = ';'; buf[i++] = '7'; }
    buf[i++] = 'm';
    return i;
}

static size_t cup_encode(int row, int col, char buf[16]) {
    int n = snprintf(buf, 16, "\033[%d;%dH", row, col);
    return (n < 0) ? 0 : (size_t)n;
}

size_t screen_flip(Screen *s) {
    if (!s || !s->out) return 0;
    size_t total = 0;
    uint8_t cur_attrs = 0;
    int attrs_known = 0;
    int cur_row = -1, cur_col = -1;

    for (int r = 0; r < s->rows; r++) {
        for (int c = 0; c < s->cols; c++) {
            size_t idx = (size_t)r * (size_t)s->cols + (size_t)c;
            ScreenCell *b = &s->back[idx];
            ScreenCell *f = &s->front[idx];
            if (b->width == 0) continue; /* trailer — handled by its lead */
            int changed = s->force_full
                || b->cp != f->cp
                || b->width != f->width
                || b->attrs != f->attrs;
            if (!changed) continue;

            if (r != cur_row || c != cur_col) {
                char buf[16];
                size_t n = cup_encode(r + 1, c + 1, buf);
                fwrite(buf, 1, n, s->out); total += n;
                cur_row = r; cur_col = c;
            }
            if (!attrs_known || b->attrs != cur_attrs) {
                char buf[16];
                size_t n = sgr_encode(b->attrs, buf);
                fwrite(buf, 1, n, s->out); total += n;
                cur_attrs = b->attrs; attrs_known = 1;
            }
            char buf[4];
            size_t n = utf8_encode(b->cp ? b->cp : ' ', buf);
            fwrite(buf, 1, n, s->out); total += n;
            cur_col += b->width;

            *f = *b;
            if (b->width == 2 && c + 1 < s->cols) {
                s->front[idx + 1] = s->back[idx + 1];
            }
        }
    }
    if (attrs_known && cur_attrs != 0) {
        const char *reset = "\033[0m";
        fwrite(reset, 1, 4, s->out); total += 4;
    }
    s->force_full = 0;
    fflush(s->out);
    return total;
}

void screen_cursor(Screen *s, int row, int col) {
    if (!s || !s->out) return;
    char buf[16];
    size_t n = cup_encode(row, col, buf);
    fwrite(buf, 1, n, s->out);
    fflush(s->out);
}

void screen_cursor_visible(Screen *s, int visible) {
    if (!s || !s->out) return;
    const char *seq = visible ? "\033[?25h" : "\033[?25l";
    fwrite(seq, 1, strlen(seq), s->out);
    fflush(s->out);
}
