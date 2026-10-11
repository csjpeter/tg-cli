/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-11 */
/* Every element of tg-tui, every state, cell by cell. */

/**
 * @file test_tui_elements.c
 * @brief TG-16 — one PTY test per element of tg-tui, colours included.
 *
 * Runs the real `tg-tui --tui` in a pseudo-terminal against the stub server
 * (tests/functional/pty/pty_tel_stub.c) with its dialog and history
 * fixtures, and reads the screen back through libptytest:
 *
 *   - the header bar: the place and the position (`N of M`) on the left, the
 *     program name at the right edge, bold white on blue over the width;
 *   - the dialog list: rows, the unread marker and its bold, the selected row
 *     (bold reverse, the whole pane width), the unfocused selection
 *     (underlined), scrolling, every movement key;
 *   - the history view: placeholder, rows with the direction arrow, the
 *     selection, every movement key, focus changes;
 *   - the status row: both hint sets (yellow), the progress/ok/error
 *     messages and their colours;
 *   - the help overlay of each view;
 *   - the empty and failed states of both lists (dim / red);
 *   - resize and ctrl-c.
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
#define LEFT 28            /* the dialog pane's width at 80 columns        */
#define STATUS_ROW (ROWS - 1)
#define MID "\xc2\xb7"     /* U+00B7, the header's separator               */

/* ── Set-up ──────────────────────────────────────────────────────────── */

static int setup_stub(PtyTelStub *stub, unsigned fixture) {
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
    if (pty_tel_stub_start_fixture(stub, fixture) != 0) return 0;
    setenv("TG_CLI_DC_HOST", "127.0.0.1", 1);
    char port[16];
    snprintf(port, sizeof(port), "%d", stub->port);
    setenv("TG_CLI_DC_PORT", port, 1);
    setenv("TG_CLI_API_ID", "12345", 1);
    setenv("TG_CLI_API_HASH", "deadbeefcafebabef00dbaadfeedc0de", 1);
    return 1;
}

