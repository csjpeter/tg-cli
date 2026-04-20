/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_config_ini_robustness.c
 * @brief TEST-84 — functional tests for ~/.config/tg-cli/config.ini parsing
 *        edge cases in src/app/credentials.c (US-33).
 *
 * Each test seeds a byte-level config.ini, redirects HOME to an isolated
 * scratch dir, unsets XDG_CONFIG_HOME/XDG_CACHE_HOME (so CI runners do not
 * override the redirect), clears TG_CLI_API_ID / TG_CLI_API_HASH env vars,
 * and then asserts either a successful credentials_load() with the
 * expected values OR a specific diagnostic on the log file.
 *
 * Scenarios (from the ticket):
 *   1.  test_crlf_line_endings_parsed_cleanly
 *   2.  test_utf8_bom_skipped_at_start
 *   3.  test_hash_comment_ignored
 *   4.  test_semicolon_comment_ignored
 *   5.  test_leading_trailing_whitespace_trimmed
 *   6.  test_quoted_value_strips_quotes
 *   7.  test_empty_value_is_missing_credential
 *   8.  test_only_api_id_reports_api_hash_missing
 *   9.  test_only_api_hash_reports_api_id_missing
 *  10.  test_duplicate_key_last_wins_and_warns
 *  11.  test_empty_file_is_missing_credentials
 *  12.  test_api_hash_wrong_length_rejected
 */

#include "test_helpers.h"

#include "app/credentials.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Canonical 32-char lowercase-hex sample api_hash for happy paths. */
#define VALID_HASH "deadbeefdeadbeefdeadbeefdeadbeef"

/** Scratch dir template — tag + pid keeps parallel runs isolated. */
static void scratch_dir_for(const char *tag, char *out, size_t cap) {
    snprintf(out, cap, "/tmp/tg-cli-ft-cfgini-%s-%d", tag, (int)getpid());
}

/** rm -rf @p path (best-effort). */
static void rm_rf(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    int sysrc = system(cmd);
    (void)sysrc;
}

/** mkdir -p @p path. */
static int mkdir_p(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    int sysrc = system(cmd);
    return sysrc == 0 ? 0 : -1;
}

/**
 * Redirect HOME to a fresh scratch dir for the given @p tag, unset
 * XDG_CONFIG_HOME / XDG_CACHE_HOME (CI runners export them), clear the
 * env-var credentials (so the INI is the only source), init the logger at
 * a per-test path, and return the config.ini path in @p out_ini.
 *
 * The caller is responsible for populating config.ini after this returns.
 */
