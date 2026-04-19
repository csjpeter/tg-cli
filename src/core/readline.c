/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file readline.c
 * @brief Custom interactive line editor built on top of terminal.h.
 */

#include "readline.h"
#include "platform/terminal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- History ---- */

void rl_history_init(LineHistory *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
}

void rl_history_add(LineHistory *h, const char *line) {
    if (!h || !line || line[0] == '\0') return;

    /* Ignore duplicate of the most recent entry */
    if (h->count > 0) {
        int last = (h->head - 1 + RL_HISTORY_MAX) % RL_HISTORY_MAX;
        if (strcmp(h->entries[last], line) == 0) return;
    }

    strncpy(h->entries[h->head], line, RL_HISTORY_ENTRY_MAX - 1);
    h->entries[h->head][RL_HISTORY_ENTRY_MAX - 1] = '\0';
    h->head = (h->head + 1) % RL_HISTORY_MAX;
    if (h->count < RL_HISTORY_MAX) h->count++;
}

/* ---- Internal: get history entry by reverse index ----
 *
 * index 0 = most recent, 1 = second most recent, etc.
 * Returns NULL if out of range.
 */
static const char *history_get(const LineHistory *h, int index) {
    if (!h || index < 0 || index >= h->count) return NULL;
    int pos = (h->head - 1 - index + RL_HISTORY_MAX * 2) % RL_HISTORY_MAX;
    return h->entries[pos];
}

/* ---- Internal: line editor state ---- */

typedef struct {
    char   *buf;       /* output buffer (caller-supplied)          */
    size_t  size;      /* buf capacity in bytes                    */
    size_t  len;       /* current content length (excl. NUL)       */
    size_t  cur;       /* cursor position (0 .. len)               */
    const char *prompt;
    int     prompt_len;
} LineState;

/* ---- Internal: redraw the current line ---- */

static void redraw(const LineState *s) {
    /* Move cursor to beginning of line, clear to end, rewrite */
    fputs("\r", stdout);
    fputs(s->prompt, stdout);
    fwrite(s->buf, 1, s->len, stdout);
    /* Clear any leftover characters from a previous longer line */
    fputs("\033[K", stdout);
    /* Reposition cursor */
    size_t cursor_col = (size_t)s->prompt_len + s->cur;
    /* Move to column cursor_col (1-based): \r then CUF */
    if (cursor_col > 0) {
        printf("\r\033[%zuC", cursor_col);
    } else {
        fputs("\r", stdout);
    }
    fflush(stdout);
}

/* ---- Internal: insert a character at cursor ---- */

static void insert_char(LineState *s, char c) {
    if (s->len + 1 >= s->size) return; /* no space */
    /* Shift right */
    memmove(s->buf + s->cur + 1, s->buf + s->cur, s->len - s->cur);
    s->buf[s->cur] = c;
    s->len++;
    s->cur++;
    s->buf[s->len] = '\0';
}

/* ---- Internal: delete character before cursor (Backspace) ---- */

static void delete_before(LineState *s) {
    if (s->cur == 0) return;
    memmove(s->buf + s->cur - 1, s->buf + s->cur, s->len - s->cur);
    s->cur--;
    s->len--;
    s->buf[s->len] = '\0';
}

/* ---- Internal: delete character at cursor (Delete / Ctrl-D) ---- */

static void delete_at(LineState *s) {
    if (s->cur >= s->len) return;
    memmove(s->buf + s->cur, s->buf + s->cur + 1, s->len - s->cur - 1);
    s->len--;
    s->buf[s->len] = '\0';
}

/* ---- Internal: delete previous word (Ctrl-W) ---- */

static void delete_prev_word(LineState *s) {
    if (s->cur == 0) return;
    size_t end = s->cur;
    /* Skip trailing spaces */
    while (s->cur > 0 && s->buf[s->cur - 1] == ' ') s->cur--;
    /* Delete word characters */
    while (s->cur > 0 && s->buf[s->cur - 1] != ' ') s->cur--;
    size_t deleted = end - s->cur;
    memmove(s->buf + s->cur, s->buf + end, s->len - end);
    s->len -= deleted;
    s->buf[s->len] = '\0';
}

/* ---- Internal: kill to end of line (Ctrl-K) ---- */

static void kill_to_end(LineState *s) {
    s->len = s->cur;
    s->buf[s->len] = '\0';
}

/* ---- rl_readline: non-TTY fallback ----
 *
 * Uses read(STDIN_FILENO) directly to bypass FILE* buffering, which is
 * important when the caller has dup2'd a pipe or file into STDIN_FILENO.
 */
