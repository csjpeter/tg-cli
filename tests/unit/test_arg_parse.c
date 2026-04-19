/**
 * @file test_arg_parse.c
 * @brief Unit tests for arg_parse.c.
 */

#include "test_helpers.h"
#include "arg_parse.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/* ---- Test: no arguments → interactive mode (CMD_NONE) ---- */
static void test_no_args(void) {
    char *argv[] = {"tg-cli", NULL};
    ArgResult r;
    int rc = arg_parse(1, argv, &r);
    ASSERT(rc == ARG_OK,        "no args: must return ARG_OK");
    ASSERT(r.command == CMD_NONE, "no args: command must be CMD_NONE");
    ASSERT(r.batch == 0,        "no args: batch must be 0");
    ASSERT(r.json  == 0,        "no args: json must be 0");
    ASSERT(r.quiet == 0,        "no args: quiet must be 0");
}

/* ---- Test: --help / -h ---- */
static void test_help_flag(void) {
    char *argv1[] = {"tg-cli", "--help", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv1, &r) == ARG_HELP, "--help: must return ARG_HELP");

    char *argv2[] = {"tg-cli", "-h", NULL};
    ASSERT(arg_parse(2, argv2, &r) == ARG_HELP, "-h: must return ARG_HELP");

    char *argv3[] = {"tg-cli", "help", NULL};
    ASSERT(arg_parse(2, argv3, &r) == ARG_HELP, "'help' subcommand must return ARG_HELP");
}

/* ---- Test: --version / -v ---- */
static void test_version_flag(void) {
    char *argv1[] = {"tg-cli", "--version", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv1, &r) == ARG_VERSION, "--version: must return ARG_VERSION");

    char *argv2[] = {"tg-cli", "-v", NULL};
    ASSERT(arg_parse(2, argv2, &r) == ARG_VERSION, "-v: must return ARG_VERSION");

    char *argv3[] = {"tg-cli", "version", NULL};
    ASSERT(arg_parse(2, argv3, &r) == ARG_VERSION, "'version' subcommand must return ARG_VERSION");
}

/* ---- Test: global flags set correctly ---- */
static void test_global_flags(void) {
    char *argv[] = {"tg-cli", "--batch", "--json", "--quiet", "contacts", NULL};
    ArgResult r;
    int rc = arg_parse(5, argv, &r);
    ASSERT(rc == ARG_OK,            "global flags: must return ARG_OK");
    ASSERT(r.batch == 1,            "global flags: batch must be 1");
    ASSERT(r.json  == 1,            "global flags: json must be 1");
    ASSERT(r.quiet == 1,            "global flags: quiet must be 1");
    ASSERT(r.command == CMD_CONTACTS, "global flags: command must be CMD_CONTACTS");
}

/* ---- Test: --config <path> ---- */
static void test_config_flag(void) {
    char *argv[] = {"tg-cli", "--config", "/tmp/my.ini", "contacts", NULL};
    ArgResult r;
    int rc = arg_parse(4, argv, &r);
    ASSERT(rc == ARG_OK,                          "--config: must return ARG_OK");
    ASSERT(r.config_path != NULL,                 "--config: config_path must be set");
    ASSERT(strcmp(r.config_path, "/tmp/my.ini") == 0,
           "--config: config_path must match");
}

/* ---- Test: --config without value → error ---- */
static void test_config_flag_missing_value(void) {
    char *argv[] = {"tg-cli", "--config", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "--config without value: must return ARG_ERROR");
}

/* ---- Test: dialogs (no options) ---- */
static void test_dialogs_no_options(void) {
    char *argv[] = {"tg-cli", "dialogs", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,               "dialogs: must return ARG_OK");
    ASSERT(r.command == CMD_DIALOGS,   "dialogs: command must be CMD_DIALOGS");
    ASSERT(r.limit == 20,              "dialogs: default limit must be 20");
}

/* ---- Test: dialogs --limit N ---- */
static void test_dialogs_with_limit(void) {
    char *argv[] = {"tg-cli", "dialogs", "--limit", "100", NULL};
    ArgResult r;
    int rc = arg_parse(4, argv, &r);
    ASSERT(rc == ARG_OK,            "dialogs --limit: must return ARG_OK");
    ASSERT(r.command == CMD_DIALOGS, "dialogs --limit: command must be CMD_DIALOGS");
    ASSERT(r.limit == 100,           "dialogs --limit: limit must be 100");
}

