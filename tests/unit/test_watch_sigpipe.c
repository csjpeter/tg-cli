/**
 * @file test_watch_sigpipe.c
 * @brief Unit tests for FEAT-17: SIGPIPE / EPIPE handling in the watch loop.
 *
 * Verifies that:
 *  1. signal(SIGPIPE, SIG_IGN) prevents the process from being killed when
 *     writing to a broken pipe — write(2) returns -1 with errno==EPIPE.
 *  2. A helper that mimics the watch-loop write path returns a "stop" signal
 *     (rather than crashing) when the downstream pipe fd is closed.
 */

#include "test_helpers.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/**
 * Mimics the per-message write path used in cmd_watch():
 *   printf() → fflush(stdout), redirected to a FILE* backed by @p fd.
 *
 * Returns 0 on success, 1 if EPIPE was detected (clean stop requested).
 */
static int watch_write_line(int fd, const char *line) {
    FILE *f = fdopen(dup(fd), "w");
    if (!f) return 1; /* cannot wrap fd */
    int ret = 0;
    if (fputs(line, f) == EOF || fflush(f) != 0) {
        if (errno == EPIPE) ret = 1;
    }
    fclose(f);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/**
 * After signal(SIGPIPE, SIG_IGN), writing to the write-end of a pipe
 * whose read-end is already closed must NOT kill the process; instead
 * write(2) / fflush must return an error with errno == EPIPE.
 */
static void test_sigpipe_ignored_write_returns_epipe(void) {
    /* Install SIG_IGN — mirrors what cmd_watch() now does. */
    signal(SIGPIPE, SIG_IGN);

    int fds[2];
    ASSERT(pipe(fds) == 0, "pipe(): must succeed");

    /* Close the read end — any write to fds[1] will now yield EPIPE. */
    close(fds[0]);

    /* Write to the broken pipe write end directly. */
    errno = 0;
    ssize_t n = write(fds[1], "hello\n", 6);
    int saved_errno = errno;

    close(fds[1]);

    ASSERT(n == -1,             "write to broken pipe: must return -1");
    ASSERT(saved_errno == EPIPE, "write to broken pipe: errno must be EPIPE");
}

/**
 * The watch-loop write helper must return 1 (stop) when the downstream
 * pipe is closed, and must not crash.
 */
static void test_watch_write_detects_epipe(void) {
    signal(SIGPIPE, SIG_IGN);

    int fds[2];
    ASSERT(pipe(fds) == 0, "pipe(): must succeed");

    /* Close the read end to simulate 'head' exiting. */
    close(fds[0]);

    int stop = watch_write_line(fds[1], "test line\n");

    close(fds[1]);

    ASSERT(stop == 1, "watch_write_line: must return 1 (stop) on EPIPE");
}

/**
 * When the pipe is intact the helper must return 0 (keep running).
 */
static void test_watch_write_succeeds_on_open_pipe(void) {
    signal(SIGPIPE, SIG_IGN);

    int fds[2];
    ASSERT(pipe(fds) == 0, "pipe(): must succeed");

    int stop = watch_write_line(fds[1], "test line\n");

    /* Drain so the kernel buffer does not fill up; result intentionally ignored. */
    char buf[64];
    ssize_t _n = read(fds[0], buf, sizeof(buf));
    (void)_n;

    close(fds[0]);
    close(fds[1]);

    ASSERT(stop == 0, "watch_write_line: must return 0 on open pipe");
}

/* ------------------------------------------------------------------ */
/* Suite entry point                                                   */
/* ------------------------------------------------------------------ */

void run_watch_sigpipe_tests(void) {
    RUN_TEST(test_sigpipe_ignored_write_returns_epipe);
    RUN_TEST(test_watch_write_detects_epipe);
    RUN_TEST(test_watch_write_succeeds_on_open_pipe);
}
