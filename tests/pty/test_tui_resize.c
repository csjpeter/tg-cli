/**
 * @file test_tui_resize.c
 * @brief PTY-04 — tg-tui SIGWINCH resize repaints screen.
 *
 * Verifies that tg-tui:
 *   a) Starts at 80×24, shows "[dialogs]" hint.
 *   b) After PTY resize to 60×20 + SIGWINCH, repaints and still shows
 *      "[dialogs]".
 *   c) After resize back to 80×24, repaints again.
 *   d) Exits cleanly (exit code 0) when 'q' is pressed.
 *
 * Approach:
 *   - Same stub-server setup as PTY-02 (pty_tel_stub seeds session.bin).
 *   - pty_resize() issues TIOCSWINSZ on the master fd and delivers SIGWINCH
 *     to the child, then pty_settle() waits for the repaint to settle.
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

/** Absolute path to the binary under test (injected by CMake). */
#ifndef TG_TUI_BINARY
#define TG_TUI_BINARY "bin/tg-tui"
#endif

/** Common setup: tmp HOME, session seeded, stub running, env vars set. */
static int setup_stub(PtyTelStub *stub) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-pty-resize-%d", (int)getpid());

    char cfg_dir[512];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", tmp);
    mkdir(tmp, 0700);
    {
        char mid[512];
        snprintf(mid, sizeof(mid), "%s/.config", tmp);
        mkdir(mid, 0700);
    }
    mkdir(cfg_dir, 0700);

    char ini_path[640];
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
 * @brief PTY-04-a: resize 80×24 → 60×20 → 80×24; screen repaints each time.
 *
 * After each resize the test asserts:
 *   - pty_get_size() reports the new dimensions.
 *   - The screen still contains "[dialogs]" (TUI repainted without artefacts).
 */
static void test_tui_resize_sigwinch(void) {
    PtyTelStub stub;
    g_tests_run++;
    if (!setup_stub(&stub)) {
        printf("  [FAIL] %s:%d: setup_stub failed\n", __FILE__, __LINE__);
        g_tests_failed++;
        return;
    }

    PtySession *s = pty_open(80, 24);
    g_tests_run++;
    if (!s) {
        printf("  [FAIL] %s:%d: pty_open failed\n", __FILE__, __LINE__);
        g_tests_failed++;
        pty_tel_stub_stop(&stub);
        return;
    }

    const char *argv[] = { TG_TUI_BINARY, "--tui", NULL };
    g_tests_run++;
    if (pty_run(s, argv) != 0) {
        printf("  [FAIL] %s:%d: pty_run failed\n", __FILE__, __LINE__);
        g_tests_failed++;
        pty_close(s);
        pty_tel_stub_stop(&stub);
        return;
    }

    /* Wait for the TUI to paint the initial layout. */
    ASSERT_WAIT_FOR(s, "[dialogs]", 5000);

    /* ── Resize to 60×20 ──────────────────────────────────────────────── */
    g_tests_run++;
    if (pty_resize(s, 60, 20) != 0) {
        printf("  [FAIL] %s:%d: pty_resize(60,20) failed\n",
               __FILE__, __LINE__);
        g_tests_failed++;
        pty_send_str(s, "q");
        pty_wait_exit(s, 3000);
        pty_close(s);
        pty_tel_stub_stop(&stub);
        return;
    }

    /* Wait for the repaint to settle. */
    pty_settle(s, 200);

    /* Assert new PTY dimensions. */
    {
        int cols = 0, rows = 0;
        pty_get_size(s, &cols, &rows);
        g_tests_run++;
        if (cols != 60 || rows != 20) {
            printf("  [FAIL] %s:%d: expected 60×20, got %d×%d\n",
                   __FILE__, __LINE__, cols, rows);
            g_tests_failed++;
        }
    }

    /* Screen must still contain "[dialogs]" after repaint. */
    ASSERT_WAIT_FOR(s, "[dialogs]", 3000);

    /* ── Resize back to 80×24 ─────────────────────────────────────────── */
    g_tests_run++;
    if (pty_resize(s, 80, 24) != 0) {
        printf("  [FAIL] %s:%d: pty_resize(80,24) failed\n",
               __FILE__, __LINE__);
        g_tests_failed++;
        pty_send_str(s, "q");
        pty_wait_exit(s, 3000);
        pty_close(s);
        pty_tel_stub_stop(&stub);
        return;
    }

    pty_settle(s, 200);

    {
        int cols = 0, rows = 0;
        pty_get_size(s, &cols, &rows);
        g_tests_run++;
        if (cols != 80 || rows != 24) {
            printf("  [FAIL] %s:%d: expected 80×24 after second resize, got %d×%d\n",
                   __FILE__, __LINE__, cols, rows);
            g_tests_failed++;
        }
    }

    ASSERT_WAIT_FOR(s, "[dialogs]", 3000);

    /* ── Clean exit ───────────────────────────────────────────────────── */
    pty_send_str(s, "q");

    int exit_code = pty_wait_exit(s, 5000);
    g_tests_run++;
    if (exit_code != 0) {
        printf("  [FAIL] %s:%d: expected exit code 0, got %d\n",
               __FILE__, __LINE__, exit_code);
        g_tests_failed++;
    }

    pty_close(s);
    pty_tel_stub_stop(&stub);
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(void) {
    printf("PTY-04 TUI SIGWINCH resize tests\n");

    RUN_TEST(test_tui_resize_sigwinch);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