/* Starts the TUI with `fixture` and waits for the first paint of the list. */
static PtySession *start_tui(PtyTelStub *stub, unsigned fixture,
                             const char *first_row_text) {
    if (!setup_stub(stub, fixture)) {
        PTY_ASSERT_FAIL_("setup_stub failed");
        return NULL;
    }
    PtySession *s = pty_open(COLS, ROWS);
    if (!s) {
        PTY_ASSERT_FAIL_("pty_open failed");
        pty_tel_stub_stop(stub);
        return NULL;
    }
    /* A failed first load shows its error where the hints would be. */
    const char *ready = (fixture & PTY_STUB_DIALOGS_FAIL)
                      ? "dialogs: load failed" : "[dialogs]";
    const char *argv[] = { TG_TUI_BINARY, "--tui", NULL };
    if (pty_run(s, argv) != 0 || pty_wait_for(s, ready, 5000) != 0
        || (first_row_text && pty_wait_for(s, first_row_text, 5000) != 0)) {
        PTY_ASSERT_FAIL_S_(s, NULL, "tg-tui --tui did not paint the list");
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

/* ── Checks (each returns 1 when it held, and reports when it did not) ── */

/* The header's left side carries `text`; waits for it. */
static int header_is(PtySession *s, const char *text) {
    g_tests_run++;
    if (pty_wait_for(s, text, 3000) != 0 || !pty_row_contains(s, 0, text)) {
        char row[256];
        pty_row_text(s, 0, row, sizeof(row));
        PTY_ASSERT_FAIL_S_(s, NULL, "header should say \"%s\", got \"%s\"",
                           text, row);
        return 0;
    }
    return 1;
}

static int status_has(PtySession *s, const char *text) {
    g_tests_run++;
    if (pty_wait_for(s, text, 3000) != 0
        || !pty_row_contains(s, STATUS_ROW, text)) {
        char row[256];
        pty_row_text(s, STATUS_ROW, row, sizeof(row));
        PTY_ASSERT_FAIL_S_(s, NULL, "status row should say \"%s\", got \"%s\"",
                           text, row);
        return 0;
    }
    return 1;
}

static int row_has(PtySession *s, int row, const char *text) {
    g_tests_run++;
    if (!pty_row_contains(s, row, text)) {
        char buf[256];
        pty_row_text(s, row, buf, sizeof(buf));
        PTY_ASSERT_FAIL_S_(s, NULL, "row %d should contain \"%s\", got \"%s\"",
                           row, text, buf);
        return 0;
    }
    return 1;
}

static int screen_has(PtySession *s, const char *text, int want) {
    g_tests_run++;
    if (!!pty_screen_contains(s, text) != want) {
        PTY_ASSERT_FAIL_S_(s, NULL, "screen should %scontain \"%s\"",
                           want ? "" : "not ", text);
        return 0;
    }
    return 1;
}

/* Every cell of row r, columns [c0, c1): fg, bg; `attrs` all set, `no_attrs`
 * all clear. */
static int row_style(PtySession *s, int r, int c0, int c1, int fg, int bg,
                     int attrs, int no_attrs) {
    for (int c = c0; c < c1; c++) {
        g_tests_run++;
        int a = pty_cell_attr(s, r, c);
        if (pty_cell_fg(s, r, c) != fg || pty_cell_bg(s, r, c) != bg
            || (a & attrs) != attrs || (a & no_attrs) != 0) {
            char d[256];
            PTY_ASSERT_FAIL_S_(s, NULL, "cell (%d,%d): expected fg %d bg %d "
                               "attrs 0x%x without 0x%x, got %s", r, c, fg, bg,
                               attrs, no_attrs,
                               pty_cell_describe(s, r, c, d, sizeof(d)));
            return 0;
        }
    }
    return 1;
}

/* The screen has a list row `r` selected (focused): bold reverse in the
 * terminal's own colours (libtuikit v0.4.2), across [c0, c1). */
static int selected_row(PtySession *s, int r, int c0, int c1) {
    return row_style(s, r, c0, c1, PTY_FG_DEFAULT, PTY_BG_DEFAULT,
                     PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0);
}

static void key(PtySession *s, int k) {
    pty_send_key(s, (PtyKey)k);
    pty_settle(s, 150);
}

static void chars(PtySession *s, const char *str) {
    pty_send_str(s, str);
    pty_settle(s, 150);
}

/* Opens the selected dialog and waits for its messages. */
static int open_history(PtySession *s) {
    key(s, PTY_KEY_ENTER);
    g_tests_run++;
    if (pty_wait_for(s, "message 40", 4000) != 0) {
        PTY_ASSERT_FAIL_S_(s, NULL, "Enter should load the history");
        return 0;
    }
    return 1;
}

/* ── Header bar ──────────────────────────────────────────────────────── */

static void test_header_bar(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    if (header_is(s, "Dialogs " MID " 1 of 30")
        && row_style(s, 0, 0, COLS, PTY_FG_WHITE, PTY_BG_BLUE,
                     PTY_ATTR_BOLD, 0)) {
        /* The program's name sits at the right edge, one column in. */
        g_tests_run++;
        if (strcmp(pty_cell_text(s, 0, COLS - 7), "t") != 0
            || strcmp(pty_cell_text(s, 0, COLS - 2), "i") != 0) {
            PTY_ASSERT_FAIL_S_(s, NULL, "\"tg-tui\" should end one column "
                               "from the right edge");
        }
        /* Rows below it keep the terminal's own background. */
        g_tests_run++;
        if (pty_cell_bg(s, 1, COLS - 1) != PTY_BG_DEFAULT) {
            PTY_ASSERT_FAIL_S_(s, NULL, "the body must not be blue");
        }
    }
    finish(s, &stub);
}

static void test_header_follows_the_view_and_position(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    if (header_is(s, "Dialogs " MID " 1 of 30")) {
        chars(s, "j");
        if (header_is(s, "Dialogs " MID " 2 of 30") && open_history(s)
            && header_is(s, "Messages " MID " 1 of 40")) {
            chars(s, "j");
            header_is(s, "Messages " MID " 2 of 40");
            key(s, PTY_KEY_ESC);
            header_is(s, "Dialogs " MID " 2 of 30");
        }
    }
    finish(s, &stub);
}

static void test_header_of_an_empty_list(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, 0, NULL);
    if (!s) return;
    header_is(s, "Dialogs " MID " 0");
    finish(s, &stub);
}

/* ── Dialog list ─────────────────────────────────────────────────────── */

static void test_dialog_rows_and_unread_marker(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    if (row_has(s, 1, "u [7] Alice Anderson")
        && row_has(s, 2, "u     Bob Brown")
        && row_has(s, 3, "u     Person 02")) {
        /* Rows are inside the left pane: nothing of them past column 27. */
        g_tests_run++;
        char row[256];
        pty_row_text(s, 1, row, sizeof(row));
        if (strstr(row, "Alice Anderson") - row > LEFT) {
            PTY_ASSERT_FAIL_S_(s, NULL, "a dialog row must stay in its pane");
        }
        /* The right pane says to select a dialog, dim. */
        if (row_has(s, 12, "(select a dialog)")) {
            g_tests_run++;
            int col = (COLS - LEFT - (int)strlen("(select a dialog)")) / 2 + LEFT;
            if (!(pty_cell_attr(s, 12, col) & PTY_ATTR_DIM)) {
                PTY_ASSERT_FAIL_S_(s, NULL, "the placeholder must be dim");
            }
        }
    }
    finish(s, &stub);
}

static void test_dialog_selection_and_unread_bold(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    /* The first row is selected: bold reverse, the whole pane. */
    if (selected_row(s, 1, 0, LEFT)
        /* The selection stops at the pane's edge. */
        && row_style(s, 1, LEFT, COLS, PTY_FG_DEFAULT, PTY_BG_DEFAULT, 0,
                     PTY_ATTR_BOLD)
        /* A read row is plain. */
        && row_style(s, 2, 0, LEFT, PTY_FG_DEFAULT, PTY_BG_DEFAULT, 0,
                     PTY_ATTR_BOLD | PTY_ATTR_REVERSE | PTY_ATTR_UNDERLINE)) {
        chars(s, "j");
        /* Selection moved; the unread row, no longer selected, is bold on the
         * default colours. */
        if (selected_row(s, 2, 0, LEFT)) {
            row_style(s, 1, 0, 20, PTY_FG_DEFAULT, PTY_BG_DEFAULT,
                      PTY_ATTR_BOLD, PTY_ATTR_REVERSE | PTY_ATTR_UNDERLINE);
        }
    }
    finish(s, &stub);
}

static void test_dialog_movement_keys(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    int ok = header_is(s, "Dialogs " MID " 1 of 30");
    if (ok) { chars(s, "j");        ok = header_is(s, "2 of 30"); }
    if (ok) { chars(s, "k");        ok = header_is(s, "1 of 30"); }
    if (ok) { key(s, PTY_KEY_DOWN); ok = header_is(s, "2 of 30"); }
    if (ok) { key(s, PTY_KEY_UP);   ok = header_is(s, "1 of 30"); }
    if (ok) { chars(s, "G");        ok = header_is(s, "30 of 30"); }
    /* The last row scrolled into view and the first one out of it. */
    if (ok) ok = row_has(s, 22, "Person 29") && screen_has(s, "Alice Anderson", 0)
              && selected_row(s, 22, 0, LEFT);
    if (ok) { chars(s, "g");        ok = header_is(s, "1 of 30"); }
    if (ok) ok = row_has(s, 1, "Alice Anderson") && selected_row(s, 1, 0, LEFT);
    if (ok) { key(s, PTY_KEY_END);  ok = header_is(s, "30 of 30"); }
    if (ok) { key(s, PTY_KEY_HOME); ok = header_is(s, "1 of 30"); }
    if (ok) { key(s, PTY_KEY_PGDN); ok = screen_has(s, "1 of 30", 0); }
    if (ok) { key(s, PTY_KEY_PGUP); ok = header_is(s, "1 of 30"); }
    if (ok) { key(s, PTY_KEY_CTRL('f')); ok = screen_has(s, "1 of 30", 0); }
    if (ok) { key(s, PTY_KEY_CTRL('b')); header_is(s, "1 of 30"); }
    finish(s, &stub);
}

static void test_dialog_list_empty_state(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, 0, "no dialogs");
    if (!s) return;
    if (row_has(s, 12, "(no dialogs - r reloads)")) {
        int col = (LEFT - (int)strlen("(no dialogs - r reloads)")) / 2;
        g_tests_run++;
        if (!(pty_cell_attr(s, 12, col) & PTY_ATTR_DIM)
            || pty_cell_fg(s, 12, col) != PTY_FG_DEFAULT) {
            PTY_ASSERT_FAIL_S_(s, NULL, "the empty-list notice must be dim");
        }
        /* Enter on an empty list opens nothing. */
        key(s, PTY_KEY_ENTER);
        status_has(s, "[dialogs]");
    }
    finish(s, &stub);
}

