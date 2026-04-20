/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_config_wizard_pty.c
 * @brief PTY functional test for the interactive config wizard (FEAT-37).
 *
 * Launches `tg-cli login` under a PTY and verifies:
 *   1. The welcome text appears.
 *   2. The api_hash prompt is present.
 *   3. Characters typed at the api_hash prompt do NOT echo back to the master.
 *   4. After submitting valid values the binary prints "Saved to …/config.ini"
 *      and exits with code 0.
 *   5. The generated config.ini has mode 0600.
 */

#include "ptytest.h"
#include "pty_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Defined by CMake: path to the tg-cli binary under test. */
#ifndef TG_CLI_BINARY
#  error "TG_CLI_BINARY must be defined by CMake"
#endif

/* ---- helpers ---- */

/** Unique temp dir per process for XDG_CONFIG_HOME. */
static char g_tmp_dir[256];

static void setup_tmp_dir(void) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    snprintf(g_tmp_dir, sizeof(g_tmp_dir),
             "%s/tg-cli-pty-wiz-%d", base, (int)getpid());
    /* mkdir -p */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", g_tmp_dir);
    int rc = system(cmd); (void)rc;
}

static void cleanup_tmp_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmp_dir);
    int rc = system(cmd); (void)rc;
}

static int g_tests_run    = 0;
static int g_tests_failed = 0;

/* ── Tests ─────────────────────────────────────────────────────────── */

/**
 * @brief Happy-path PTY test.
 *
 * Launches `tg-cli login` with XDG_CONFIG_HOME pointed at a temp dir,
 * feeds valid api_id (without echo suppression) then api_hash (with echo
 * suppression), and checks:
 *   - Welcome text visible.
 *   - api_hash typed chars NOT echoed (the hash string must NOT appear
 *     anywhere on screen after typing it).
 *   - "Saved to" line appears.
 *   - Exit code 0.
 *   - config.ini has mode 0600.
 */
static void test_wizard_pty_happy(void) {
    setup_tmp_dir();

    /* Point XDG_CONFIG_HOME to our temp dir so we don't touch the real one. */
    setenv("XDG_CONFIG_HOME", g_tmp_dir, 1);

    PtySession *s = pty_open(100, 30);
    ASSERT(s != NULL, "pty_open must succeed");

    const char *argv[] = { TG_CLI_BINARY, "login", NULL };
    int rc = pty_run(s, argv);
    ASSERT(rc == 0, "pty_run(tg-cli login) must succeed");

    /* Wait for welcome text. */
    int found = pty_wait_for(s, "Welcome to tg-cli", 5000);
    ASSERT(found == 0, "welcome text must appear on screen");

    /* Wait for the api_id prompt. */
    found = pty_wait_for(s, "Enter your api_id", 5000);
    ASSERT(found == 0, "api_id prompt must appear");

    /* Type a valid api_id followed by Enter. */
    pty_send(s, "99999\r", 6);

    /* Wait for the api_hash prompt. */
    found = pty_wait_for(s, "Enter your api_hash", 5000);
    ASSERT(found == 0, "api_hash prompt must appear");

    /* Type a valid api_hash (32 lowercase hex chars). */
    const char *hash = "abcdef0123456789abcdef0123456789";
    pty_send(s, hash, 32);
    /* Give the terminal a moment to process then check no echo. */
    pty_wait_for(s, "Saved to", 200); /* short timeout — probably not there yet */

    /* The hash should NOT appear in the screen buffer. */
    int hash_visible = pty_screen_contains(s, hash);
    ASSERT(hash_visible == 0,
           "api_hash must NOT echo: secret string must not appear on screen");

    /* Send Enter to submit. */
    pty_send(s, "\r", 1);

    /* Wait for "Saved to" confirmation. */
    found = pty_wait_for(s, "Saved to", 5000);
    ASSERT(found == 0, "\"Saved to\" must appear after valid input");

    /* Wait for the process to exit. */
    int exit_code = pty_wait_exit(s, 5000);
    ASSERT(exit_code == 0, "tg-cli login must exit with code 0");

    /* Verify config.ini mode 0600. */
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/tg-cli/config.ini", g_tmp_dir);
    struct stat st;
    ASSERT(stat(cfg_path, &st) == 0, "config.ini must exist after wizard");
    ASSERT((st.st_mode & 0777) == 0600, "config.ini must have mode 0600");

    pty_close(s);
    cleanup_tmp_dir();
    unsetenv("XDG_CONFIG_HOME");
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(void) {
    printf("PTY config wizard tests (%s)\n", TG_CLI_BINARY);

    test_wizard_pty_happy();

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
