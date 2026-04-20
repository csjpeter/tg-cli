/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file terminal_coverage_harness.c
 * @brief Harness binary that exercises terminal.c paths for coverage.
 *
 * Each mode exercises a specific area of src/platform/posix/terminal.c.
 * The test runner (test_terminal_coverage.c) drives this binary through
 * a PTY or via piped stdin to reach all uncovered lines.
 *
 * Modes:
 *   cols_rows        - call terminal_cols() / terminal_rows() and print results
 *   raw_enter_exit   - enter/exit raw mode, print result
 *   read_key         - raw mode: read one key, print name, exit
 *   wait_key         - call terminal_wait_key(500), print READY/TIMEOUT
 *   install_handlers - enter raw mode, install cleanup handlers, then exit 0
 *   resize_notify    - enable resize notifications, loop until resize or 'q'
 *   passwd_nontty    - call terminal_read_password when stdin is a pipe (non-TTY)
 */

#include "platform/terminal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/* ---- cols_rows --------------------------------------------------------- */

static int mode_cols_rows(void) {
    int c = terminal_cols();
    int r = terminal_rows();
    /* On a real PTY both should be > 0; on non-TTY fallback values apply. */
    printf("COLS:%d\n", c);
    printf("ROWS:%d\n", r);
    fflush(stdout);
    return 0;
}

/* ---- raw_enter_exit ---------------------------------------------------- */

static int mode_raw_enter_exit(void) {
    TermRawState *st = terminal_raw_enter();
    if (!st) {
        printf("RAW_ENTER:FAIL\n");
        fflush(stdout);
        return 1;
    }
    printf("RAW_ENTER:OK\n");
    fflush(stdout);
    terminal_raw_exit(&st);
    if (st != NULL) {
        printf("RAW_EXIT:FAIL\n");
        fflush(stdout);
        return 1;
    }
    printf("RAW_EXIT:OK\n");
    fflush(stdout);
    return 0;
}

/* ---- read_key ---------------------------------------------------------- */

/**
 * Enter raw mode, print "READY", then read keys until 'q' or a count limit.
 * For each key print its name so the test can assert receipt.
 */
static int mode_read_key(void) {
    TermRawState *st = terminal_raw_enter();
    if (!st) {
        printf("RAW:FAIL\n");
        fflush(stdout);
        return 1;
    }
    printf("READY\n");
    fflush(stdout);

    /* Read up to 32 keystrokes, exit on 'q' (printable) or TERM_KEY_QUIT */
    for (int i = 0; i < 32; i++) {
        /* Use terminal_wait_key so that path is also hit. */
        int avail = terminal_wait_key(3000);
        if (avail <= 0) {
            printf("TIMEOUT\n");
            fflush(stdout);
            break;
        }
        TermKey k = terminal_read_key();
        int lp = terminal_last_printable();
        switch (k) {
        case TERM_KEY_QUIT:      printf("KEY:QUIT\n");      fflush(stdout); goto done;
        case TERM_KEY_ENTER:     printf("KEY:ENTER\n");     fflush(stdout); break;
        case TERM_KEY_ESC:       printf("KEY:ESC\n");       fflush(stdout); break;
        case TERM_KEY_BACK:      printf("KEY:BACK\n");      fflush(stdout); break;
        case TERM_KEY_LEFT:      printf("KEY:LEFT\n");      fflush(stdout); break;
        case TERM_KEY_RIGHT:     printf("KEY:RIGHT\n");     fflush(stdout); break;
        case TERM_KEY_PREV_LINE: printf("KEY:UP\n");        fflush(stdout); break;
        case TERM_KEY_NEXT_LINE: printf("KEY:DOWN\n");      fflush(stdout); break;
        case TERM_KEY_HOME:      printf("KEY:HOME\n");      fflush(stdout); break;
        case TERM_KEY_END:       printf("KEY:END\n");       fflush(stdout); break;
        case TERM_KEY_DELETE:    printf("KEY:DELETE\n");    fflush(stdout); break;
        case TERM_KEY_PREV_PAGE: printf("KEY:PGUP\n");      fflush(stdout); break;
        case TERM_KEY_NEXT_PAGE: printf("KEY:PGDN\n");      fflush(stdout); break;
        case TERM_KEY_CTRL_A:    printf("KEY:CTRL_A\n");    fflush(stdout); break;
        case TERM_KEY_CTRL_E:    printf("KEY:CTRL_E\n");    fflush(stdout); break;
        case TERM_KEY_CTRL_K:    printf("KEY:CTRL_K\n");    fflush(stdout); break;
        case TERM_KEY_CTRL_W:    printf("KEY:CTRL_W\n");    fflush(stdout); break;
        case TERM_KEY_CTRL_D:    printf("KEY:CTRL_D\n");    fflush(stdout); goto done;
        case TERM_KEY_IGNORE:
            if (lp) {
                if (lp == 'q') {
                    printf("KEY:q\n");
                    fflush(stdout);
                    goto done;
                }
                printf("KEY:CHAR:%c\n", (char)lp);
                fflush(stdout);
            } else {
                printf("KEY:IGNORE\n");
                fflush(stdout);
            }
            break;
        }
    }
done:
    terminal_raw_exit(&st);
    return 0;
}

