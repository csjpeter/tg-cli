/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Terminal_raw_enter() failure paths (TG-4). */

/**
 * @file test_terminal_raw_faults.c
 * @brief terminal_raw_enter() failure paths (TG-4).
 *
 * src/platform/posix/terminal.c is compiled into this executable with
 * tcgetattr/tcsetattr renamed (see tests/unit/CMakeLists.txt), so the tests
 * decide what the "terminal" answers.
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/terminal.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

int g_tests_run = 0;
int g_tests_failed = 0;
const char *g_test_filter = NULL;

#define CHECK(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

/* What the fake terminal does. */
static int  g_set_calls;
static int  g_fail_set;          /* tcsetattr returns -1 */
static int  g_ignore_flow;       /* tcsetattr "succeeds" but keeps IXON */
static struct termios g_state;   /* the fake terminal's current mode */

int tg_test_tcgetattr(int fd, struct termios *t) {
    (void)fd;
    *t = g_state;
    return 0;
}

/* TG-6: how much of the prompt had reached stdout when echo was cleared. */
static long g_out_at_echo_off = -1;
static int  g_fake_tty;

int tg_test_isatty(int fd) {
    (void)fd;
    return g_fake_tty;
}

static long stdout_size(void) {
    struct stat st;
    if (fstat(STDOUT_FILENO, &st) != 0) return -1;
    return (long)st.st_size;
}

int tg_test_tcsetattr(int fd, int when, const struct termios *t) {
    (void)fd; (void)when;
    if (!(t->c_lflag & ECHO) && g_out_at_echo_off < 0)
        g_out_at_echo_off = stdout_size();
    g_set_calls++;
    if (g_fail_set) return -1;
    g_state = *t;
    if (g_ignore_flow) g_state.c_iflag |= IXON;
    return 0;
}

/* terminal_wait_key(): what poll() answers (TG-5). */
static int g_poll_rc;
static int g_poll_errno;
static short g_poll_revents;

int tg_test_poll(struct pollfd *fds, nfds_t n, int timeout) {
    (void)n; (void)timeout;
    fds[0].revents = g_poll_revents;
    errno = g_poll_errno;
    return g_poll_rc;
}

static void reset(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.c_iflag = IXON | ICRNL;
    g_state.c_lflag = ICANON | ECHO | ISIG;
    g_set_calls = 0;
    g_fail_set = 0;
    g_ignore_flow = 0;
}

static void test_success_clears_flow_control(void) {
    reset();
    TermRawState *st = terminal_raw_enter();
    CHECK(st != NULL, "raw mode is entered");
    CHECK((g_state.c_iflag & (IXON | IXOFF)) == 0, "IXON and IXOFF are off");
    CHECK((g_state.c_lflag & (ICANON | ECHO | ISIG)) == 0, "line flags are off");
    terminal_raw_exit(&st);
    CHECK(st == NULL, "state is released");
    CHECK((g_state.c_iflag & IXON) != 0, "exit restores IXON");
    CHECK((g_state.c_lflag & (ICANON | ECHO | ISIG)) == (ICANON | ECHO | ISIG),
          "exit restores the line flags");
}

static void test_tcsetattr_failure_is_reported(void) {
    reset();
    g_fail_set = 1;
    TermRawState *st = terminal_raw_enter();
    CHECK(st == NULL, "a failed tcsetattr() makes terminal_raw_enter() fail");
}

static void test_partial_change_is_reported_and_undone(void) {
    reset();
    g_ignore_flow = 1;
    TermRawState *st = terminal_raw_enter();
    CHECK(st == NULL, "a mode that did not take makes terminal_raw_enter() fail");
    CHECK(g_set_calls == 2, "the saved mode is written back");
    CHECK((g_state.c_lflag & ICANON) != 0, "the terminal is left as it was found");
}

static void test_wait_key_interrupt_differs_from_error(void) {
    g_poll_rc = -1; g_poll_errno = EINTR; g_poll_revents = 0;
    CHECK(terminal_wait_key(10) == TERM_WAIT_INTR, "EINTR is reported as an interrupt");
    g_poll_rc = -1; g_poll_errno = EBADF;
    CHECK(terminal_wait_key(10) == TERM_WAIT_ERROR, "EBADF is reported as an error");
    CHECK(TERM_WAIT_INTR != TERM_WAIT_ERROR, "the two codes differ");
    g_poll_rc = 0; g_poll_errno = 0;
    CHECK(terminal_wait_key(10) == 0, "a timeout is 0");
    g_poll_rc = 1; g_poll_revents = POLLIN;
    CHECK(terminal_wait_key(10) == 1, "pending input is 1");
}

static void test_password_echo_off_before_prompt(void) {
    char path[] = "/tmp/tg-cli-pw-XXXXXX";
    int tmp = mkstemp(path);
    CHECK(tmp >= 0, "temporary file for the captured output");
    int saved_out = dup(STDOUT_FILENO);
    fflush(stdout);
    dup2(tmp, STDOUT_FILENO);
    FILE *in = freopen("/dev/null", "r", stdin);

    reset();
    g_fake_tty = 1;
    g_out_at_echo_off = -1;
    char buf[32];
    int rc = terminal_read_password("Password", buf, sizeof(buf));

    fflush(stdout);
    long total = stdout_size();
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
    close(tmp);
    unlink(path);
    g_fake_tty = 0;

    CHECK(in != NULL, "stdin can be redirected");
    CHECK(rc == -1, "end of input is an error");
    CHECK(g_out_at_echo_off == 0,
          "echo is cleared before a single byte of the prompt is written");
    CHECK(total > 0, "the prompt was written afterwards");
    CHECK((g_state.c_lflag & ECHO) != 0, "echo is restored after the read");
}

int main(void) {
    test_password_echo_off_before_prompt();
    test_wait_key_interrupt_differs_from_error();
    test_success_clears_flow_control();
    test_tcsetattr_failure_is_reported();
    test_partial_change_is_reported_and_undone();
    printf("%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
