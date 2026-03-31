/**
 * @file test_readline.c
 * @brief Unit tests for readline.c (history API and non-TTY path).
 *
 * The interactive (TTY) path cannot be unit-tested without a real terminal,
 * so these tests focus on the history API and the non-TTY fallback.
 */

#include "test_helpers.h"
#include "readline.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- Test: rl_history_init zeroes the struct ---- */
static void test_history_init(void) {
    LineHistory h;
    memset(&h, 0xFF, sizeof(h)); /* fill with garbage */
    rl_history_init(&h);
    ASSERT(h.count == 0, "history_init: count must be 0");
    ASSERT(h.head  == 0, "history_init: head must be 0");
}

/* ---- Test: rl_history_init with NULL is safe ---- */
static void test_history_init_null(void) {
    rl_history_init(NULL); /* must not crash */
    ASSERT(1, "history_init(NULL): must not crash");
}

/* ---- Test: rl_history_add basic ---- */
static void test_history_add_basic(void) {
    LineHistory h;
    rl_history_init(&h);
    rl_history_add(&h, "hello");
    rl_history_add(&h, "world");
    ASSERT(h.count == 2, "history_add: count must be 2 after two adds");
}

/* ---- Test: rl_history_add ignores empty strings ---- */
static void test_history_add_empty(void) {
    LineHistory h;
    rl_history_init(&h);
    rl_history_add(&h, "");
    ASSERT(h.count == 0, "history_add: empty string must not be added");
}

/* ---- Test: rl_history_add ignores NULL ---- */
static void test_history_add_null(void) {
    LineHistory h;
    rl_history_init(&h);
    rl_history_add(&h, NULL);
    ASSERT(h.count == 0, "history_add: NULL must not be added");
}

/* ---- Test: rl_history_add ignores consecutive duplicate ---- */
static void test_history_add_duplicate(void) {
    LineHistory h;
    rl_history_init(&h);
    rl_history_add(&h, "alpha");
    rl_history_add(&h, "alpha"); /* duplicate */
    ASSERT(h.count == 1, "history_add: consecutive duplicate must not be added");
    rl_history_add(&h, "beta");
    rl_history_add(&h, "alpha"); /* non-consecutive duplicate — should be added */
    ASSERT(h.count == 3, "history_add: non-consecutive duplicate must be added");
}

/* ---- Test: rl_history_add wraps around when full ---- */
static void test_history_add_wrap(void) {
    LineHistory h;
    rl_history_init(&h);

    char buf[32];
    for (int i = 0; i < RL_HISTORY_MAX + 5; i++) {
        snprintf(buf, sizeof(buf), "line%d", i);
        rl_history_add(&h, buf);
    }
    ASSERT(h.count == RL_HISTORY_MAX,
           "history_add wrap: count must not exceed RL_HISTORY_MAX");
}

/* ---- Test: rl_readline NULL args return -1 ---- */
static void test_readline_null_args(void) {
    char buf[64];
    ASSERT(rl_readline(">> ", NULL, 64,   NULL) == -1,
           "readline: NULL buf must return -1");
    ASSERT(rl_readline(">> ", buf,  0,    NULL) == -1,
           "readline: zero size must return -1");
}

/* ---- Test: rl_readline non-TTY reads from stdin pipe ----
 *
 * We redirect a pipe into stdin, call rl_readline, and verify the output.
 * terminal_is_tty(STDIN_FILENO) returns 0 for a pipe, triggering the
 * non-TTY fallback path which does a plain fgets().
 */
static void test_readline_nontty_basic(void) {
    /* Save original stdin */
    int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin == -1) {
        ASSERT(0, "readline nontty: dup(stdin) failed — test skipped");
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        close(saved_stdin);
        ASSERT(0, "readline nontty: pipe() failed — test skipped");
        return;
    }

    /* Write test input into the write end, then close it */
    const char *input = "hello pipe\n";
    { ssize_t w = write(pipefd[1], input, strlen(input)); (void)w; }
    close(pipefd[1]);

    /* Replace stdin with the read end of the pipe */
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    char buf[128];
    int rc = rl_readline(NULL, buf, sizeof(buf), NULL);

    /* Restore stdin */
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    ASSERT(rc >= 0, "readline nontty: must succeed on pipe input");
    ASSERT(strcmp(buf, "hello pipe") == 0,
           "readline nontty: must return input without trailing newline");
}

/* ---- Test: rl_readline non-TTY strips trailing CR+LF ---- */
static void test_readline_nontty_crlf(void) {
    int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin == -1) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) { close(saved_stdin); return; }

    const char *input = "test\r\n";
    { ssize_t w = write(pipefd[1], input, strlen(input)); (void)w; }
    close(pipefd[1]);

    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    char buf[128];
    int rc = rl_readline(NULL, buf, sizeof(buf), NULL);

    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    ASSERT(rc >= 0, "readline nontty crlf: must succeed");
    ASSERT(strcmp(buf, "test") == 0,
           "readline nontty crlf: must strip CR+LF");
}

/* ---- Test: rl_readline non-TTY EOF returns -1 ---- */
static void test_readline_nontty_eof(void) {
    int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin == -1) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) { close(saved_stdin); return; }

    /* Write nothing — immediate EOF */
    close(pipefd[1]);

    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    char buf[128];
    int rc = rl_readline(NULL, buf, sizeof(buf), NULL);

    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    ASSERT(rc == -1, "readline nontty eof: must return -1 on EOF");
}

/* ---- Test: rl_readline non-TTY with history parameter is safe ---- */
static void test_readline_nontty_with_history(void) {
    int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin == -1) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) { close(saved_stdin); return; }

    const char *input = "cmd arg\n";
    { ssize_t w = write(pipefd[1], input, strlen(input)); (void)w; }
    close(pipefd[1]);

    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    LineHistory h;
    rl_history_init(&h);

    char buf[128];
    int rc = rl_readline(">>> ", buf, sizeof(buf), &h);

    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    ASSERT(rc >= 0, "readline nontty+hist: must succeed");
    ASSERT(strcmp(buf, "cmd arg") == 0,
           "readline nontty+hist: content must match");
}

void run_readline_tests(void) {
    RUN_TEST(test_history_init);
    RUN_TEST(test_history_init_null);
    RUN_TEST(test_history_add_basic);
    RUN_TEST(test_history_add_empty);
    RUN_TEST(test_history_add_null);
    RUN_TEST(test_history_add_duplicate);
    RUN_TEST(test_history_add_wrap);
    RUN_TEST(test_readline_null_args);
    RUN_TEST(test_readline_nontty_basic);
    RUN_TEST(test_readline_nontty_crlf);
    RUN_TEST(test_readline_nontty_eof);
    RUN_TEST(test_readline_nontty_with_history);
}
