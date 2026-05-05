/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_config_wizard_batch.c
 * @brief Functional tests for config_wizard_run_batch() (FEAT-37).
 *
 * Tests:
 *   - Happy path: writes config.ini with mode 0600 and correct content.
 *   - Refuses to overwrite an existing non-empty config without --force.
 *   - Accepts overwrite with --force.
 *   - Rejects malformed api_id (0, negative, non-numeric).
 *   - Rejects malformed api_hash (wrong length, uppercase, non-hex).
 */

#include "test_helpers.h"
#include "app/config_wizard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Helpers ---- */

/** Return a temp dir unique to this test run. */
static const char *get_tmp_dir(void) {
    static char tmp[256];
    if (tmp[0]) return tmp;
    const char *t = getenv("TMPDIR");
    if (!t) t = "/tmp";
    snprintf(tmp, sizeof(tmp), "%s/tg-cli-wiz-test-%d", t, (int)getpid());
    return tmp;
}

/** Set XDG_CONFIG_HOME to @p dir so config_wizard writes there. */
static void set_config_home(const char *dir) {
    setenv("XDG_CONFIG_HOME", dir, 1);
}

/** Build the expected config.ini path. */
static void cfg_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/tg-cli/config.ini", get_tmp_dir());
}

/** Remove the test config file (ignore errors). */
static void rm_cfg(void) {
    char p[512]; cfg_path(p, sizeof(p));
    unlink(p);
}

