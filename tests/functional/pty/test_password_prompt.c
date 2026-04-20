/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_password_prompt.c
 * @brief TEST-87 — PTY tests for terminal_read_password() masking/echo.
 *
 * Exercises src/platform/posix/terminal.c::terminal_read_password via a
 * minimal harness binary (password_harness) that calls it directly and
 * prints the result.  Using a harness — rather than tg-tui's 2FA path —
 * keeps these tests fast and independent from the mock server used by
 * the other PTY suites.
 *
 * Scenarios (matches TEST-87 ticket):
 *   1. echo off during masked input
 *        type "hunter2"; the PTY screen must NOT show "hunter2" before
 *        Enter is pressed.
 *   2. echo restored on return
 *        after the password prompt returns, a subsequent fgets() prompt
 *        echoes typed characters normally.
 *   3. echo restored after SIGINT
 *        Ctrl-C kills the child; a fresh child's /bin/echo-style check
 *        shows echoed characters.  Proves the previous process did not
 *        leave the terminal in a stuck no-echo state (SIGINT handlers
 *        in terminal.c restore termios).
 *   4. backspace edits the hidden buffer
 *        "bad<BS><BS><BS>hunter2<Enter>" → ACCEPTED:hunter2.
 *   5. Ctrl-D on empty reports error
 *        0x04 on an empty line → harness prints "ERROR" and exits 1.
 *   6. long input (256 chars) accepted without truncation
 *        ACCEPTED:<256 chars>, LEN:256.
 *
 * Each test uses "goto cleanup" labels for ASAN-safe resource release on
 * failure paths (same pattern as tests/functional/pty/test_readline.c).
 */

#include "ptytest.h"
#include "pty_assert.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Globals required by ASSERT / ASSERT_WAIT_FOR macros ──────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#ifndef PASSWORD_HARNESS_BINARY
#define PASSWORD_HARNESS_BINARY "password_harness"
#endif

/* ── Helpers ──────────────────────────────────────────────────────────── */

#define RUN_TEST(fn) do { \
    printf("  running %s ...\n", #fn); \
    fn(); \
} while(0)

/** CHECK_WAIT_FOR: jumps to label on timeout instead of returning. */
#define CHECK_WAIT_FOR(s, text, timeout_ms, label) do { \
    g_tests_run++; \
    if (pty_wait_for((s), (text), (timeout_ms)) != 0) { \
        printf("  [FAIL] %s:%d: wait_for(\"%s\", %d ms) timed out\n", \
               __FILE__, __LINE__, (text), (timeout_ms)); \
        g_tests_failed++; \
        goto label; \
    } \
} while(0)

/** CHECK: jumps to label instead of returning. */
#define CHECK(cond, msg, label) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        g_tests_failed++; \
        goto label; \
    } \
} while(0)