static int readline_nontty(const char *prompt, char *buf, size_t size) {
    if (prompt && *prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    size_t len = 0;
    while (len + 1 < size) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            if (len == 0) return -1; /* EOF with no data */
            break;
        }
        if (c == '\n') break;
        if (c == '\r') continue;    /* skip CR in CR+LF sequences */
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return (int)len;
}

/* ---- rl_readline: interactive (TTY) path ---- */

int rl_readline(const char *prompt, char *buf, size_t size,
                LineHistory *history) {
    if (!buf || size == 0) return -1;

    /* Non-TTY fallback */
    if (!terminal_is_tty(STDIN_FILENO)) {
        return readline_nontty(prompt, buf, size);
    }

    RAII_TERM_RAW TermRawState *raw = terminal_raw_enter();
    if (!raw) {
        /* Raw mode failed — fall back to plain read */
        return readline_nontty(prompt, buf, size);
    }

    LineState s;
    s.buf        = buf;
    s.size       = size;
    s.len        = 0;
    s.cur        = 0;
    s.prompt     = prompt ? prompt : "";
    s.prompt_len = prompt ? (int)strlen(prompt) : 0;
    buf[0]       = '\0';

    /* History navigation state */
    int hist_idx = -1; /* -1 = not navigating */
    char saved_line[RL_HISTORY_ENTRY_MAX]; /* saved current edit when navigating */
    saved_line[0] = '\0';

    /* Initial prompt */
    fputs(s.prompt, stdout);
    fflush(stdout);

    for (;;) {
        TermKey key = terminal_read_key();

        switch (key) {
        case TERM_KEY_ENTER:
            /* Submit */
            buf[s.len] = '\0';
            fputs("\r\n", stdout);
            fflush(stdout);
            return (int)s.len;

        case TERM_KEY_QUIT:
            /* Ctrl-C: discard and signal abort */
            fputs("\r\n", stdout);
            fflush(stdout);
            buf[0] = '\0';
            return -1;

        case TERM_KEY_CTRL_D:
            if (s.len == 0) {
                /* EOF on empty line */
                fputs("\r\n", stdout);
                fflush(stdout);
                return -1;
            }
            delete_at(&s);
            break;

        case TERM_KEY_BACK:
            delete_before(&s);
            break;

        case TERM_KEY_DELETE:
            delete_at(&s);
            break;

        case TERM_KEY_LEFT:
            if (s.cur > 0) s.cur--;
            break;

        case TERM_KEY_RIGHT:
            if (s.cur < s.len) s.cur++;
            break;

        case TERM_KEY_HOME:
        case TERM_KEY_CTRL_A:
            s.cur = 0;
            break;

        case TERM_KEY_END:
        case TERM_KEY_CTRL_E:
            s.cur = s.len;
            break;

        case TERM_KEY_CTRL_K:
            kill_to_end(&s);
            break;

        case TERM_KEY_CTRL_W:
            delete_prev_word(&s);
            break;

        case TERM_KEY_PREV_LINE: /* Up — older history */
            if (!history || history->count == 0) break;
            if (hist_idx == -1) {
                /* Save current edit */
                strncpy(saved_line, buf, RL_HISTORY_ENTRY_MAX - 1);
                saved_line[RL_HISTORY_ENTRY_MAX - 1] = '\0';
            }
            if (hist_idx + 1 < history->count) {
                hist_idx++;
                const char *entry = history_get(history, hist_idx);
                if (entry) {
                    strncpy(buf, entry, size - 1);
                    buf[size - 1] = '\0';
                    s.len = strlen(buf);
                    s.cur = s.len;
                }
            }
            break;

        case TERM_KEY_NEXT_LINE: /* Down — newer history / current edit */
            if (!history || hist_idx == -1) break;
            hist_idx--;
            if (hist_idx < 0) {
                /* Restore saved edit */
                hist_idx = -1;
                strncpy(buf, saved_line, size - 1);
                buf[size - 1] = '\0';
                s.len = strlen(buf);
                s.cur = s.len;
            } else {
                const char *entry = history_get(history, hist_idx);
                if (entry) {
                    strncpy(buf, entry, size - 1);
                    buf[size - 1] = '\0';
                    s.len = strlen(buf);
                    s.cur = s.len;
                }
            }
            break;

        case TERM_KEY_IGNORE: {
            int ch = terminal_last_printable();
            if (ch >= 32 && ch <= 126) {
                insert_char(&s, (char)ch);
            }
            break;
        }

        default:
            /* TERM_KEY_ESC, TERM_KEY_PREV_PAGE, TERM_KEY_NEXT_PAGE — ignore */
            break;
        }

        redraw(&s);
    }
}