/** Create a non-empty config file to simulate "already configured". */
static void create_nonempty_cfg(void) {
    char p[512]; cfg_path(p, sizeof(p));
    char dir[512]; snprintf(dir, sizeof(dir), "%s/tg-cli", get_tmp_dir());
    /* mkdir -p */
    char cmd[1024]; snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
    int sysrc = system(cmd); (void)sysrc;
    FILE *fp = fopen(p, "w");
    if (fp) { fprintf(fp, "api_id=99999\napi_hash=%s\n", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"); fclose(fp); }
}

/* ---- Tests ---- */

static void test_batch_happy_path(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();

    int rc = config_wizard_run_batch("12345",
                                     "deadbeefdeadbeefdeadbeefdeadbeef",
                                     0);
    ASSERT(rc == 0, "batch happy: must return 0");

    /* Check file exists. */
    char p[512]; cfg_path(p, sizeof(p));
    struct stat st;
    ASSERT(stat(p, &st) == 0, "batch happy: config.ini must exist");

    /* Check mode 0600. */
    ASSERT((st.st_mode & 0777) == 0600, "batch happy: mode must be 0600");

    /* Check content. */
    FILE *fp = fopen(p, "r");
    ASSERT(fp != NULL, "batch happy: can open config.ini");
    char content[512] = {0};
    size_t n = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    ASSERT(n > 0, "batch happy: config.ini is non-empty");
    ASSERT(strstr(content, "api_id=12345") != NULL,
           "batch happy: config.ini contains api_id=12345");
    ASSERT(strstr(content, "api_hash=deadbeefdeadbeefdeadbeefdeadbeef") != NULL,
           "batch happy: config.ini contains correct api_hash");

    rm_cfg();
}

static void test_batch_refuses_overwrite_without_force(void) {
    set_config_home(get_tmp_dir());
    create_nonempty_cfg();

    int rc = config_wizard_run_batch("12345",
                                     "deadbeefdeadbeefdeadbeefdeadbeef",
                                     0 /* force=0 */);
    ASSERT(rc != 0, "batch no-force: must refuse to overwrite existing config");

    rm_cfg();
}

static void test_batch_force_overwrites(void) {
    set_config_home(get_tmp_dir());
    create_nonempty_cfg();

    int rc = config_wizard_run_batch("99",
                                     "abcdef0123456789abcdef0123456789",
                                     1 /* force=1 */);
    ASSERT(rc == 0, "batch --force: must succeed");

    char p[512]; cfg_path(p, sizeof(p));
    FILE *fp = fopen(p, "r");
    ASSERT(fp != NULL, "batch --force: config.ini must exist after overwrite");
    char content[512] = {0};
    size_t nf = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    (void)nf;
    ASSERT(strstr(content, "api_id=99") != NULL,
           "batch --force: config.ini has new api_id");

    rm_cfg();
}

static void test_batch_rejects_zero_api_id(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    ASSERT(config_wizard_run_batch("0", "deadbeefdeadbeefdeadbeefdeadbeef", 0) != 0,
           "api_id=0: must fail");
}

static void test_batch_rejects_negative_api_id(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    ASSERT(config_wizard_run_batch("-1", "deadbeefdeadbeefdeadbeefdeadbeef", 0) != 0,
           "api_id=-1: must fail");
}

static void test_batch_rejects_non_numeric_api_id(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    ASSERT(config_wizard_run_batch("abc", "deadbeefdeadbeefdeadbeefdeadbeef", 0) != 0,
           "api_id=abc: must fail");
}

static void test_batch_rejects_wrong_length_api_hash(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    /* 31 chars — too short */
    ASSERT(config_wizard_run_batch("12345", "deadbeefdeadbeefdeadbeefdeadbee", 0) != 0,
           "api_hash 31 chars: must fail");
}

static void test_batch_rejects_uppercase_api_hash(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    /* 32 chars but uppercase */
    ASSERT(config_wizard_run_batch("12345", "DEADBEEFDEADBEEFDEADBEEFDEADBEEF", 0) != 0,
           "api_hash uppercase: must fail");
}

static void test_batch_rejects_non_hex_api_hash(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    /* 32 chars but 'z' is not hex */
    ASSERT(config_wizard_run_batch("12345", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", 0) != 0,
           "api_hash non-hex: must fail");
}

static void test_batch_null_args(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    ASSERT(config_wizard_run_batch(NULL, "deadbeefdeadbeefdeadbeefdeadbeef", 0) != 0,
           "null api_id_str: must fail");
    ASSERT(config_wizard_run_batch("12345", NULL, 0) != 0,
           "null api_hash_str: must fail");
}

/* Script-invocation guard: when stdin is not a TTY (e.g. piped from a
 * shell script or a cron job), config_wizard_run_interactive() must
 * bail out immediately with a clear error rather than block on fgets
 * (which would read EOF in a tight loop or hang). CTest runs its
 * children with stdin attached to a pipe, so calling the function
 * here is the exact script scenario. */
static void test_interactive_refuses_when_stdin_not_tty(void) {
    set_config_home(get_tmp_dir());
    rm_cfg();
    ASSERT(!isatty(STDIN_FILENO),
           "precondition: CTest gives us a non-TTY stdin");
    /* Must return -1 quickly — no read, no hang. */
    ASSERT(config_wizard_run_interactive(0) == -1,
           "interactive wizard: must refuse non-TTY stdin with rc=-1");
    /* Must NOT have created the config file on this error path. */
    struct stat st;
    char cfg[1024];
    snprintf(cfg, sizeof(cfg), "%s/tg-cli/config.ini", get_tmp_dir());
    ASSERT(stat(cfg, &st) != 0,
           "interactive wizard: must not create config on TTY guard failure");
}

/* ---- Runner ---- */

void run_config_wizard_batch_tests(void) {
    RUN_TEST(test_batch_happy_path);
    RUN_TEST(test_batch_refuses_overwrite_without_force);
    RUN_TEST(test_batch_force_overwrites);
    RUN_TEST(test_batch_rejects_zero_api_id);
    RUN_TEST(test_batch_rejects_negative_api_id);
    RUN_TEST(test_batch_rejects_non_numeric_api_id);
    RUN_TEST(test_batch_rejects_wrong_length_api_hash);
    RUN_TEST(test_batch_rejects_uppercase_api_hash);
    RUN_TEST(test_batch_rejects_non_hex_api_hash);
    RUN_TEST(test_batch_null_args);
    RUN_TEST(test_interactive_refuses_when_stdin_not_tty);
}