/* ---- wait_key ---------------------------------------------------------- */

static int mode_wait_key(void) {
    /* Signal readiness so the test can send a byte after seeing READY. */
    printf("READY\n");
    fflush(stdout);
    int rc = terminal_wait_key(3000);
    if (rc > 0)
        printf("WAIT_KEY:READY\n");
    else if (rc == 0)
        printf("WAIT_KEY:TIMEOUT\n");
    else
        printf("WAIT_KEY:INTR\n");
    fflush(stdout);
    return 0;
}

/* ---- install_handlers -------------------------------------------------- */

static int mode_install_handlers(void) {
    TermRawState *st = terminal_raw_enter();
    if (!st) {
        printf("HANDLERS:FAIL\n");
        fflush(stdout);
        return 1;
    }
    /* Install the handlers — this exercises terminal_install_cleanup_handlers
     * and exposes g_saved_termios. */
    terminal_install_cleanup_handlers(st);
    printf("HANDLERS:OK\n");
    fflush(stdout);
    /* Clean exit: restore terminal before exiting so the PTY isn't stuck. */
    terminal_raw_exit(&st);
    return 0;
}

/* ---- resize_notify ----------------------------------------------------- */

static int mode_resize_notify(void) {
    terminal_enable_resize_notifications();
    /* Call a second time to cover the "already installed" guard. */
    terminal_enable_resize_notifications();

    printf("RESIZE_READY\n");
    fflush(stdout);

    /* Poll for a resize event or 'q' for up to 3 s. */
    TermRawState *st = terminal_raw_enter();
    if (!st) {
        printf("RAW:FAIL\n");
        fflush(stdout);
        return 1;
    }
    for (int i = 0; i < 30; i++) {
        if (terminal_consume_resize()) {
            printf("RESIZE_DETECTED\n");
            fflush(stdout);
            break;
        }
        int avail = terminal_wait_key(100);
        if (avail > 0) {
            TermKey k = terminal_read_key();
            int lp = terminal_last_printable();
            if (k == TERM_KEY_QUIT || (k == TERM_KEY_IGNORE && lp == 'q')) {
                printf("QUIT\n");
                fflush(stdout);
                break;
            }
        }
    }
    terminal_raw_exit(&st);
    return 0;
}

/* ---- passwd_nontty ----------------------------------------------------- */

/**
 * Call terminal_read_password when stdin is NOT a TTY (piped).
 * This exercises the else-branch in terminal_read_password (lines 328-346).
 */
static int mode_passwd_nontty(void) {
    char buf[64];
    int rc = terminal_read_password("TestPrompt", buf, sizeof(buf));
    if (rc < 0) {
        printf("PASSWD:ERROR\n");
        fflush(stdout);
        return 1;
    }
    printf("PASSWD:%s\n", buf);
    printf("PASSWD_LEN:%d\n", rc);
    fflush(stdout);
    return 0;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: terminal_coverage_harness <mode>\n");
        return 2;
    }

    if (strcmp(argv[1], "cols_rows")        == 0) return mode_cols_rows();
    if (strcmp(argv[1], "raw_enter_exit")   == 0) return mode_raw_enter_exit();
    if (strcmp(argv[1], "read_key")         == 0) return mode_read_key();
    if (strcmp(argv[1], "wait_key")         == 0) return mode_wait_key();
    if (strcmp(argv[1], "install_handlers") == 0) return mode_install_handlers();
    if (strcmp(argv[1], "resize_notify")    == 0) return mode_resize_notify();
    if (strcmp(argv[1], "passwd_nontty")    == 0) return mode_passwd_nontty();

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