/* ---- Test: dialogs --limit missing value → error ---- */
static void test_dialogs_limit_missing(void) {
    char *argv[] = {"tg-cli", "dialogs", "--limit", NULL};
    ArgResult r;
    ASSERT(arg_parse(3, argv, &r) == ARG_ERROR,
           "dialogs --limit without value: must return ARG_ERROR");
}

/* ---- Test: history <peer> ---- */
static void test_history_basic(void) {
    char *argv[] = {"tg-cli", "history", "username123", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,             "history: must return ARG_OK");
    ASSERT(r.command == CMD_HISTORY, "history: command must be CMD_HISTORY");
    ASSERT(r.peer != NULL,           "history: peer must be set");
    ASSERT(strcmp(r.peer, "username123") == 0, "history: peer must match");
    ASSERT(r.limit  == 50,           "history: default limit must be 50");
    ASSERT(r.offset == 0,            "history: default offset must be 0");
}

/* ---- Test: history <peer> --limit N --offset M ---- */
static void test_history_with_options(void) {
    char *argv[] = {"tg-cli", "history", "@testchat",
                    "--limit", "30", "--offset", "60", NULL};
    ArgResult r;
    int rc = arg_parse(7, argv, &r);
    ASSERT(rc == ARG_OK,             "history opts: must return ARG_OK");
    ASSERT(r.command == CMD_HISTORY, "history opts: command must be CMD_HISTORY");
    ASSERT(strcmp(r.peer, "@testchat") == 0, "history opts: peer must match");
    ASSERT(r.limit  == 30,           "history opts: limit must be 30");
    ASSERT(r.offset == 60,           "history opts: offset must be 60");
}

/* ---- Test: history without peer → error ---- */
static void test_history_missing_peer(void) {
    char *argv[] = {"tg-cli", "history", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "history without peer: must return ARG_ERROR");
}

/* ---- Test: send <peer> <message> ---- */
static void test_send_basic(void) {
    char *argv[] = {"tg-cli", "send", "@user", "Hello, world!", NULL};
    ArgResult r;
    int rc = arg_parse(4, argv, &r);
    ASSERT(rc == ARG_OK,          "send: must return ARG_OK");
    ASSERT(r.command == CMD_SEND, "send: command must be CMD_SEND");
    ASSERT(strcmp(r.peer,    "@user")        == 0, "send: peer must match");
    ASSERT(strcmp(r.message, "Hello, world!") == 0, "send: message must match");
}

/* ---- Test: send without peer → error ---- */
static void test_send_missing_peer(void) {
    char *argv[] = {"tg-cli", "send", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "send without peer: must return ARG_ERROR");
}

/* ---- Test: send without message → ARG_OK, message NULL (stdin pipe path).
 *
 * Since P8-03 the send parser accepts <peer> alone and defers message
 * sourcing to tg-cli (which reads stdin when the tty is not connected).
 */
static void test_send_missing_message(void) {
    char *argv[] = {"tg-cli", "send", "@user", NULL};
    ArgResult r;
    ASSERT(arg_parse(3, argv, &r) == ARG_OK,
           "send without message: allowed for stdin pipe");
    ASSERT(r.message == NULL,
           "send without message: message stays NULL so tg-cli can read stdin");
}

/* ---- Test: search <query> (no peer) ---- */
static void test_search_query_only(void) {
    char *argv[] = {"tg-cli", "search", "hello world", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,            "search: must return ARG_OK");
    ASSERT(r.command == CMD_SEARCH, "search: command must be CMD_SEARCH");
    ASSERT(r.peer == NULL,          "search (no peer): peer must be NULL");
    ASSERT(strcmp(r.query, "hello world") == 0, "search: query must match");
}

/* ---- Test: search <peer> <query> ---- */
static void test_search_peer_and_query(void) {
    char *argv[] = {"tg-cli", "search", "@news", "breaking", NULL};
    ArgResult r;
    int rc = arg_parse(4, argv, &r);
    ASSERT(rc == ARG_OK,            "search peer+query: must return ARG_OK");
    ASSERT(r.command == CMD_SEARCH, "search peer+query: command must be CMD_SEARCH");
    ASSERT(strcmp(r.peer,  "@news")    == 0, "search peer+query: peer must match");
    ASSERT(strcmp(r.query, "breaking") == 0, "search peer+query: query must match");
}

/* ---- Test: search without query → error ---- */
static void test_search_missing_query(void) {
    char *argv[] = {"tg-cli", "search", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "search without query: must return ARG_ERROR");
}