static void test_dialog_list_failed_state(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS_FAIL, "could not read");
    if (!s) return;
    /* The status row says so too, in bold red, until a key clears it. */
    row_style(s, STATUS_ROW, 0, 20, PTY_FG_RED, PTY_BG_DEFAULT,
              PTY_ATTR_BOLD, 0);
    /* The notice would be clipped in one line in the 28-column pane, so the
     * retry hint is on the next row, whole, in the same style. */
    if (row_has(s, 12, "(could not read dialogs)")
        && row_has(s, 13, "r retries")) {
        int col = (28 - (int)strlen("(could not read dialogs)")) / 2;
        g_tests_run++;
        if (pty_cell_fg(s, 12, col) != PTY_FG_RED
            || !(pty_cell_attr(s, 12, col) & PTY_ATTR_BOLD)) {
            PTY_ASSERT_FAIL_S_(s, NULL, "a failed load must be bold red");
        }
    }
    finish(s, &stub);
}

/* ── History view ────────────────────────────────────────────────────── */

static void test_history_rows_and_selection(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    if (open_history(s)
        /* Even ids are outgoing (">"), odd ones incoming ("<"). */
        && row_has(s, 1, "> [40] message 40")
        && row_has(s, 2, "< [39] message 39")
        /* The first message is selected, focused: bold reverse, the
         * history pane's whole width. */
        && selected_row(s, 1, LEFT, COLS)
        && row_style(s, 2, LEFT, COLS, PTY_FG_DEFAULT, PTY_BG_DEFAULT, 0,
                     PTY_ATTR_BOLD | PTY_ATTR_REVERSE | PTY_ATTR_UNDERLINE)
        /* The dialog list lost the focus: its selected row is underlined on
         * the default colours, not highlighted. */
        && row_style(s, 1, 0, 14, PTY_FG_DEFAULT, PTY_BG_DEFAULT,
                     PTY_ATTR_UNDERLINE, PTY_ATTR_REVERSE)) {
        /* The status row has the history hints, yellow, and the "loading"
         * message is gone. */
        if (status_has(s, "[history]") && status_has(s, "Esc back")
            && screen_has(s, "loading", 0)) {
            row_style(s, STATUS_ROW, 0, 9, PTY_FG_YELLOW, PTY_BG_DEFAULT, 0,
                      PTY_ATTR_REVERSE);
        }
    }
    finish(s, &stub);
}