static void with_fresh_home(const char *tag,
                            char *out_home, size_t home_cap,
                            char *out_ini,  size_t ini_cap,
                            char *out_log,  size_t log_cap) {
    /* Intentionally modest caps — a 128-byte scratch root keeps the
     *  compile-time FORTIFY check for snprintf happy while still leaving
     * plenty of headroom for the /tmp/tg-cli-ft-cfgini-<tag>-<pid> prefix. */
    char home_buf[256];
    scratch_dir_for(tag, home_buf, sizeof(home_buf));
    rm_rf(home_buf);

    char cfg_dir[512];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", home_buf);
    (void)mkdir_p(cfg_dir);

    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/tg-cli/logs", home_buf);
    (void)mkdir_p(cache_dir);

    setenv("HOME", home_buf, 1);
    /* CI runners (GitHub Actions) set XDG_CONFIG_HOME / XDG_CACHE_HOME.
     * Without these unsets platform_config_dir() would point at the CI
     * runner's own config tree and the test would read somebody else's
     * config.ini. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
    unsetenv("TG_CLI_API_ID");
    unsetenv("TG_CLI_API_HASH");

    snprintf(out_home, home_cap, "%s", home_buf);
    snprintf(out_ini,  ini_cap,  "%s/config.ini",  cfg_dir);
    snprintf(out_log,  log_cap,  "%s/session.log", cache_dir);
    (void)unlink(out_log);
    (void)logger_init(out_log, LOG_DEBUG);
}

/** Write @p n bytes from @p buf to @p path (overwrite). */
static int write_bytes(const char *path, const void *buf, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t wrote = fwrite(buf, 1, n, fp);
    fclose(fp);
    return wrote == n ? 0 : -1;
}

/** Convenience: write a NUL-terminated string to @p path as-is. */
static int write_text(const char *path, const char *text) {
    return write_bytes(path, text, strlen(text));
}

/** Read file @p path into a heap buffer; caller frees. NUL-terminated. */
static char *slurp(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (size_out) *size_out = n;
    return buf;
}

/** Slurp the log file and return 1 if @p needle is a substring. */
static int log_contains(const char *log_path, const char *needle) {
    size_t sz = 0;
    char *buf = slurp(log_path, &sz);
    if (!buf) return 0;
    int hit = (strstr(buf, needle) != NULL);
    free(buf);
    return hit;
}

/** Flush the logger so slurp() sees the latest diagnostics. */
static void flush_logs(void) {
    logger_close();
}

/* ================================================================ */
/* 1. CRLF line endings                                             */
/* ================================================================ */

/**
 * Windows-style CRLF line endings must parse cleanly: api_hash must not
 * retain a trailing \r that would later fail the 32-char hex check.
 */
static void test_crlf_line_endings_parsed_cleanly(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("crlf", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "api_id=12345\r\n"
        "api_hash=" VALID_HASH "\r\n";
    ASSERT(write_text(ini, body) == 0, "CRLF: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "CRLF: credentials_load succeeds");
    ASSERT(cfg.api_id == 12345, "CRLF: api_id parsed");
    ASSERT(cfg.api_hash != NULL, "CRLF: api_hash not NULL");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "CRLF: api_hash parsed with no trailing \\r");
    /* Extra belt-and-braces: there must be no literal CR byte in the
     * returned string (was a real regression in the pre-fix parser). */
    ASSERT(strchr(cfg.api_hash, '\r') == NULL,
           "CRLF: returned api_hash carries no CR byte");

    rm_rf(home);
}

/* ================================================================ */
/* 2. UTF-8 BOM                                                     */
/* ================================================================ */

/**
 * A BOM (EF BB BF) at the start of config.ini must be skipped so the
 * first key on line 1 is still recognised.
 */
static void test_utf8_bom_skipped_at_start(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("bom", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    /* EF BB BF | api_id=777\napi_hash=...\n */
    unsigned char buf[256];
    size_t off = 0;
    buf[off++] = 0xEF; buf[off++] = 0xBB; buf[off++] = 0xBF;
    const char *rest = "api_id=777\napi_hash=" VALID_HASH "\n";
    memcpy(buf + off, rest, strlen(rest));
    off += strlen(rest);
    ASSERT(write_bytes(ini, buf, off) == 0, "BOM: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "BOM: credentials_load succeeds");
    ASSERT(cfg.api_id == 777, "BOM: api_id parsed past the BOM");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "BOM: api_hash parsed");

    rm_rf(home);
}

/* ================================================================ */
/* 3. # comment                                                     */
/* ================================================================ */

/**
 * A `#` comment line must be skipped entirely — the parser must not try
 * to match `api_id` against `# tg-cli config` or similar.
 */
static void test_hash_comment_ignored(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("hash", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "# tg-cli config\n"
        "# generated 2026-04-20\n"
        "api_id=42\n"
        "api_hash=" VALID_HASH "\n";
    ASSERT(write_text(ini, body) == 0, "HASH: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "HASH: credentials_load succeeds");
    ASSERT(cfg.api_id == 42, "HASH: api_id parsed past # comments");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "HASH: api_hash parsed past # comments");

    rm_rf(home);
}

/* ================================================================ */
/* 4. ; comment                                                     */
/* ================================================================ */

/**
 * A `;` alt-comment line must also be skipped entirely.
 */
static void test_semicolon_comment_ignored(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("semi", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "; alt-comment form\n"
        "api_id=99\n"
        "; trailing alt-comment\n"
        "api_hash=" VALID_HASH "\n";
    ASSERT(write_text(ini, body) == 0, "SEMI: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "SEMI: credentials_load succeeds");
    ASSERT(cfg.api_id == 99, "SEMI: api_id parsed past ; comments");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "SEMI: api_hash parsed past ; comments");

    rm_rf(home);
}

/* ================================================================ */
/* 5. Whitespace                                                    */
/* ================================================================ */

/**
 * Leading/trailing whitespace around key AND value must be trimmed.
 */
static void test_leading_trailing_whitespace_trimmed(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("ws", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "  api_id =  12345  \n"
        "\tapi_hash\t=\t" VALID_HASH "\t\n";
    ASSERT(write_text(ini, body) == 0, "WS: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "WS: credentials_load succeeds");
    ASSERT(cfg.api_id == 12345, "WS: api_id trimmed correctly");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "WS: api_hash trimmed correctly");

    rm_rf(home);
}

/* ================================================================ */
/* 6. Quoted value                                                  */
/* ================================================================ */

/**
 * Double-quoted values must have their quotes stripped so the inner
 * string is what reaches ApiConfig.api_hash.
 */
static void test_quoted_value_strips_quotes(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("quote", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "api_id=1\n"
        "api_hash=\"" VALID_HASH "\"\n";
    ASSERT(write_text(ini, body) == 0, "QUOTE: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "QUOTE: credentials_load succeeds");
    ASSERT(cfg.api_id == 1, "QUOTE: api_id parsed");
    ASSERT(cfg.api_hash != NULL, "QUOTE: api_hash not NULL");
    ASSERT(strcmp(cfg.api_hash, VALID_HASH) == 0,
           "QUOTE: surrounding quotes stripped from api_hash");
    ASSERT(strchr(cfg.api_hash, '"') == NULL,
           "QUOTE: no stray quote byte in api_hash");

    rm_rf(home);
}

/* ================================================================ */
/* 7. Empty value                                                   */
/* ================================================================ */

/**
 * `api_id=\n` (empty RHS) must be treated as missing, produce a clear
 * LOG_ERROR pointing at the wizard, and NOT crash.
 */
static void test_empty_value_is_missing_credential(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("empty", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "api_id=\n"
        "api_hash=" VALID_HASH "\n";
    ASSERT(write_text(ini, body) == 0, "EMPTY: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == -1, "EMPTY: credentials_load reports failure");
    ASSERT(log_contains(log, "api_id"),
           "EMPTY: log mentions api_id");
    ASSERT(log_contains(log, "wizard") || log_contains(log, "config --wizard"),
           "EMPTY: log points user at the wizard");

    rm_rf(home);
}

/* ================================================================ */
/* 8. Only api_id                                                   */
/* ================================================================ */

/**
 * Config file has only api_id (no api_hash line at all). Error message
 * must target api_hash specifically and reference the wizard.
 */
static void test_only_api_id_reports_api_hash_missing(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("onlyid", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    ASSERT(write_text(ini, "api_id=12345\n") == 0, "ONLYID: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == -1, "ONLYID: credentials_load reports failure");
    ASSERT(log_contains(log, "api_hash"),
           "ONLYID: diagnostic references api_hash");
    ASSERT(log_contains(log, "wizard"),
           "ONLYID: diagnostic mentions the wizard");
    /* And crucially: it should NOT say api_id is missing. */
    ASSERT(!log_contains(log, "api_id not found") &&
           !log_contains(log, "api_id/api_hash not found"),
           "ONLYID: diagnostic does not falsely claim api_id missing");

    rm_rf(home);
}

/* ================================================================ */
/* 9. Only api_hash                                                 */
/* ================================================================ */

/**
 * Config file has only api_hash (no api_id). Error must target api_id
 * specifically.
 */
static void test_only_api_hash_reports_api_id_missing(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("onlyhash", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    ASSERT(write_text(ini, "api_hash=" VALID_HASH "\n") == 0,
           "ONLYHASH: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == -1, "ONLYHASH: credentials_load reports failure");
    ASSERT(log_contains(log, "api_id"),
           "ONLYHASH: diagnostic references api_id");
    ASSERT(log_contains(log, "wizard"),
           "ONLYHASH: diagnostic mentions the wizard");
    ASSERT(!log_contains(log, "api_hash not found"),
           "ONLYHASH: diagnostic does not falsely claim api_hash missing");

    rm_rf(home);
}

/* ================================================================ */
/* 10. Duplicate keys                                               */
/* ================================================================ */

/**
 * If a key appears twice, the last occurrence wins AND LOG_WARN is
 * emitted explaining the duplicate.
 */
static void test_duplicate_key_last_wins_and_warns(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("dup", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    const char *body =
        "api_id=111\n"
        "api_id=222\n"
        "api_hash=" VALID_HASH "\n";
    ASSERT(write_text(ini, body) == 0, "DUP: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == 0, "DUP: credentials_load succeeds");
    ASSERT(cfg.api_id == 222, "DUP: last api_id wins (222, not 111)");
    ASSERT(log_contains(log, "duplicate"),
           "DUP: LOG_WARN about duplicate api_id is emitted");

    rm_rf(home);
}

/* ================================================================ */
/* 11. Empty file                                                   */
/* ================================================================ */

/**
 * A zero-byte config.ini must be treated like a missing file — clear
 * "credentials not found" diagnostic, no crash.
 */
static void test_empty_file_is_missing_credentials(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("zero", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    ASSERT(write_bytes(ini, "", 0) == 0, "ZERO: write empty config.ini");
    /* Confirm the file is actually empty on disk. */
    struct stat st;
    ASSERT(stat(ini, &st) == 0 && st.st_size == 0,
           "ZERO: config.ini is zero-byte");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == -1, "ZERO: credentials_load reports failure");
    ASSERT(log_contains(log, "api_id") && log_contains(log, "api_hash"),
           "ZERO: diagnostic mentions both api_id and api_hash");
    ASSERT(log_contains(log, "wizard"),
           "ZERO: diagnostic mentions the wizard");

    rm_rf(home);
}

/* ================================================================ */
/* 12. Wrong-length api_hash                                        */
/* ================================================================ */

/**
 * An api_hash that is not exactly 32 hex chars must be rejected with a
 * dedicated LOG_ERROR and cause credentials_load() to fail, so a
 * truncated paste never becomes the live credential.
 */
static void test_api_hash_wrong_length_rejected(void) {
    char home[512], ini[768], log[768];
    with_fresh_home("hashlen", home, sizeof(home), ini, sizeof(ini),
                    log, sizeof(log));

    /* 31 chars — one byte too short. */
    const char *body =
        "api_id=12345\n"
        "api_hash=deadbeefdeadbeefdeadbeefdeadbee\n";
    ASSERT(write_text(ini, body) == 0, "HASHLEN: write config.ini");

    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    flush_logs();

    ASSERT(rc == -1, "HASHLEN: credentials_load reports failure");
    ASSERT(log_contains(log, "api_hash"),
           "HASHLEN: diagnostic mentions api_hash");
    ASSERT(log_contains(log, "32") || log_contains(log, "hex"),
           "HASHLEN: diagnostic explains the expected length/hex rule");

    rm_rf(home);

    /* Second variant — 33 chars, over by one — just to exercise the
     * other side of the length check. */
    char home2[512], ini2[768], log2[768];
    with_fresh_home("hashlen33", home2, sizeof(home2),
                    ini2, sizeof(ini2), log2, sizeof(log2));
    const char *body2 =
        "api_id=12345\n"
        "api_hash=deadbeefdeadbeefdeadbeefdeadbeef0\n";
    ASSERT(write_text(ini2, body2) == 0, "HASHLEN33: write config.ini");

    ApiConfig cfg2;
    int rc2 = credentials_load(&cfg2);
    flush_logs();

    ASSERT(rc2 == -1, "HASHLEN33: 33-char api_hash also rejected");
    ASSERT(log_contains(log2, "api_hash"),
           "HASHLEN33: diagnostic mentions api_hash");

    rm_rf(home2);
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_config_ini_robustness_tests(void) {
    RUN_TEST(test_crlf_line_endings_parsed_cleanly);
    RUN_TEST(test_utf8_bom_skipped_at_start);
    RUN_TEST(test_hash_comment_ignored);
    RUN_TEST(test_semicolon_comment_ignored);
    RUN_TEST(test_leading_trailing_whitespace_trimmed);
    RUN_TEST(test_quoted_value_strips_quotes);
    RUN_TEST(test_empty_value_is_missing_credential);
    RUN_TEST(test_only_api_id_reports_api_hash_missing);
    RUN_TEST(test_only_api_hash_reports_api_id_missing);
    RUN_TEST(test_duplicate_key_last_wins_and_warns);
    RUN_TEST(test_empty_file_is_missing_credentials);
    RUN_TEST(test_api_hash_wrong_length_rejected);
}
