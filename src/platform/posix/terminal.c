/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */

/**
 * POSIX terminal implementation.
 * Uses termios(3), ioctl TIOCGWINSZ and the core wcwidth table.
 */
#include "../terminal.h"
#include "core/wcwidth.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <wchar.h>

/** Opaque saved terminal state. Definition lives here (hidden from header). */
struct TermRawState {
    struct termios saved;
    int            active;
};

int terminal_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;
}

int terminal_rows(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
    return 0;
}

int terminal_is_tty(int fd) {
    return isatty(fd);
}

TermRawState *terminal_raw_enter(void) {
    TermRawState *state = malloc(sizeof(TermRawState));
    if (!state) return NULL;

    if (tcgetattr(STDIN_FILENO, &state->saved) != 0) {
        free(state);
        return NULL;
    }

    struct termios raw = state->saved;
    raw.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG);
    /* Flow control off: otherwise the line discipline swallows Ctrl-S and
     * Ctrl-Q and the screen appears to hang (TG-4). */
    raw.c_iflag &= ~(unsigned)(IXON | IXOFF);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        free(state);
        return NULL;
    }

    /* tcsetattr() reports success when any part of the change was applied:
     * read the mode back and insist on the bits this program relies on. */
    struct termios got;
    if (tcgetattr(STDIN_FILENO, &got) != 0 ||
        (got.c_lflag & (ICANON | ECHO | ISIG)) != 0 ||
        (got.c_iflag & (IXON | IXOFF)) != 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &state->saved);
        free(state);
        return NULL;
    }
    state->active = 1;
    return state;
}

void terminal_raw_exit(TermRawState **state) {
    if (!state || !*state) return;
    if ((*state)->active)
        tcsetattr(STDIN_FILENO, TCSANOW, &(*state)->saved);
    free(*state);
    *state = NULL;
}

/**
 * Read one byte from STDIN_FILENO via read(2) — NOT via getchar()/stdio.
 *
 * Rationale: getchar() uses the C stdio buffer.  When getchar() calls
 * read(2) with VMIN=0 VTIME=1 and gets a 0-byte return (timeout for bare
 * ESC), stdio marks the FILE* EOF flag.  All subsequent getchar() calls
 * then return EOF immediately without blocking, causing an infinite
 * redraw loop in the TUI.  Using read(2) directly bypasses the stdio
 * layer entirely and avoids the EOF flag problem.
 *
 * Returns the byte value [0..255], or -1 on error/timeout.
 */
static int read_byte(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? (int)c : -1;
}

static int g_last_printable = 0;

int terminal_last_printable(void) { return g_last_printable; }