static void test_history_movement_keys(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    int ok = open_history(s);
    if (ok) { chars(s, "j");        ok = header_is(s, "Messages " MID " 2 of 40"); }
    if (ok) ok = selected_row(s, 2, LEFT, COLS);
    if (ok) { chars(s, "k");        ok = header_is(s, "1 of 40"); }
    if (ok) { key(s, PTY_KEY_DOWN); ok = header_is(s, "2 of 40"); }
    if (ok) { key(s, PTY_KEY_UP);   ok = header_is(s, "1 of 40"); }
    if (ok) { chars(s, "G");        ok = header_is(s, "40 of 40"); }
    if (ok) ok = row_has(s, 22, "message 01") && screen_has(s, "message 40", 0);
    if (ok) { chars(s, "g");        ok = header_is(s, "1 of 40"); }
    if (ok) { key(s, PTY_KEY_END);  ok = header_is(s, "40 of 40"); }
    if (ok) { key(s, PTY_KEY_HOME); ok = header_is(s, "1 of 40"); }
    if (ok) { key(s, PTY_KEY_PGDN); ok = screen_has(s, "1 of 40", 0); }
    if (ok) { key(s, PTY_KEY_PGUP); ok = header_is(s, "1 of 40"); }
    if (ok) { key(s, PTY_KEY_CTRL('f')); ok = screen_has(s, "1 of 40", 0); }
    if (ok) { key(s, PTY_KEY_CTRL('b')); header_is(s, "1 of 40"); }
    finish(s, &stub);
}

static void test_history_failed_state(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY_FAIL,
                              "Alice Anderson");
    if (!s) return;
    key(s, PTY_KEY_ENTER);
    if (status_has(s, "history: load failed")
        /* Bold red on the status row. */
        && row_style(s, STATUS_ROW, 0, 20, PTY_FG_RED, PTY_BG_DEFAULT,
                     PTY_ATTR_BOLD, 0)
        && row_has(s, 12, "(could not read messages - r retries)")) {
        int col = LEFT + (COLS - LEFT
                  - (int)strlen("(could not read messages - r retries)")) / 2;
        g_tests_run++;
        if (pty_cell_fg(s, 12, col) != PTY_FG_RED) {
            PTY_ASSERT_FAIL_S_(s, NULL, "the failed history notice is red");
        }
    }
    finish(s, &stub);
}

