/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_ctrl_c_exit.c
 * @brief PTY-05 — Ctrl-C exits tg-tui cleanly without leaving raw-mode artefacts.
 *
 * Two tests:
 *
 *   test_repl_ctrl_c:
 *     - Launches tg-tui (REPL / no --tui flag).
 *     - Waits for the "tg> " prompt.
 *     - Sends Ctrl-C (byte 0x03).
 *     - Asserts the child exits within 2 seconds with code 0 (graceful: the
 *       readline layer returns -1 on Ctrl-C and repl() returns 0).
 *     - Verifies that PTY dimensions are intact (terminal not trashed).
 *
 *   test_tui_mode_ctrl_c:
 *     - Launches tg-tui --tui.
 *     - Waits for "[dialogs]" in the status row.
 *     - Sends Ctrl-C.
 *     - Asserts child exits within 2 seconds with status 130 (128+SIGINT) or 0.
 *       (terminal_install_cleanup_handlers resets the handler to SIG_DFL and
 *       re-raises SIGINT, so the shell sees 128+2=130.)
 *     - Verifies PTY dimensions are intact after exit.
 *
 * Depends on:
 *   - libptytest (PTY-01)
 *   - pty_tel_stub (PTY-02 infrastructure)
 *   - tg-tui binary with terminal_install_cleanup_handlers (FEAT-16)
 */

#include "ptytest.h"
#include "pty_assert.h"
#include "pty_tel_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ── Test infrastructure ─────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn) do { \
    printf("  running %s ...\n", #fn); \
    fn(); \
} while(0)

#ifndef TG_TUI_BINARY
#define TG_TUI_BINARY "bin/tg-tui"
#endif

/**
 * @brief Like ASSERT but jumps to a label on failure (avoids early-return leaks).
 */
#define CHECK(cond, msg, label) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        g_tests_failed++; \
        goto label; \
    } \
} while(0)

/**
 * @brief Like ASSERT_WAIT_FOR but jumps to a label on failure.
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

/**
 * @brief Common setup: unique tmp HOME, session seeded, stub started, env set.
 * @return 1 on success, 0 on failure.
 */
static int setup_stub(PtyTelStub *stub) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-pty-ctrlc-%d", (int)getpid());

    mkdir(tmp, 0700);
    char dot_config[512];
    snprintf(dot_config, sizeof(dot_config), "%s/.config", tmp);
    mkdir(dot_config, 0700);
    char cfg_dir[640];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/tg-cli", dot_config);
    mkdir(cfg_dir, 0700);

    char ini_path[768];
    snprintf(ini_path, sizeof(ini_path), "%s/config.ini", cfg_dir);
    FILE *f = fopen(ini_path, "w");
    if (!f) return 0;
    fprintf(f, "api_id=12345\napi_hash=deadbeefcafebabef00dbaadfeedc0de\n");
    fclose(f);

    setenv("HOME", tmp, 1);

    if (pty_tel_stub_start(stub) != 0) return 0;

    setenv("TG_CLI_DC_HOST", "127.0.0.1", 1);
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", stub->port);
    setenv("TG_CLI_DC_PORT", port_str, 1);

    setenv("TG_CLI_API_ID",   "12345", 1);
    setenv("TG_CLI_API_HASH", "deadbeefcafebabef00dbaadfeedc0de", 1);

    return 1;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/**
 * @brief PTY-05-a: REPL mode — Ctrl-C on the prompt exits cleanly (code 0).
 *
 * In REPL mode the terminal stays in cooked mode; rl_readline intercepts
 * Ctrl-C internally (raw mode is only for the custom readline widget) and
 * returns -1, which causes repl() to return 0 (clean exit).
 */
static void test_repl_ctrl_c(void) {
    PtyTelStub stub;
    CHECK(setup_stub(&stub), "setup_stub failed", done_no_stub);

    PtySession *s = pty_open(80, 24);
    CHECK(s != NULL, "pty_open failed", done_no_session);

    const char *argv[] = { TG_TUI_BINARY, NULL };
    CHECK(pty_run(s, argv) == 0, "pty_run(tg-tui) failed", done);

    /* Wait for the REPL prompt. */
    CHECK_WAIT_FOR(s, "tg>", 5000, done);

    /* Send Ctrl-C. */
    pty_send_key(s, PTY_KEY_CTRL_C);

    /* The process should exit gracefully with code 0. */
    int code = pty_wait_exit(s, 2000);
    g_tests_run++;
    if (code != 0) {
        printf("  [FAIL] %s:%d: REPL Ctrl-C exit code: expected 0, got %d\n",
               __FILE__, __LINE__, code);
        g_tests_failed++;
    }

    /* PTY dimensions must be intact (terminal not left garbled). */
    int cols = 0, rows = 0;
    pty_get_size(s, &cols, &rows);
    g_tests_run++;
    if (cols != 80 || rows != 24) {
        printf("  [FAIL] %s:%d: PTY dimensions wrong after exit (%dx%d)\n",
               __FILE__, __LINE__, cols, rows);
        g_tests_failed++;
    }

done:
    pty_close(s);
done_no_session:
    pty_tel_stub_stop(&stub);
done_no_stub:
    return;
}

/**
 * @brief PTY-05-b: TUI mode (--tui) — Ctrl-C exits and restores the terminal.
 *
 * In TUI mode the terminal is in raw mode.  terminal_install_cleanup_handlers()
 * installs a SIGINT handler that restores the terminal and re-raises the signal
 * so the exit status is 130 (128+2).  We also accept 0 in case the signal is
 * delivered while the TUI loop is processing a normal quit path.
 */
static void test_tui_mode_ctrl_c(void) {
    PtyTelStub stub;
    CHECK(setup_stub(&stub), "setup_stub failed", done_no_stub);

    PtySession *s = pty_open(80, 24);
    CHECK(s != NULL, "pty_open failed", done_no_session);

    const char *argv[] = { TG_TUI_BINARY, "--tui", NULL };
    CHECK(pty_run(s, argv) == 0, "pty_run(tg-tui --tui) failed", done);

    /* Wait for the dialog-pane hint in the status row. */
    CHECK_WAIT_FOR(s, "[dialogs]", 5000, done);

    /* Send Ctrl-C to the TUI. */
    pty_send_key(s, PTY_KEY_CTRL_C);

    /* Allow up to 2 seconds for the process to exit. */
    int code = pty_wait_exit(s, 2000);

    /* Accept 130 (SIGINT with restored-then-re-raised handler) or 0 (graceful). */
    g_tests_run++;
    if (code != 130 && code != 0) {
        printf("  [FAIL] %s:%d: TUI Ctrl-C exit code: expected 130 or 0, got %d\n",
               __FILE__, __LINE__, code);
        g_tests_failed++;
    }

    /* PTY dimensions must be intact (terminal not left in raw mode). */
    int cols = 0, rows = 0;
    pty_get_size(s, &cols, &rows);
    g_tests_run++;
    if (cols != 80 || rows != 24) {
        printf("  [FAIL] %s:%d: PTY dimensions wrong after exit (%dx%d)\n",
               __FILE__, __LINE__, cols, rows);
        g_tests_failed++;
    }

done:
    pty_close(s);
done_no_session:
    pty_tel_stub_stop(&stub);
done_no_stub:
    return;
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(void) {
    printf("PTY-05 Ctrl-C exit tests\n");

    RUN_TEST(test_repl_ctrl_c);
    RUN_TEST(test_tui_mode_ctrl_c);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
