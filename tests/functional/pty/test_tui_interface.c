/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-11 */
/* Tg-tui against the TUI interface policy: header, palette, keys, help. */

/**
 * @file test_tui_interface.c
 * @brief TG-7 — tg-tui conforms to policies/tui-interface-model.md.
 *
 * Runs the real `tg-tui --tui` in an 80x24 pseudo-terminal against the
 * stub server and reads the screen back cell by cell:
 *
 *   - header bar: row 0, bold white on blue, the place on the left and the
 *     program's name on the right;
 *   - the status line (last row) carries the hints in yellow, on the
 *     terminal's own background;
 *   - `?` opens a bordered, titled help overlay that lists the keys, leaves
 *     the view visible, and is closed by Esc, q and Backspace without
 *     quitting; any other key changes nothing;
 *   - `q` and Backspace never quit; Esc on the top level quits (status 0);
 *   - `r` reloads and says so;
 *   - ctrl-c quits at once, also with the help open;
 *   - function keys change nothing.
 */

#include "ptytest.h"
#include "pty_assert.h"
#include "pty_tel_stub.h"
#include "pty_test_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn) do { \
    printf("  running %s ...\n", #fn); \
    fn(); \
} while (0)

#ifndef TG_TUI_BINARY
#define TG_TUI_BINARY "bin/tg-tui"
#endif
#ifndef TG_PTY_HOME_BASE
#error "TG_PTY_HOME_BASE must be set by CMake: tests keep their files under build/"
#endif

#define ROWS 24
#define COLS 80

/* A seeded HOME under the build tree, the stub server, the environment. */
static int setup_stub(PtyTelStub *stub) {
    char home[512], mid[560], cfg[600], ini[700];
    snprintf(home, sizeof(home), "%s-%d", TG_PTY_HOME_BASE, (int)getpid());
    snprintf(mid, sizeof(mid), "%s/.config", home);
    snprintf(cfg, sizeof(cfg), "%s/tg-cli", mid);
    snprintf(ini, sizeof(ini), "%s/config.ini", cfg);
    mkdir(home, 0700);
    mkdir(mid, 0700);
    mkdir(cfg, 0700);
    FILE *f = fopen(ini, "w");
    if (!f) return 0;
    fprintf(f, "api_id=12345\napi_hash=deadbeefcafebabef00dbaadfeedc0de\n");
    fputs(PTY_TEST_RSA_INI_LINE, f);
    fclose(f);
    setenv("HOME", home, 1);
    if (pty_tel_stub_start(stub) != 0) return 0;
    setenv("TG_CLI_DC_HOST", "127.0.0.1", 1);
    char port[16];
    snprintf(port, sizeof(port), "%d", stub->port);
    setenv("TG_CLI_DC_PORT", port, 1);
    setenv("TG_CLI_API_ID", "12345", 1);
    setenv("TG_CLI_API_HASH", "deadbeefcafebabef00dbaadfeedc0de", 1);
    return 1;
}

/* Starts the TUI and waits for the first paint. Returns NULL on failure. */
static PtySession *start_tui(PtyTelStub *stub) {
    if (!setup_stub(stub)) {
        PTY_ASSERT_FAIL_("setup_stub failed");
        return NULL;
    }
    PtySession *s = pty_open(COLS, ROWS);
    if (!s) {
        PTY_ASSERT_FAIL_("pty_open failed");
        pty_tel_stub_stop(stub);
        return NULL;
    }
    const char *argv[] = { TG_TUI_BINARY, "--tui", NULL };
    if (pty_run(s, argv) != 0 || pty_wait_for(s, "[dialogs]", 5000) != 0) {
        PTY_ASSERT_FAIL_("tg-tui --tui did not paint its status line");
        pty_close(s);
        pty_tel_stub_stop(stub);
        return NULL;
    }
    return s;
}

static void finish(PtySession *s, PtyTelStub *stub) {
    pty_close(s);
    pty_tel_stub_stop(stub);
}

/* ── The screen: header bar, hints ───────────────────────────────────── */