static void test_history_placeholder_before_a_dialog_is_opened(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    key(s, PTY_KEY_RIGHT);
    /* Focus moved to the history pane, which has nothing selected yet. */
    if (header_is(s, "Messages " MID " 0") && status_has(s, "[history]")
        && row_has(s, 12, "(select a dialog)")) {
        key(s, PTY_KEY_LEFT);
        header_is(s, "Dialogs " MID " 1 of 30");
    }
    finish(s, &stub);
}

/* ── Focus and going back ────────────────────────────────────────────── */

static void test_focus_keys(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    int ok = open_history(s);
    /* h and Left go back to the dialogs; l and Right (history already
     * loaded) go forward again. */
    if (ok) { chars(s, "h");        ok = status_has(s, "[dialogs]"); }
    if (ok) { chars(s, "l");        ok = status_has(s, "[history]"); }
    if (ok) { key(s, PTY_KEY_LEFT); ok = status_has(s, "[dialogs]"); }
    if (ok) { key(s, PTY_KEY_RIGHT);ok = status_has(s, "[history]"); }
    if (ok) ok = row_has(s, 1, "message 40");
    finish(s, &stub);
}

static void test_back_keys_return_to_the_dialogs(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    const int back[] = { PTY_KEY_ESC, 'q', PTY_KEY_BACK };
    for (int i = 0; i < 3; i++) {
        if (!open_history(s)) break;
        if (back[i] == 'q') chars(s, "q"); else key(s, back[i]);
        if (!status_has(s, "[dialogs]")) break;
        /* Back is a level, not a quit: the program is still running and the
         * dialog list is selected and focused again. */
        if (!selected_row(s, 1, 0, LEFT)) break;
    }
    finish(s, &stub);
}

/* ── Status row ──────────────────────────────────────────────────────── */

static void test_status_hints_of_both_views(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    static const char dh[] = "[dialogs] j/k move  Enter open  l history  "
                             "r reload  ? help  Esc quit";
    static const char hh[] = "[history] j/k scroll  h dialogs  r reload  "
                             "? help  Esc back";
    if (status_has(s, dh)
        && row_style(s, STATUS_ROW, 0, (int)strlen(dh), PTY_FG_YELLOW,
                     PTY_BG_DEFAULT, 0, PTY_ATTR_REVERSE | PTY_ATTR_BOLD)
        && open_history(s) && status_has(s, hh)) {
        row_style(s, STATUS_ROW, 0, (int)strlen(hh), PTY_FG_YELLOW,
                  PTY_BG_DEFAULT, 0, PTY_ATTR_REVERSE | PTY_ATTR_BOLD);
    }
    finish(s, &stub);
}

static void test_reload_keeps_the_cursor(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    chars(s, "jjj");
    if (header_is(s, "Dialogs " MID " 4 of 30")) {
        chars(s, "r");
        /* "reloaded": bold green; the cursor and the view stay. */
        if (status_has(s, "reloaded")
            && row_style(s, STATUS_ROW, 0, 8, PTY_FG_GREEN, PTY_BG_DEFAULT,
                         PTY_ATTR_BOLD, 0)) {
            header_is(s, "Dialogs " MID " 4 of 30");
        }
    }
    finish(s, &stub);
}

static void test_failed_reload_says_so_in_red(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS_FAIL, "could not read");
    if (!s) return;
    chars(s, "r");
    if (status_has(s, "reload failed")) {
        row_style(s, STATUS_ROW, 0, 13, PTY_FG_RED, PTY_BG_DEFAULT,
                  PTY_ATTR_BOLD, 0);
    }
    finish(s, &stub);
}

/* ── Help overlays ───────────────────────────────────────────────────── */

