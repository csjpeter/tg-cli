/**
 * @file test_watch_json.c
 * @brief Unit tests for FEAT-04: --json flag on `watch` subcommand.
 *
 * Tests cover:
 *  1. Argument parsing: `watch --json` sets args.json = 1.
 *  2. JSON string escaping via json_escape_str().
 */

#include "test_helpers.h"
#include "arg_parse.h"
#include "json_util.h"

#include <string.h>
#include <stdlib.h>

/* ---- Test: `--json watch` (global flag before subcommand) ---- */
static void test_watch_json_global_flag(void) {
    char *argv[] = {"tg-cli", "--json", "watch", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,           "--json watch: must return ARG_OK");
    ASSERT(r.command == CMD_WATCH, "--json watch: command must be CMD_WATCH");
    ASSERT(r.json == 1,            "--json watch: json flag must be 1");
}

/* ---- Test: `watch` without --json keeps json=0 ---- */
static void test_watch_no_json_flag(void) {
    char *argv[] = {"tg-cli", "watch", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,           "watch (no --json): must return ARG_OK");
    ASSERT(r.command == CMD_WATCH, "watch (no --json): command must be CMD_WATCH");
    ASSERT(r.json == 0,            "watch (no --json): json flag must be 0");
}

/* ---- Test: `--json watch --peers X --interval 5` combined ---- */
static void test_watch_json_with_peers_and_interval(void) {
    char *argv[] = {"tg-cli", "--json", "watch", "--peers", "@news",
                    "--interval", "10", NULL};
    ArgResult r;
    int rc = arg_parse(7, argv, &r);
    ASSERT(rc == ARG_OK,                     "watch json+peers+interval: ARG_OK");
    ASSERT(r.command == CMD_WATCH,           "watch json+peers+interval: CMD_WATCH");
    ASSERT(r.json == 1,                      "watch json+peers+interval: json=1");
    ASSERT(r.watch_interval == 10,           "watch json+peers+interval: interval=10");
    ASSERT(r.watch_peers != NULL,                   "watch json+peers+interval: watch_peers set");
    ASSERT(strcmp(r.watch_peers, "@news") == 0,     "watch json+peers+interval: watch_peers=@news");
}

/* ---- Test: json_escape_str — plain ASCII passes through unchanged ---- */
static void test_json_escape_plain(void) {
    char buf[64];
    size_t n = json_escape_str(buf, sizeof(buf), "hello world");
    ASSERT(strcmp(buf, "hello world") == 0, "json_escape plain: output matches");
    ASSERT(n == 11,                          "json_escape plain: length correct");
}

/* ---- Test: json_escape_str — double-quote and backslash are escaped ---- */
static void test_json_escape_special_chars(void) {
    char buf[64];
    /* Input: say "hi"\path  →  say \"hi\"\\path */
    json_escape_str(buf, sizeof(buf), "say \"hi\"\\path");
    ASSERT(strcmp(buf, "say \\\"hi\\\"\\\\path") == 0,
           "json_escape special: quotes and backslashes escaped");
}

/* ---- Test: json_escape_str — control characters use shorthand/unicode ---- */
static void test_json_escape_control_chars(void) {
    char buf[64];
    /* newline, tab, carriage-return */
    json_escape_str(buf, sizeof(buf), "a\nb\tc\r");
    ASSERT(strcmp(buf, "a\\nb\\tc\\r") == 0,
           "json_escape control: \\n \\t \\r expanded");
}

/* ---- Test: json_escape_str — other C0 control characters → \\uXXXX ---- */
static void test_json_escape_c0_unicode(void) {
    char buf[32];
    /* ASCII 0x01 (SOH) and 0x1f (US) */
    char input[3] = {'\x01', '\x1f', '\0'};
    json_escape_str(buf, sizeof(buf), input);
    ASSERT(strcmp(buf, "\\u0001\\u001f") == 0,
           "json_escape c0: \\u0001 and \\u001f");
}

/* ---- Test: json_escape_str — UTF-8 multibyte passes through ---- */
static void test_json_escape_utf8_passthrough(void) {
    char buf[64];
    /* UTF-8 encoded em-dash U+2014: 0xE2 0x80 0x94 */
    const char *input = "\xe2\x80\x94";
    json_escape_str(buf, sizeof(buf), input);
    ASSERT(strcmp(buf, "\xe2\x80\x94") == 0,
           "json_escape utf8: high bytes pass through");
}

/* ---- Test: json_escape_str — NULL input treated as empty string ---- */
static void test_json_escape_null_input(void) {
    char buf[8];
    size_t n = json_escape_str(buf, sizeof(buf), NULL);
    ASSERT(n == 0,             "json_escape NULL: length is 0");
    ASSERT(buf[0] == '\0',     "json_escape NULL: NUL-terminated empty string");
}

/* ---- Test: json_escape_str — output truncated when buffer too small ---- */
static void test_json_escape_truncation(void) {
    char buf[5]; /* only room for 4 chars + NUL */
    size_t n = json_escape_str(buf, sizeof(buf), "abcdefgh");
    /* Must be NUL-terminated within the 5-byte buffer. */
    ASSERT(buf[4] == '\0',  "json_escape trunc: NUL-terminated");
    ASSERT(n > 4,           "json_escape trunc: returns true length (>= cap)");
    (void)n;
}

void run_watch_json_tests(void) {
    RUN_TEST(test_watch_json_global_flag);
    RUN_TEST(test_watch_no_json_flag);
    RUN_TEST(test_watch_json_with_peers_and_interval);
    RUN_TEST(test_json_escape_plain);
    RUN_TEST(test_json_escape_special_chars);
    RUN_TEST(test_json_escape_control_chars);
    RUN_TEST(test_json_escape_c0_unicode);
    RUN_TEST(test_json_escape_utf8_passthrough);
    RUN_TEST(test_json_escape_null_input);
    RUN_TEST(test_json_escape_truncation);
}