/* ---- Test: contacts ---- */
static void test_contacts(void) {
    char *argv[] = {"tg-cli", "contacts", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,              "contacts: must return ARG_OK");
    ASSERT(r.command == CMD_CONTACTS, "contacts: command must be CMD_CONTACTS");
}

/* ---- Test: user-info <peer> ---- */
static void test_user_info(void) {
    char *argv[] = {"tg-cli", "user-info", "durov", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,               "user-info: must return ARG_OK");
    ASSERT(r.command == CMD_USER_INFO, "user-info: command must be CMD_USER_INFO");
    ASSERT(strcmp(r.peer, "durov") == 0, "user-info: peer must match");
}

/* ---- Test: user-info without peer → error ---- */
static void test_user_info_missing_peer(void) {
    char *argv[] = {"tg-cli", "user-info", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "user-info without peer: must return ARG_ERROR");
}

/* ---- Test: unknown subcommand → error ---- */
static void test_unknown_subcommand(void) {
    char *argv[] = {"tg-cli", "frobniculate", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "unknown subcommand: must return ARG_ERROR");
}

/* ---- Test: null argv/out → error ---- */
static void test_null_args(void) {
    ArgResult r;
    ASSERT(arg_parse(0, NULL, &r) == ARG_ERROR, "null argv: must return ARG_ERROR");
    ASSERT(arg_parse(1, (char *[]){NULL}, NULL)  == ARG_ERROR, "null out: must return ARG_ERROR");
}

/* ---- Test: dialogs --limit non-numeric value ---- */
static void test_dialogs_limit_non_numeric(void) {
    char *argv[] = {"tg-cli", "dialogs", "--limit", "abc", NULL};
    ArgResult r;
    ASSERT(arg_parse(4, argv, &r) == ARG_ERROR,
           "dialogs --limit abc: must return ARG_ERROR");
}

/* ---- Test: dialogs unknown option ---- */
static void test_dialogs_unknown_option(void) {
    char *argv[] = {"tg-cli", "dialogs", "--bogus", NULL};
    ArgResult r;
    ASSERT(arg_parse(3, argv, &r) == ARG_ERROR,
           "dialogs --bogus: must return ARG_ERROR");
}

/* ---- Test: history with peer starting with '-' ---- */
static void test_history_dash_peer(void) {
    char *argv[] = {"tg-cli", "history", "-notapeer", NULL};
    ArgResult r;
    ASSERT(arg_parse(3, argv, &r) == ARG_ERROR,
           "history -peer: must return ARG_ERROR");
}

/* ---- Test: history --limit missing value ---- */
static void test_history_limit_missing(void) {
    char *argv[] = {"tg-cli", "history", "@u", "--limit", NULL};
    ArgResult r;
    ASSERT(arg_parse(4, argv, &r) == ARG_ERROR,
           "history --limit w/o val: ARG_ERROR");
}

/* ---- Test: history --limit non-numeric ---- */
static void test_history_limit_non_numeric(void) {
    char *argv[] = {"tg-cli", "history", "@u", "--limit", "xyz", NULL};
    ArgResult r;
    ASSERT(arg_parse(5, argv, &r) == ARG_ERROR,
           "history --limit xyz: ARG_ERROR");
}

/* ---- Test: history --offset missing value ---- */
static void test_history_offset_missing(void) {
    char *argv[] = {"tg-cli", "history", "@u", "--offset", NULL};
    ArgResult r;
    ASSERT(arg_parse(4, argv, &r) == ARG_ERROR,
           "history --offset w/o val: ARG_ERROR");
}

/* ---- Test: history --offset non-numeric ---- */
static void test_history_offset_non_numeric(void) {
    char *argv[] = {"tg-cli", "history", "@u", "--offset", "nope", NULL};
    ArgResult r;
    ASSERT(arg_parse(5, argv, &r) == ARG_ERROR,
           "history --offset nope: ARG_ERROR");
}

/* ---- Test: history unknown option ---- */
static void test_history_unknown_option(void) {
    char *argv[] = {"tg-cli", "history", "@u", "--weird", NULL};
    ArgResult r;
    ASSERT(arg_parse(4, argv, &r) == ARG_ERROR,
           "history --weird: ARG_ERROR");
}

/* ---- Test: send with peer starting with '-' ---- */
static void test_send_dash_peer(void) {
    char *argv[] = {"tg-cli", "send", "-peer", "msg", NULL};
    ArgResult r;
    ASSERT(arg_parse(4, argv, &r) == ARG_ERROR,
           "send -peer: must return ARG_ERROR");
}