static void test_header_bar_appearance(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;

    char row[256];
    pty_row_text(s, 0, row, sizeof(row));
    g_tests_run++;
    if (!strstr(row, "Dialogs") || !strstr(row, "tg-tui")
        || strstr(row, "tg-tui") < strstr(row, "Dialogs")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "header should say where we are on the left "
                           "and the program on the right, got \"%s\"", row);
        finish(s, &stub);
        return;
    }
    for (int c = 0; c < COLS; c++) {
        g_tests_run++;
        if (pty_cell_fg(s, 0, c) != PTY_FG_WHITE
            || pty_cell_bg(s, 0, c) != PTY_BG_BLUE
            || !(pty_cell_attr(s, 0, c) & PTY_ATTR_BOLD)) {
            char d[256];
            PTY_ASSERT_FAIL_S_(s, NULL, "header cell (0,%d) is not bold white "
                               "on blue: %s", c,
                               pty_cell_describe(s, 0, c, d, sizeof(d)));
            finish(s, &stub);
            return;
        }
    }
    /* Everything below the header keeps the terminal's own background. */
    g_tests_run++;
    if (pty_cell_bg(s, 5, 40) != PTY_COLOR_DEFAULT) {
        PTY_ASSERT_FAIL_S_(s, NULL, "the body must keep the default background");
    }
    finish(s, &stub);
}

static void test_status_line_hints_are_yellow(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;

    g_tests_run++;
    if (!pty_row_contains(s, ROWS - 1, "[dialogs]")
        || !pty_row_contains(s, ROWS - 1, "? help")
        || !pty_row_contains(s, ROWS - 1, "Esc quit")) {
        char row[256];
        pty_row_text(s, ROWS - 1, row, sizeof(row));
        PTY_ASSERT_FAIL_S_(s, NULL, "the hints should name ?, r and Esc, "
                           "got \"%s\"", row);
        finish(s, &stub);
        return;
    }
    g_tests_run++;
    if (pty_cell_fg(s, ROWS - 1, 0) != PTY_FG_YELLOW
        || pty_cell_bg(s, ROWS - 1, 0) != PTY_COLOR_DEFAULT
        || (pty_cell_attr(s, ROWS - 1, 0) & PTY_ATTR_REVERSE)) {
        char d[256];
        PTY_ASSERT_FAIL_S_(s, NULL, "hints should be yellow on the default "
                           "background, not reverse: %s",
                           pty_cell_describe(s, ROWS - 1, 0, d, sizeof(d)));
    }
    finish(s, &stub);
}

static void test_empty_list_says_so(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;
    g_tests_run++;
    if (!pty_screen_contains(s, "no dialogs - r reloads")
        && !pty_screen_contains(s, "could not read dialogs")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "an empty or unreadable list must say so");
    }
    finish(s, &stub);
}

/* ── Help overlay ────────────────────────────────────────────────────── */

static void test_help_overlay_opens_and_closes(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;

    pty_send_str(s, "?");
    g_tests_run++;
    if (pty_wait_for(s, "Dialog list shortcuts", 3000) != 0) {
        PTY_ASSERT_FAIL_S_(s, NULL, "? should open the help of the view");
        finish(s, &stub);
        return;
    }
    g_tests_run++;
    if (!pty_screen_contains(s, "ctrl-f") || !pty_screen_contains(s, "reload")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "help should list ctrl-f and r");
        finish(s, &stub);
        return;
    }
    /* The view stays visible around the box: the header is still there. */
    g_tests_run++;
    if (!pty_row_contains(s, 0, "tg-tui")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "the header must stay visible behind the help");
        finish(s, &stub);
        return;
    }
    /* The frame is in the accent colour (bold cyan) somewhere on screen. */
    int accent = 0;
    for (int r = 1; r < ROWS - 1 && !accent; r++)
        for (int c = 0; c < COLS && !accent; c++)
            if (pty_cell_fg(s, r, c) == PTY_FG_CYAN
                && (pty_cell_attr(s, r, c) & PTY_ATTR_BOLD)) accent = 1;
    g_tests_run++;
    if (!accent) {
        PTY_ASSERT_FAIL_S_(s, NULL, "the help frame should be bold cyan");
        finish(s, &stub);
        return;
    }

    /* Another key changes nothing. */
    pty_send_str(s, "x");
    pty_settle(s, 200);
    g_tests_run++;
    if (!pty_screen_contains(s, "Dialog list shortcuts")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "an unbound key must not close the help");
        finish(s, &stub);
        return;
    }

    /* Esc, q and Backspace close it and never quit. */
    const int closers[] = { PTY_KEY_ESC, 'q', PTY_KEY_BACK };
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            pty_send_str(s, "?");
            pty_wait_for(s, "Dialog list shortcuts", 3000);
        }
        if (closers[i] == 'q') pty_send_str(s, "q");
        else pty_send_key(s, (PtyKey)closers[i]);
        pty_settle(s, 300);
        g_tests_run++;
        if (pty_screen_contains(s, "Dialog list shortcuts")) {
            PTY_ASSERT_FAIL_S_(s, NULL, "closer %d should close the help", i);
            finish(s, &stub);
            return;
        }
        g_tests_run++;
        if (!pty_row_contains(s, ROWS - 1, "[dialogs]")) {
            PTY_ASSERT_FAIL_S_(s, NULL, "closing the help must not quit "
                               "(closer %d)", i);
            finish(s, &stub);
            return;
        }
    }
    finish(s, &stub);
}