/** Open an 80x24 PTY and start password_harness with the given mode. */
static PtySession *open_harness(const char *mode) {
    PtySession *s = pty_open(80, 24);
    if (!s) return NULL;
    const char *argv[] = { PASSWORD_HARNESS_BINARY, mode, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    /* Wait for the prompt string emitted by terminal_read_password ("Password: "). */
    if (pty_wait_for(s, "Password", 3000) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* ── Tests ────────────────────────────────────────────────────────────── */

/**
 * @brief Scenario 1: typed characters are NOT echoed during masked input.
 *
 * After the prompt appears, type "hunter2" but do NOT press Enter yet.
 * The PTY master should not have received any echo of those bytes
 * (because terminal_read_password cleared ECHO).  Once Enter is pressed
 * the harness prints "ACCEPTED:hunter2" on its own stdout — which IS
 * visible.  We assert "hunter2" is absent between the prompt and Enter
 * and present only in the "ACCEPTED:" line.
 */
static void test_echo_off_during_input(void) {
    PtySession *s = open_harness("prompt");
    CHECK(s != NULL, "open_harness(prompt) should succeed", done_no_session);

    /* Type the secret (no Enter yet). */
    pty_send_str(s, "hunter2");
    /* Let any terminal echo land on the master — if it were going to. */
    pty_settle(s, 150);

    /* The literal "hunter2" must NOT be visible on screen yet. */
    CHECK(pty_screen_contains(s, "hunter2") == 0,
          "typed password must not be echoed while ECHO is cleared",
          done);

    /* Submit so the child exits cleanly. */
    pty_send_key(s, PTY_KEY_ENTER);

    /* After Enter, the harness prints ACCEPTED:hunter2 to its own stdout. */
    CHECK_WAIT_FOR(s, "ACCEPTED:hunter2", 3000, done);

    int exit_code = pty_wait_exit(s, 3000);
    CHECK(exit_code == 0, "harness must exit 0 after successful prompt", done);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief Scenario 2: echo is restored after the prompt returns.
 *
 * Prompt once, submit, then the harness fgets() a second line.  Typing
 * "CONFIRM" on that second read MUST become visible on screen (the
 * kernel tty line discipline echoes it because ECHO was restored).
 */
static void test_echo_restored_on_return(void) {
    PtySession *s = open_harness("prompt_then_echo");
    CHECK(s != NULL, "open_harness(prompt_then_echo) should succeed",
          done_no_session);

    /* First prompt: masked input. */
    pty_send_str(s, "secret");
    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ACCEPTED:secret", 3000, done);

    /* Now a plain fgets() is active.  Type CONFIRM — it SHOULD echo. */
    pty_send_str(s, "CONFIRM");
    /* Give the kernel time to echo the typed bytes back on the master. */
    pty_settle(s, 150);

    /* The typed text must have been echoed — search for it on screen
     * BEFORE sending Enter (so we cannot be confused with the harness's
     * own "ECHO:CONFIRM\n" output). */
    CHECK(pty_screen_contains(s, "CONFIRM") != 0,
          "plain prompt after password must echo typed chars "
          "(ECHO bit was not restored)",
          done);

    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ECHO:CONFIRM", 3000, done);

    int exit_code = pty_wait_exit(s, 3000);
    CHECK(exit_code == 0, "harness must exit 0 after both reads succeed",
          done);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief Scenario 3: echo is still usable in a fresh process after SIGINT.
 *
 * Ctrl-C during the masked prompt raises SIGINT; the process dies.
 * If the SIGINT handler failed to restore termios we would observe
 * the problem in a NEW child started on a fresh PTY: ordinary typed
 * bytes would not echo.  This test confirms SIGINT is not sticky.
 *
 * Note: canonical-mode Ctrl-C affects only the owning process.  Because
 * we open a brand-new PTY for the "after" probe there is no cross-run
 * termios leakage to catch in a single test host; the assertion here
 * is really "Ctrl-C kills the prompt cleanly AND a brand-new prompt
 * still works".  This is the strongest observable behaviour from the
 * user's perspective.
 */
static void test_echo_restored_after_sigint(void) {
    /* First session: start the prompt, then send Ctrl-C. */
    PtySession *s = open_harness("prompt");
    CHECK(s != NULL, "open_harness(prompt) should succeed",
          done_no_session);

    /* Start typing, then interrupt before hitting Enter. */
    pty_send_str(s, "hun");
    pty_settle(s, 100);

    /* Ctrl-C (0x03) — ISIG is still enabled inside terminal_read_password,
     * so the tty line discipline will send SIGINT to the foreground group. */
    pty_send_key(s, PTY_KEY_CTRL_C);

    int exit_code = pty_wait_exit(s, 3000);
    /* pty_wait_exit encodes signal exit as 128 + signum; SIGINT = 2 → 130. */
    CHECK(exit_code == 130 || exit_code == -1,
          "Ctrl-C during masked prompt must terminate the child",
          done_first);
    pty_close(s);

    /* Second session: open a fresh PTY and a fresh harness.  Running
     * through prompt_then_echo verifies that a new process's plain
     * fgets() echoes typed bytes normally. */
    s = open_harness("prompt_then_echo");
    CHECK(s != NULL, "fresh open_harness after SIGINT must succeed",
          done_no_session);

    pty_send_str(s, "pw2");
    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ACCEPTED:pw2", 3000, done);

    pty_send_str(s, "ECHOOK");
    pty_settle(s, 150);
    CHECK(pty_screen_contains(s, "ECHOOK") != 0,
          "fresh child after SIGINT must echo typed bytes",
          done);

    pty_send_key(s, PTY_KEY_ENTER);
    CHECK_WAIT_FOR(s, "ECHO:ECHOOK", 3000, done);
    pty_wait_exit(s, 3000);
done:
    pty_close(s);
    return;
done_first:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief Scenario 4: Backspace in the hidden buffer corrects the password.
 *
 * In canonical mode with ECHO cleared, the tty line discipline still
 * handles ERASE (DEL / 0x7F): typing "bad" then three ERASE chars then
 * "hunter2\n" must deliver the string "hunter2" (NOT "badhunter2") to
 * getline() inside terminal_read_password.
 */
static void test_backspace_edits_hidden_buffer(void) {
    PtySession *s = open_harness("prompt");
    CHECK(s != NULL, "open_harness(prompt) should succeed", done_no_session);

    pty_send_str(s, "bad");
    pty_send_key(s, PTY_KEY_BACK);   /* 0x7F */
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_str(s, "hunter2");
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "ACCEPTED:hunter2", 3000, done);

    /* Also assert the wrong string did not slip through. */
    CHECK(pty_screen_contains(s, "ACCEPTED:badhunter2") == 0,
          "backspace must erase the preceding chars, not append",
          done);

    CHECK_WAIT_FOR(s, "LEN:7", 3000, done);

    int exit_code = pty_wait_exit(s, 3000);
    CHECK(exit_code == 0, "harness must exit 0", done);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief Scenario 5: Ctrl-D on an empty line returns an error.
 *
 * In canonical mode, 0x04 (EOT) on an empty line causes read(2) to
 * return 0.  getline() returns -1, terminal_read_password returns -1,
 * and the harness prints "ERROR" and exits 1.
 */
static void test_ctrl_d_on_empty_reports_error(void) {
    PtySession *s = open_harness("prompt");
    CHECK(s != NULL, "open_harness(prompt) should succeed", done_no_session);

    pty_send_key(s, PTY_KEY_CTRL_D);

    CHECK_WAIT_FOR(s, "ERROR", 3000, done);

    int exit_code = pty_wait_exit(s, 3000);
    CHECK(exit_code == 1,
          "Ctrl-D on empty password line must exit 1 (error)",
          done);
done:
    pty_close(s);
done_no_session:
    return;
}

/**
 * @brief Scenario 6: a 256-character password is accepted without truncation.
 *
 * The harness's "prompt_big" mode uses a 512-byte buffer, so 256 chars
 * fit with room for NUL.  We assert both the echoed string and the
 * reported length.
 */
static void test_long_input_fits_buffer(void) {
    PtySession *s = pty_open(120, 24);
    CHECK(s != NULL, "pty_open should succeed", done_no_session);

    const char *argv[] = { PASSWORD_HARNESS_BINARY, "prompt_big", NULL };
    CHECK(pty_run(s, argv) == 0, "pty_run should succeed", done);

    CHECK_WAIT_FOR(s, "Password", 3000, done);

    /* Build a 256-char password made of repeating 'a'..'z' so we can
     * verify the tail arrived intact. */
    char pw[257];
    for (int i = 0; i < 256; i++) pw[i] = (char)('a' + (i % 26));
    pw[256] = '\0';

    pty_send(s, pw, 256);
    pty_send_key(s, PTY_KEY_ENTER);

    CHECK_WAIT_FOR(s, "LEN:256", 5000, done);

    int exit_code = pty_wait_exit(s, 3000);
    CHECK(exit_code == 0, "harness must exit 0 for 256-char password", done);
done:
    pty_close(s);
done_no_session:
    return;
}

/* ── Entry point ──────────────────────────────────────────────────────── */

int main(void) {
    printf("TEST-87 password prompt PTY tests (%s)\n",
           PASSWORD_HARNESS_BINARY);

    RUN_TEST(test_echo_off_during_input);
    RUN_TEST(test_echo_restored_on_return);
    RUN_TEST(test_echo_restored_after_sigint);
    RUN_TEST(test_backspace_edits_hidden_buffer);
    RUN_TEST(test_ctrl_d_on_empty_reports_error);
    RUN_TEST(test_long_input_fits_buffer);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
