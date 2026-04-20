/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file password_harness.c
 * @brief Minimal driver for terminal_read_password used by TEST-87 PTY tests.
 *
 * Exercises src/platform/posix/terminal.c::terminal_read_password directly
 * without requiring the full tg-tui login path or a mock Telegram server.
 *
 * Usage (chosen via argv[1]):
 *
 *   password_harness prompt
 *     Prompt once with terminal_read_password and print either
 *       "ACCEPTED:<value>\n"   on success (rc >= 0)
 *       "ERROR\n"              when the function returns -1
 *     Exits 0 on success, 1 on error.
 *
 *   password_harness prompt_then_echo
 *     Prompt once with terminal_read_password, print "ACCEPTED:<value>\n",
 *     then read a second line with fgets() and print "ECHO:<value>\n".
 *     Used to verify echo is restored on return: if the echo was still
 *     suppressed, the second read would not be visible on the PTY.
 *     Exits 0.
 *
 *   password_harness prompt_big
 *     Same as "prompt" but with a 512-byte buffer for the 256-char input
 *     test.  Prints "ACCEPTED:<value>" and "LEN:<n>\n", exits 0.
 */

#include "platform/terminal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SMALL_BUF 128
#define BIG_BUF   512

static int do_prompt_once(size_t cap) {
    char buf[BIG_BUF];
    if (cap > sizeof(buf)) cap = sizeof(buf);
    memset(buf, 0, cap);
    int rc = terminal_read_password("Password", buf, cap);
    if (rc < 0) {
        printf("ERROR\n");
        fflush(stdout);
        return 1;
    }
    /* Intentionally print the resulting buffer so the test can assert the
     * backspace- and long-input- handling works. The PTY output the test
     * inspects is the harness's own stdout, not the child's echoed input. */
    printf("ACCEPTED:%s\n", buf);
    printf("LEN:%d\n", rc);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: password_harness <mode>\n");
        return 2;
    }

    if (strcmp(argv[1], "prompt") == 0) {
        return do_prompt_once(SMALL_BUF);
    }

    if (strcmp(argv[1], "prompt_big") == 0) {
        return do_prompt_once(BIG_BUF);
    }

    if (strcmp(argv[1], "prompt_then_echo") == 0) {
        char pw[SMALL_BUF];
        memset(pw, 0, sizeof(pw));
        int rc = terminal_read_password("Password", pw, sizeof(pw));
        if (rc < 0) {
            printf("ERROR\n");
            fflush(stdout);
            return 1;
        }
        printf("ACCEPTED:%s\n", pw);
        fflush(stdout);

        /* Second read: ordinary line with no echo manipulation.  If the
         * first call failed to restore termios (ECHO bit still cleared),
         * the user's typed characters would not appear on the PTY master
         * and the test would see no "CONFIRM" string followed by typed
         * text on screen. */
        char line[SMALL_BUF];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("ECHO_ERROR\n");
            fflush(stdout);
            return 1;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
            line[--n] = '\0';
        printf("ECHO:%s\n", line);
        fflush(stdout);
        return 0;
    }

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
