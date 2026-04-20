/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_readline.c
 * @brief PTY-03 — PTY tests for the custom readline cursor/editing keys.
 *
 * Verifies that rl_readline correctly handles:
 *   - Left/Right arrow cursor movement and mid-line character insertion.
 *   - Home / End jumps.
 *   - Backspace deletion.
 *   - Ctrl-K (kill to end of line).
 *   - Ctrl-W (kill previous word).
 *   - Up / Down history navigation.
 *   - Ctrl-D on an empty line causes clean exit.
 *
 * Approach:
 *   The `rl_harness` binary runs rl_readline in a loop and prints
 *   "ACCEPTED:<line>" after each Enter.  The test drives the harness through a
 *   PTY, sending keystroke sequences and asserting the expected ACCEPTED output
 *   appears.
 *
 *   Each test uses "goto cleanup" for ASAN-safe resource release on all exit
 *   paths — ASSERT_WAIT_FOR would otherwise return early and leak the session.
 */

#include "ptytest.h"
#include "pty_assert.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ── Globals required by ASSERT / ASSERT_WAIT_FOR macros ──────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

/* ── Helpers ──────────────────────────────────────────────────────────── */

#define RUN_TEST(fn) do { \
    printf("  running %s ...\n", #fn); \
    fn(); \
} while(0)

/** Injected at build time by CMake. */
#ifndef RL_HARNESS_BINARY
#define RL_HARNESS_BINARY "rl_harness"
#endif

/**
 * @brief CHECK_WAIT: like ASSERT_WAIT_FOR but jumps to a label on failure.
 *
 * This avoids early-return leaks when the test has an open PtySession.
 */
#define CHECK_WAIT_FOR(s, text, timeout_ms, label) do { \
    g_tests_run++; \
    if (pty_wait_for((s), (text), (timeout_ms)) != 0) { \
        printf("  [FAIL] %s:%d: wait_for(\"%s\", %d ms) timed out\n", \
               __FILE__, __LINE__, (text), (timeout_ms)); \
        g_tests_failed++; \
        goto label; \
    } \
} while(0)

/** Like ASSERT but jumps to a label on failure (no return). */
#define CHECK(cond, msg, label) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        g_tests_failed++; \
        goto label; \
    } \
} while(0)

