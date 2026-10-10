/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Unit tests for the masked secret prompt (non-terminal path). */

/**
 * @file tests/unit/test_secret_prompt.c
 * @brief secret_prompt_read() with stdin a pipe: bad arguments, line ending
 *        removal, truncation, EOF. The masked terminal path is covered by
 *        the PTY test test_secret_prompt_is_masked.
 */

#include "test_helpers.h"
#include "app/secret_prompt.h"

#include <string.h>
#include <unistd.h>

/* Run @p fn with stdin replaced by a pipe that holds @p input. */
static int with_stdin(const char *input, char *out, size_t cap) {
    int fds[2];
    if (pipe(fds) != 0) return -2;
    int saved = dup(STDIN_FILENO);
    if (input && *input) {
        if (write(fds[1], input, strlen(input)) < 0) return -2;
    }
    close(fds[1]);
    dup2(fds[0], STDIN_FILENO);
    close(fds[0]);
    int rc = secret_prompt_read("2FA password", out, cap);
    dup2(saved, STDIN_FILENO);
    close(saved);
    return rc;
}

static void test_bad_arguments(void) {
    char buf[8];
    ASSERT(secret_prompt_read("x", NULL, 8) == -1, "NULL buffer");
    ASSERT(secret_prompt_read("x", buf, 0) == -1, "zero capacity");
}

static void test_line_ending_removed(void) {
    char buf[64];
    ASSERT(with_stdin("hunter2\r\n", buf, sizeof(buf)) == 0, "read ok");
    ASSERT(strcmp(buf, "hunter2") == 0, "no CR/LF in the secret");
}

static void test_eof_is_error_and_clears_buffer(void) {
    char buf[64];
    memset(buf, 'x', sizeof(buf));
    ASSERT(with_stdin("", buf, sizeof(buf)) == -1, "EOF reports an error");
    ASSERT(buf[0] == '\0', "buffer left empty");
}

static void test_truncates_to_capacity(void) {
    char buf[6];
    ASSERT(with_stdin("abcdefghij\n", buf, sizeof(buf)) == 0, "read ok");
    ASSERT(strlen(buf) <= 5, "never writes past the capacity");
}

void test_secret_prompt_run(void) {
    RUN_TEST(test_bad_arguments);
    RUN_TEST(test_line_ending_removed);
    RUN_TEST(test_eof_is_error_and_clears_buffer);
    RUN_TEST(test_truncates_to_capacity);
}