/* ---- Test: search with all-dash args ---- */
static void test_search_all_dash(void) {
    char *argv[] = {"tg-cli", "search", "-flag", NULL};
    ArgResult r;
    ASSERT(arg_parse(3, argv, &r) == ARG_ERROR,
           "search -flag: must return ARG_ERROR");
}

/* ---- Test: batch without subcommand → error ---- */
static void test_batch_no_subcommand(void) {
    char *argv[] = {"tg-cli", "--batch", NULL};
    ArgResult r;
    ASSERT(arg_parse(2, argv, &r) == ARG_ERROR,
           "--batch w/o subcommand: ARG_ERROR");
}

/* ---- Test: --json before subcommand ---- */
static void test_json_before_subcommand(void) {
    char *argv[] = {"tg-cli", "--json", "dialogs", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,             "--json before dialogs: must return ARG_OK");
    ASSERT(r.json == 1,              "--json before dialogs: json must be 1");
    ASSERT(r.command == CMD_DIALOGS, "--json before dialogs: command must be CMD_DIALOGS");
}

/* ---- Test: dialogs --archived ---- */
static void test_dialogs_archived(void) {
    char *argv[] = {"tg-cli", "dialogs", "--archived", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,             "dialogs --archived: must return ARG_OK");
    ASSERT(r.command == CMD_DIALOGS, "dialogs --archived: CMD_DIALOGS");
    ASSERT(r.archived == 1,          "dialogs --archived: archived flag set");
}

/* ---- Test: dialogs --archived --limit N (combined) ---- */
static void test_dialogs_archived_with_limit(void) {
    char *argv[] = {"tg-cli", "dialogs", "--archived", "--limit", "50", NULL};
    ArgResult r;
    int rc = arg_parse(5, argv, &r);
    ASSERT(rc == ARG_OK,             "dialogs --archived --limit: ARG_OK");
    ASSERT(r.archived == 1,          "archived flag set");
    ASSERT(r.limit == 50,            "limit=50");
}

/* ---- Test: dialogs (no --archived) → archived is 0 ---- */
static void test_dialogs_not_archived_by_default(void) {
    char *argv[] = {"tg-cli", "dialogs", NULL};
    ArgResult r;
    arg_parse(2, argv, &r);
    ASSERT(r.archived == 0, "dialogs: archived must default to 0");
}

void run_arg_parse_tests(void) {
    RUN_TEST(test_no_args);
    RUN_TEST(test_help_flag);
    RUN_TEST(test_version_flag);
    RUN_TEST(test_global_flags);
    RUN_TEST(test_config_flag);
    RUN_TEST(test_config_flag_missing_value);
    RUN_TEST(test_dialogs_no_options);
    RUN_TEST(test_dialogs_with_limit);
    RUN_TEST(test_dialogs_limit_missing);
    RUN_TEST(test_history_basic);
    RUN_TEST(test_history_with_options);
    RUN_TEST(test_history_missing_peer);
    RUN_TEST(test_send_basic);
    RUN_TEST(test_send_missing_peer);
    RUN_TEST(test_send_missing_message);
    RUN_TEST(test_search_query_only);
    RUN_TEST(test_search_peer_and_query);
    RUN_TEST(test_search_missing_query);
    RUN_TEST(test_contacts);
    RUN_TEST(test_user_info);
    RUN_TEST(test_user_info_missing_peer);
    RUN_TEST(test_unknown_subcommand);
    RUN_TEST(test_null_args);
    RUN_TEST(test_json_before_subcommand);
    RUN_TEST(test_dialogs_limit_non_numeric);
    RUN_TEST(test_dialogs_unknown_option);
    RUN_TEST(test_history_dash_peer);
    RUN_TEST(test_history_limit_missing);
    RUN_TEST(test_history_limit_non_numeric);
    RUN_TEST(test_history_offset_missing);
    RUN_TEST(test_history_offset_non_numeric);
    RUN_TEST(test_history_unknown_option);
    RUN_TEST(test_send_dash_peer);
    RUN_TEST(test_search_all_dash);
    RUN_TEST(test_batch_no_subcommand);
    RUN_TEST(test_dialogs_archived);
    RUN_TEST(test_dialogs_archived_with_limit);
    RUN_TEST(test_dialogs_not_archived_by_default);
}