/* ── Keys ────────────────────────────────────────────────────────────── */

static void test_q_and_backspace_never_quit_esc_does(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;

    pty_send_str(s, "q");
    pty_send_key(s, PTY_KEY_BACK);
    pty_send_key(s, PTY_KEY_F1);
    pty_send_key(s, PTY_KEY_F10);
    pty_settle(s, 300);
    g_tests_run++;
    if (!pty_row_contains(s, ROWS - 1, "[dialogs]")
        || pty_screen_contains(s, "shortcuts")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "q, Backspace and the function keys must "
                           "change nothing on the top level");
        finish(s, &stub);
        return;
    }
    pty_send_key(s, PTY_KEY_ESC);
    int code = pty_wait_exit(s, 5000);
    g_tests_run++;
    if (code != 0) {
        PTY_ASSERT_FAIL_S_(s, NULL, "Esc on the top level should quit with "
                           "status 0, got %d", code);
    }
    finish(s, &stub);
}

static void test_ctrl_c_quits_at_once_even_in_help(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;
    pty_send_str(s, "?");
    pty_wait_for(s, "Dialog list shortcuts", 3000);
    pty_send_key(s, PTY_KEY_CTRL_C);
    int code = pty_wait_exit(s, 5000);
    g_tests_run++;
    if (code != 0 && code != 130) {
        PTY_ASSERT_FAIL_S_(s, NULL, "ctrl-c should quit at once, exit status %d", code);
    }
    finish(s, &stub);
}

static void test_r_reloads_and_says_so(void) {
    PtyTelStub stub;
    g_tests_run++;
    PtySession *s = start_tui(&stub);
    if (!s) return;
    pty_send_str(s, "r");
    g_tests_run++;
    /* The status line says it worked, in bold green, instead of the hints. */
    if (pty_wait_for(s, "reloaded", 3000) != 0
        || pty_row_contains(s, ROWS - 1, "[dialogs]")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "r should reload and report \"reloaded\" "
                           "on the status line, replacing the hints");
        finish(s, &stub);
        return;
    }
    g_tests_run++;
    if (pty_cell_fg(s, ROWS - 1, 0) != PTY_FG_GREEN
        || !(pty_cell_attr(s, ROWS - 1, 0) & PTY_ATTR_BOLD)) {
        PTY_ASSERT_FAIL_S_(s, NULL, "a worked reload is bold green");
        finish(s, &stub);
        return;
    }
    /* The next key clears the message and the hints come back. */
    pty_send_str(s, "x");
    pty_settle(s, 300);
    g_tests_run++;
    if (!pty_row_contains(s, ROWS - 1, "[dialogs]")) {
        PTY_ASSERT_FAIL_S_(s, NULL, "the next keypress should bring the hints back");
    }
    finish(s, &stub);
}

int main(void) {
    printf("TG-7 TUI interface policy tests\n");

    RUN_TEST(test_header_bar_appearance);
    RUN_TEST(test_status_line_hints_are_yellow);
    RUN_TEST(test_empty_list_says_so);
    RUN_TEST(test_help_overlay_opens_and_closes);
    RUN_TEST(test_q_and_backspace_never_quit_esc_does);
    RUN_TEST(test_ctrl_c_quits_at_once_even_in_help);
    RUN_TEST(test_r_reloads_and_says_so);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