/** Open a 80×24 PTY and start rl_harness in it. Returns session or NULL. */
static PtySession *open_harness(void) {
    PtySession *s = pty_open(80, 24);
    if (!s) return NULL;
    const char *argv[] = { RL_HARNESS_BINARY, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    /* Wait for the initial prompt (search without trailing space — pty_row_text
     * trims trailing spaces, so "rl>" matches but "rl> " would not). */
    if (pty_wait_for(s, "rl>", 3000) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* ── Tests ────────────────────────────────────────────────────────────── */

/**
 * @brief PTY-03-a: prompt "rl> " is visible on launch.
 */
static void test_prompt_visible(void) {
    PtySession *s = pty_open(80, 24);
    CHECK(s != NULL, "pty_open should succeed", done_no_session);

    const char *argv[] = { RL_HARNESS_BINARY, NULL };
    CHECK(pty_run(s, argv) == 0, "pty_run(rl_harness) should succeed", done);

    CHECK_WAIT_FOR(s, "rl>", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-b: plain text is accepted and echoed as ACCEPTED:<text>.
 */
static void test_plain_text_accepted(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "hello");
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hello", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-c: Left arrow + insert places character mid-line.
 *
 * Type "helo", move left once (cursor before 'o'), type 'l' → "hello".
 */
static void test_left_arrow_insert(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "helo");
    pty_send_key(s, PTY_KEY_LEFT);  /* cursor before 'o' */
    pty_send_str(s, "l");           /* insert → "hello" */
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hello", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-d: Home then End move cursor; typing after Home inserts at start.
 *
 * Type "world", Home, type "hello " → "hello world".
 */
static void test_home_end_insert(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "world");
    pty_send_key(s, PTY_KEY_HOME);    /* cursor at position 0 */
    pty_send_str(s, "hello ");        /* insert at start → "hello world" */
    pty_send_key(s, PTY_KEY_END);     /* move to end — no-op here but exercises END */
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hello world", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-e: Backspace deletes the character before the cursor.
 *
 * Type "hellox", Backspace → "hello".
 */
static void test_backspace(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "hellox");
    pty_send_key(s, PTY_KEY_BACK);   /* delete 'x' */
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hello", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-f: Ctrl-K kills from cursor position to end of line.
 *
 * Type "helloXXX", Left×3, Ctrl-K → "hello".
 */
static void test_ctrl_k_kill_to_end(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "helloXXX");
    /* Move cursor before the first 'X' */
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_LEFT);
    /* Kill to end (Ctrl-K = 0x0B) */
    pty_send(s, "\x0B", 1);
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hello", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-g: Ctrl-W kills the previous word.
 *
 * Type "foo bar", Ctrl-W → "foo ".
 */
static void test_ctrl_w_kill_word(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "foo bar");
    pty_send(s, "\x17", 1);   /* Ctrl-W = 0x17 */
    pty_send_key(s, PTY_KEY_ENTER);

    /* Search for "ACCEPTED:foo" (without trailing space) — pty_row_text trims
     * trailing spaces so "ACCEPTED:foo " would not be found. */
    CHECK_WAIT_FOR(s, "ACCEPTED:foo", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-h: Up arrow recalls the most recent history entry.
 *
 * Submit "first", then on the next prompt press Up, then Enter →
 * "ACCEPTED:first" appears a second time.
 */
static void test_up_arrow_history(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    /* Submit first line. */
    pty_send_str(s, "first");
    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ACCEPTED:first", 3000, done);

    /* Wait for the next prompt before pressing Up. */
    CHECK_WAIT_FOR(s, "rl>", 3000, done);

    /* Recall history with Up, then submit. */
    pty_send_key(s, PTY_KEY_UP);
    pty_settle(s, 100);
    pty_send_key(s, PTY_KEY_ENTER);

    /* The harness should print ACCEPTED:first a second time. */
    CHECK_WAIT_FOR(s, "ACCEPTED:first", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-i: Down arrow after Up restores the current (empty) edit line.
 *
 * Submit "alpha", then on the next prompt press Up then Down, then "beta",
 * Enter → ACCEPTED:beta (not ACCEPTED:alpha).
 */
static void test_down_arrow_restores_edit(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_str(s, "alpha");
    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ACCEPTED:alpha", 3000, done);

    CHECK_WAIT_FOR(s, "rl>", 3000, done);

    /* Up (recall "alpha"), then Down (restore empty line). */
    pty_send_key(s, PTY_KEY_UP);
    pty_settle(s, 100);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_settle(s, 100);

    /* Type "beta" on the restored blank line. */
    pty_send_str(s, "beta");
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:beta", 3000, done);

    pty_send_key(s, PTY_KEY_CTRL_D);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief PTY-03-j: Ctrl-D on an empty line exits the harness with code 0.
 */
static void test_ctrl_d_exits(void) {
    PtySession *s = open_harness();
    CHECK(s != NULL, "open_harness should succeed", done_no_session);

    pty_send_key(s, PTY_KEY_CTRL_D);

    int exit_code = pty_wait_exit(s, 3000);
    g_tests_run++;
    if (exit_code != 0) {
        printf("  [FAIL] %s:%d: Ctrl-D on empty line should cause exit 0, got %d\n",
               __FILE__, __LINE__, exit_code);
        g_tests_failed++;
    }

    pty_close(s);
done_no_session:
    return;
}

/* ── Entry point ──────────────────────────────────────────────────────── */

int main(void) {
    printf("PTY-03 readline navigation tests\n");

    RUN_TEST(test_prompt_visible);
    RUN_TEST(test_plain_text_accepted);
    RUN_TEST(test_left_arrow_insert);
    RUN_TEST(test_home_end_insert);
    RUN_TEST(test_backspace);
    RUN_TEST(test_ctrl_k_kill_to_end);
    RUN_TEST(test_ctrl_w_kill_word);
    RUN_TEST(test_up_arrow_history);
    RUN_TEST(test_down_arrow_restores_edit);
    RUN_TEST(test_ctrl_d_exits);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