TermKey terminal_read_key(void) {
    /* The terminal must already be in raw mode (VMIN=1, VTIME=0). */
    g_last_printable = 0;
    int c = read_byte();
    TermKey result = TERM_KEY_IGNORE;   /* unknown input → silent no-op */

    if (c == '\033') {
        /* Temporarily switch to VMIN=0 VTIME=1 (100 ms timeout) to drain
         * the escape sequence without blocking if it is a bare ESC. */
        struct termios t;
        tcgetattr(STDIN_FILENO, &t);
        struct termios drain = t;
        drain.c_cc[VMIN]  = 0;
        drain.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &drain);

        int c2 = read_byte();
        if (c2 == '[') {
            int c3 = read_byte();
            switch (c3) {
            case 'A': result = TERM_KEY_PREV_LINE; break;  /* ESC[A — Up arrow    */
            case 'B': result = TERM_KEY_NEXT_LINE; break;  /* ESC[B — Down arrow  */
            case 'C': result = TERM_KEY_RIGHT;     break;  /* ESC[C — Right arrow */
            case 'D': result = TERM_KEY_LEFT;      break;  /* ESC[D — Left arrow  */
            case 'H': result = TERM_KEY_HOME;      break;  /* ESC[H — Home        */
            case 'F': result = TERM_KEY_END;       break;  /* ESC[F — End         */
            case '1': case '3': case '4': case '5':
            case '6': case '7': case '8': {
                /* ESC[<n>~ is a plain editing key; ESC[<n>;<mod>~ and
                 * ESC[1;<mod><A-F> are modified keys.  Consume the whole
                 * sequence up to its final byte so no parameter or final
                 * byte is left behind to be read as typed text (TG-3);
                 * only the unmodified ESC[<n>~ form maps to a key. */
                int c4 = read_byte();
                if (c4 == '~') {
                    switch (c3) {
                    case '1': case '7': result = TERM_KEY_HOME;      break;
                    case '3':           result = TERM_KEY_DELETE;    break;
                    case '4': case '8': result = TERM_KEY_END;       break;
                    case '5':           result = TERM_KEY_PREV_PAGE; break;
                    default:            result = TERM_KEY_NEXT_PAGE; break;
                    }
                } else {
                    int ch = c4;
                    while (ch != -1 && !(ch >= 0x40 && ch <= 0x7E))
                        ch = read_byte();
                    result = TERM_KEY_IGNORE;
                }
                break;
            }
            default:
                if (c3 != -1) {
                    int ch;
                    while ((ch = read_byte()) != -1) {
                        if ((ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') || ch == '~') break;
                    }
                }
                result = TERM_KEY_IGNORE;
                break;
            }
        } else if (c2 == 'O') {
            int c3 = read_byte();
            switch (c3) {
            case 'H': result = TERM_KEY_HOME; break; /* ESC O H — Home */
            case 'F': result = TERM_KEY_END;  break; /* ESC O F — End  */
            default:  result = TERM_KEY_IGNORE; break;
            }
        } else {
            result = TERM_KEY_ESC;   /* bare ESC — go back */
        }

        /* Restore VMIN=1 VTIME=0 raw mode. */
        t.c_cc[VMIN]  = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else if (c == '\n' || c == '\r') {
        result = TERM_KEY_ENTER;
    } else if (c == 3 /* Ctrl-C */) {
        result = TERM_KEY_QUIT;
    } else if (c == 4 /* Ctrl-D */) {
        result = TERM_KEY_CTRL_D;
    } else if (c == 1 /* Ctrl-A */) {
        result = TERM_KEY_CTRL_A;
    } else if (c == 5 /* Ctrl-E */) {
        result = TERM_KEY_CTRL_E;
    } else if (c == 11 /* Ctrl-K */) {
        result = TERM_KEY_CTRL_K;
    } else if (c == 23 /* Ctrl-W */) {
        result = TERM_KEY_CTRL_W;
    } else if (c == 127 || c == 8 /* DEL / Backspace */) {
        result = TERM_KEY_BACK;
    } else if (c >= 32 && c <= 126) {
        g_last_printable = c;
        result = TERM_KEY_IGNORE;
    }
    /* c == -1 (read error/timeout) → result stays TERM_KEY_IGNORE */

    return result;
}

int terminal_wcwidth(uint32_t cp) {
    /* The project's own Unicode table, not libc wcwidth(): the program never
     * calls setlocale(), and in the "C" locale libc reports -1 for every code
     * point above ASCII (TG-2). */
    return core_wcwidth(cp);
}

int terminal_wait_key(int timeout_ms) {
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0) {
        /* EINTR (e.g. SIGWINCH) is not a hard error — the caller retries;
         * anything else is reported as an error (TG-5). */
        return (errno == EINTR) ? TERM_WAIT_INTR : TERM_WAIT_ERROR;
    }
    if (rc == 0) return 0;
    return (pfd.revents & POLLIN) ? 1 : 0;
}

/* ---- SIGTERM / SIGHUP / SIGINT cleanup handlers ---- */

/**
 * Pointer to the saved termios inside the active TermRawState.
 * Written once from terminal_install_cleanup_handlers() before
 * any signal can arrive; read only from signal handlers thereafter.
 * volatile to prevent the compiler from caching the load.
 */
static volatile struct termios *g_saved_termios = NULL;

/**
 * Async-signal-safe terminal cleanup and re-raise.
 *
 * Uses only async-signal-safe functions:
 *   tcsetattr(3)  — POSIX async-signal-safe
 *   write(2)      — POSIX async-signal-safe
 *   signal(2)     — POSIX async-signal-safe
 *   raise(3)      — POSIX async-signal-safe
 */
static void cleanup_signal_handler(int sig) {
    /* Restore cooked mode if we have a saved state. */
    if (g_saved_termios) {
        /* Cast away volatile: tcsetattr requires a non-volatile pointer.
         * The cast is safe because we only write this pointer once and
         * the pointed-to struct outlives the signal handler. */
        struct termios saved;
        saved = *(struct termios *)g_saved_termios;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    /* Show the cursor (ESC[?25h) — write(2) is async-signal-safe.
     * Ignore the return value: if the write fails there is nothing
     * sensible to do inside a signal handler. */
    static const char show_cursor[] = "\033[?25h";
    ssize_t _wr_unused = write(STDOUT_FILENO, show_cursor,
                               sizeof(show_cursor) - 1);
    (void)_wr_unused;
    /* Reset to default handler and re-raise so the shell sees the
     * correct exit status (e.g. 128 + SIGTERM). */
    signal(sig, SIG_DFL);
    raise(sig);
}

void terminal_install_cleanup_handlers(TermRawState *state) {
    if (!state) return;
    /* Expose the saved termios to the signal handler. */
    g_saved_termios = &state->saved;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cleanup_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* no SA_RESTART — we re-raise immediately */

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
}

/* ---- SIGWINCH / resize notifications ---- */

static volatile sig_atomic_t g_resize_pending = 0;
static int g_resize_handler_installed = 0;

static void resize_handler(int sig) {
    (void)sig;
    g_resize_pending = 1;
}

void terminal_enable_resize_notifications(void) {
    if (g_resize_handler_installed) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = resize_handler;
    sigemptyset(&sa.sa_mask);
    /* No SA_RESTART on purpose: we want blocking read(2) in
     * terminal_read_key to return with EINTR so the TUI loop can
     * observe the resize between keystrokes. */
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);
    g_resize_handler_installed = 1;
}

int terminal_consume_resize(void) {
    if (g_resize_pending) {
        g_resize_pending = 0;
        return 1;
    }
    return 0;
}

/* Terminal mode to put back when a signal ends the process during the
 * password prompt.  Written before the handlers are installed. */
static struct termios g_pw_saved;
static int g_pw_fd = -1;

/* Async-signal-safe: tcsetattr, signal, raise only. */
static void password_signal_handler(int sig) {
    if (g_pw_fd >= 0)
        tcsetattr(g_pw_fd, TCSANOW, &g_pw_saved);
    signal(sig, SIG_DFL);
    raise(sig);
}

int terminal_read_password(const char *prompt, char *buf, size_t size) {
    if (!buf || size == 0) return -1;

    int fd = fileno(stdin);
    int is_tty = isatty(fd);

    if (is_tty) {
        /* Echo goes off BEFORE the prompt is written: whatever is typed once
         * the prompt is visible must already be silent (TG-6). */
        struct termios oldt, newt;
        if (tcgetattr(fd, &oldt) != 0) return -1;
        newt = oldt;
        newt.c_lflag &= ~(unsigned)ECHO;
        if (tcsetattr(fd, TCSANOW, &newt) != 0) return -1;

        /* ISIG stays on, so Ctrl-C ends the process: the handlers put the
         * terminal back first, otherwise the shell is left without echo. */
        g_pw_saved = oldt;
        g_pw_fd = fd;
        struct sigaction sa, old_int, old_term, old_hup, old_quit;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = password_signal_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT,  &sa, &old_int);
        sigaction(SIGTERM, &sa, &old_term);
        sigaction(SIGHUP,  &sa, &old_hup);
        sigaction(SIGQUIT, &sa, &old_quit);

        printf("%s: ", prompt);
        fflush(stdout);

        char *line = NULL;
        size_t len = 0;
        ssize_t nread = getline(&line, &len, stdin);

        tcsetattr(fd, TCSANOW, &oldt);
        sigaction(SIGINT,  &old_int,  NULL);
        sigaction(SIGTERM, &old_term, NULL);
        sigaction(SIGHUP,  &old_hup,  NULL);
        sigaction(SIGQUIT, &old_quit, NULL);
        printf("\n");

        if (nread == -1 || !line) {
            free(line);
            return -1;
        }

        /* Strip trailing newline */
        size_t slen = strlen(line);
        if (slen > 0 && (line[slen-1] == '\n' || line[slen-1] == '\r'))
            line[--slen] = '\0';
        if (slen > 0 && (line[slen-1] == '\r'))
            line[--slen] = '\0';

        if (slen >= size) slen = size - 1;
        memcpy(buf, line, slen);
        buf[slen] = '\0';
        free(line);
        return (int)slen;
    } else {
        /* Non-TTY: read from stdin without echo manipulation */
        char *line = NULL;
        size_t len = 0;
        ssize_t nread = getline(&line, &len, stdin);
        if (nread == -1 || !line) {
            free(line);
            return -1;
        }
        size_t slen = strlen(line);
        if (slen > 0 && (line[slen-1] == '\n' || line[slen-1] == '\r'))
            line[--slen] = '\0';
        if (slen > 0 && (line[slen-1] == '\r'))
            line[--slen] = '\0';
        if (slen >= size) slen = size - 1;
        memcpy(buf, line, slen);
        buf[slen] = '\0';
        free(line);
        return (int)slen;
    }
}
