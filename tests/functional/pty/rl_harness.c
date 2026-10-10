/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-19 */
/* Minimal driver for tui_readline used by the PTY tests. */

/**
 * @file rl_harness.c
 * @brief Minimal driver for tui_readline used by PTY-03 tests.
 *
 * Runs tui_readline in a loop until Ctrl-D / EOF.
 * Each accepted line is echoed as:  "ACCEPTED:<line>\n"
 * so the test can wait for that sentinel.
 *
 * Usage:  rl_harness
 *
 * The process exits 0 after Ctrl-D on an empty line.
 */

#include <tuikit/lineedit.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUF_SIZE 256

int main(void) {
    tui_history hist;
    tui_history_init(&hist, 256);

    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.history = &hist;

    char buf[BUF_SIZE];
    for (;;) {
        int rc = tui_readline("rl> ", buf, sizeof(buf), &o);
        if (rc < 0) {
            /* Ctrl-C or EOF on empty line */
            break;
        }
        /* Always print the accepted line so the test can observe it. */
        printf("ACCEPTED:%s\n", buf);
        fflush(stdout);
    }
    tui_history_free(&hist);
    return 0;
}
