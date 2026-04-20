/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file rl_harness.c
 * @brief Minimal driver for rl_readline used by PTY-03 tests.
 *
 * Runs rl_readline in a loop until Ctrl-D / EOF.
 * Each accepted line is echoed as:  "ACCEPTED:<line>\n"
 * so the test can wait for that sentinel.
 *
 * Usage:  rl_harness
 *
 * The process exits 0 after Ctrl-D on an empty line.
 */

#include "readline.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUF_SIZE 256

int main(void) {
    LineHistory hist;
    rl_history_init(&hist);

    char buf[BUF_SIZE];
    for (;;) {
        int rc = rl_readline("rl> ", buf, sizeof(buf), &hist);
        if (rc < 0) {
            /* Ctrl-C or EOF on empty line */
            break;
        }
        if (rc > 0) {
            rl_history_add(&hist, buf);
        }
        /* Always print the accepted line so the test can observe it. */
        printf("ACCEPTED:%s\n", buf);
        fflush(stdout);
    }
    return 0;
}