static void test_help_of_the_dialog_list(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS, "Alice Anderson");
    if (!s) return;
    chars(s, "?");
    if (screen_has(s, "Dialog list shortcuts", 1)) {
        static const char *const lines[] = {
            "next dialog", "previous dialog", "page down", "page up",
            "first dialog", "last dialog", "open the dialog's history",
            "reload the dialogs", "this help", "quit at once",
        };
        int ok = 1;
        for (size_t i = 0; ok && i < sizeof(lines) / sizeof(*lines); i++)
            ok = screen_has(s, lines[i], 1);
        /* The view stays visible round the box: the header and the status
         * line are still drawn, and the movement keys do not reach the
         * list underneath. */
        if (ok) ok = row_has(s, 0, "tg-tui");
        if (ok) {
            chars(s, "j");
            ok = header_is(s, "Dialogs " MID " 1 of 30");
        }
        if (ok) {
            key(s, PTY_KEY_ESC);
            ok = screen_has(s, "Dialog list shortcuts", 0);
        }
        if (ok) {
            chars(s, "j");
            header_is(s, "Dialogs " MID " 2 of 30");
        }
    }
    finish(s, &stub);
}

static void test_help_of_the_history_view(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    if (open_history(s)) {
        chars(s, "?");
        if (screen_has(s, "Message history shortcuts", 1)) {
            static const char *const lines[] = {
                "next message", "previous message", "first message",
                "last message", "back to the dialogs", "reload the messages",
            };
            int ok = 1;
            for (size_t i = 0; ok && i < sizeof(lines) / sizeof(*lines); i++)
                ok = screen_has(s, lines[i], 1);
            if (ok) {
                chars(s, "q");
                /* q closes the help only; the history view is still up. */
                ok = screen_has(s, "Message history shortcuts", 0)
                     && status_has(s, "[history]");
            }
        }
    }
    finish(s, &stub);
}

/* ── Resize and ctrl-c ───────────────────────────────────────────────── */

static void test_resize_repaints_every_element(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    if (open_history(s)) {
        pty_resize(s, 60, 16);
        pty_settle(s, 300);
        int last = 15;
        g_tests_run++;
        if (!pty_row_contains(s, last, "[history]")) {
            PTY_ASSERT_FAIL_S_(s, NULL, "the status row should move to the "
                               "new last row");
        } else if (row_style(s, 0, 0, 60, PTY_FG_WHITE, PTY_BG_BLUE,
                             PTY_ATTR_BOLD, 0)
                   && row_has(s, 0, "Messages " MID " 1 of 40")
                   && row_has(s, 0, "tg-tui")) {
            /* The message selection follows the pane's new width. */
            selected_row(s, 1, 28, 60);
        }
    }
    finish(s, &stub);
}

static void test_ctrl_c_quits_from_the_history(void) {
    PtyTelStub stub;
    PtySession *s = start_tui(&stub, PTY_STUB_DIALOGS | PTY_STUB_HISTORY,
                              "Alice Anderson");
    if (!s) return;
    if (open_history(s)) {
        pty_send_key(s, PTY_KEY_CTRL_C);
        int code = pty_wait_exit(s, 5000);
        g_tests_run++;
        if (code != 0 && code != 130) {
            PTY_ASSERT_FAIL_S_(s, NULL, "ctrl-c should quit at once, "
                               "exit status %d", code);
        }
    }
    finish(s, &stub);
}

int main(void) {
    printf("TG-16 tg-tui element tests\n");

    RUN_TEST(test_header_bar);
    RUN_TEST(test_header_follows_the_view_and_position);
    RUN_TEST(test_header_of_an_empty_list);
    RUN_TEST(test_dialog_rows_and_unread_marker);
    RUN_TEST(test_dialog_selection_and_unread_bold);
    RUN_TEST(test_dialog_movement_keys);
    RUN_TEST(test_dialog_list_empty_state);
    RUN_TEST(test_dialog_list_failed_state);
    RUN_TEST(test_history_rows_and_selection);
    RUN_TEST(test_history_movement_keys);
    RUN_TEST(test_history_failed_state);
    RUN_TEST(test_history_placeholder_before_a_dialog_is_opened);
    RUN_TEST(test_focus_keys);
    RUN_TEST(test_back_keys_return_to_the_dialogs);
    RUN_TEST(test_status_hints_of_both_views);
    RUN_TEST(test_reload_keeps_the_cursor);
    RUN_TEST(test_failed_reload_says_so_in_red);
    RUN_TEST(test_help_of_the_dialog_list);
    RUN_TEST(test_help_of_the_history_view);
    RUN_TEST(test_resize_repaints_every_element);
    RUN_TEST(test_ctrl_c_quits_from_the_history);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
