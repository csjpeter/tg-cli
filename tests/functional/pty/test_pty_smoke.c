/**
 * @file test_pty_smoke.c
 * @brief PTY harness smoke tests — verifies that libptytest itself works.
 *
 * These tests do not require a running tg-cli binary; they exercise the
 * PTY harness directly by spawning simple POSIX utilities.
 */

#include "ptytest.h"
#include "pty_assert.h"
#include <stdio.h>
#include <string.h>

static int g_tests_run    = 0;
static int g_tests_failed = 0;

/* ── Helpers ─────────────────────────────────────────────────────────── */

#define RUN_TEST(fn) do { \
    printf("  running %s ...\n", #fn); \
    fn(); \
} while(0)

/* ── Tests ───────────────────────────────────────────────────────────── */

/**
 * @brief Spawns /bin/echo and asserts the output appears on the screen.
 */
static void test_echo_hello(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open() should succeed");

    const char *argv[] = { "/bin/echo", "hello", NULL };
    int rc = pty_run(s, argv);
    ASSERT(rc == 0, "pty_run(/bin/echo hello) should succeed");

    int found = pty_wait_for(s, "hello", 2000);
    ASSERT(found == 0, "screen should contain 'hello' after /bin/echo");

    pty_close(s);
}

/**
 * @brief Verifies pty_screen_contains() returns 0 for absent text.
 */
static void test_absent_text(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open() should succeed");

    const char *argv[] = { "/bin/echo", "world", NULL };
    int rc = pty_run(s, argv);
    ASSERT(rc == 0, "pty_run() should succeed");

    pty_wait_for(s, "world", 2000);

    /* "hello" was never printed — must not be found */
    int found = pty_screen_contains(s, "hello");
    ASSERT(found == 0, "screen must NOT contain 'hello' when only 'world' was echoed");

    pty_close(s);
}

/**
 * @brief Checks that pty_row_text() extracts the expected content.
 */
static void test_row_text(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open() should succeed");

    const char *argv[] = { "/bin/echo", "rowtest", NULL };
    int rc = pty_run(s, argv);
    ASSERT(rc == 0, "pty_run() should succeed");

    pty_wait_for(s, "rowtest", 2000);

    char buf[256];
    /* The text must appear somewhere across the visible rows */
    int found = 0;
    for (int r = 0; r < 24; r++) {
        pty_row_text(s, r, buf, sizeof(buf));
        if (strstr(buf, "rowtest")) { found = 1; break; }
    }
    ASSERT(found, "pty_row_text() should expose 'rowtest' on some row");

    pty_close(s);
}

/**
 * @brief Verifies terminal dimensions reported by pty_get_size().
 */
static void test_get_size(void) {
    PtySession *s = pty_open(120, 40);
    ASSERT(s != NULL, "pty_open(120, 40) should succeed");

    int cols = 0, rows = 0;
    pty_get_size(s, &cols, &rows);
    ASSERT(cols == 120, "pty_get_size() cols should be 120");
    ASSERT(rows == 40,  "pty_get_size() rows should be 40");

    pty_close(s);
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(void) {
    printf("PTY smoke tests\n");

    RUN_TEST(test_echo_hello);
    RUN_TEST(test_absent_text);
    RUN_TEST(test_row_text);
    RUN_TEST(test_get_size);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
