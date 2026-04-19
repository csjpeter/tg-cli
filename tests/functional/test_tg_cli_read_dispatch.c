/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_tg_cli_read_dispatch.c
 * @brief Verify that arg_parse recognises all read subcommands that tg-cli
 *        now dispatches (FEAT-37 refactor — tg-cli exposes every command).
 *
 * These are parser-level tests only; no RPC is performed.
 */

#include "test_helpers.h"
#include "arg_parse.h"

#include <string.h>

static void test_dispatch_me(void) {
    char *argv[] = {"tg-cli", "me", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,        "me: ARG_OK");
    ASSERT(r.command == CMD_ME, "me: CMD_ME");
}

static void test_dispatch_self(void) {
    char *argv[] = {"tg-cli", "self", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,        "self: ARG_OK");
    ASSERT(r.command == CMD_ME, "self: CMD_ME");
}

static void test_dispatch_dialogs(void) {
    char *argv[] = {"tg-cli", "dialogs", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,               "dialogs: ARG_OK");
    ASSERT(r.command == CMD_DIALOGS,   "dialogs: CMD_DIALOGS");
}

static void test_dispatch_history(void) {
    char *argv[] = {"tg-cli", "history", "@peer", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,               "history: ARG_OK");
    ASSERT(r.command == CMD_HISTORY,   "history: CMD_HISTORY");
    ASSERT(strcmp(r.peer, "@peer") == 0, "history: peer set");
}

static void test_dispatch_search(void) {
    char *argv[] = {"tg-cli", "search", "hello", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,               "search: ARG_OK");
    ASSERT(r.command == CMD_SEARCH,    "search: CMD_SEARCH");
}

static void test_dispatch_contacts(void) {
    char *argv[] = {"tg-cli", "contacts", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,               "contacts: ARG_OK");
    ASSERT(r.command == CMD_CONTACTS,  "contacts: CMD_CONTACTS");
}

static void test_dispatch_user_info(void) {
    char *argv[] = {"tg-cli", "user-info", "@durov", NULL};
    ArgResult r;
    int rc = arg_parse(3, argv, &r);
    ASSERT(rc == ARG_OK,               "user-info: ARG_OK");
    ASSERT(r.command == CMD_USER_INFO, "user-info: CMD_USER_INFO");
    ASSERT(strcmp(r.peer, "@durov") == 0, "user-info: peer set");
}

static void test_dispatch_watch(void) {
    char *argv[] = {"tg-cli", "watch", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,               "watch: ARG_OK");
    ASSERT(r.command == CMD_WATCH,     "watch: CMD_WATCH");
}

static void test_dispatch_download(void) {
    char *argv[] = {"tg-cli", "download", "@peer", "42", NULL};
    ArgResult r;
    int rc = arg_parse(4, argv, &r);
    ASSERT(rc == ARG_OK,               "download: ARG_OK");
    ASSERT(r.command == CMD_DOWNLOAD,  "download: CMD_DOWNLOAD");
    ASSERT(r.msg_id == 42,             "download: msg_id 42");
}

static void test_dispatch_login(void) {
    char *argv[] = {"tg-cli", "login", NULL};
    ArgResult r;
    int rc = arg_parse(2, argv, &r);
    ASSERT(rc == ARG_OK,               "login: ARG_OK");
    ASSERT(r.command == CMD_LOGIN,     "login: CMD_LOGIN");
    ASSERT(r.api_id_str  == NULL,      "login: api_id_str NULL");
    ASSERT(r.api_hash_str == NULL,     "login: api_hash_str NULL");
    ASSERT(r.force == 0,               "login: force 0");
}

static void test_dispatch_login_batch(void) {
    char *argv[] = {"tg-cli", "login",
                    "--api-id",   "12345",
                    "--api-hash", "deadbeefdeadbeefdeadbeefdeadbeef",
                    "--force",    NULL};
    ArgResult r;
    int rc = arg_parse(7, argv, &r);
    ASSERT(rc == ARG_OK,               "login batch: ARG_OK");
    ASSERT(r.command == CMD_LOGIN,     "login batch: CMD_LOGIN");
    ASSERT(strcmp(r.api_id_str,  "12345")                             == 0,
           "login batch: api_id_str");
    ASSERT(strcmp(r.api_hash_str, "deadbeefdeadbeefdeadbeefdeadbeef") == 0,
           "login batch: api_hash_str");
    ASSERT(r.force == 1,               "login batch: force 1");
}

void run_tg_cli_read_dispatch_tests(void) {
    RUN_TEST(test_dispatch_me);
    RUN_TEST(test_dispatch_self);
    RUN_TEST(test_dispatch_dialogs);
    RUN_TEST(test_dispatch_history);
    RUN_TEST(test_dispatch_search);
    RUN_TEST(test_dispatch_contacts);
    RUN_TEST(test_dispatch_user_info);
    RUN_TEST(test_dispatch_watch);
    RUN_TEST(test_dispatch_download);
    RUN_TEST(test_dispatch_login);
    RUN_TEST(test_dispatch_login_batch);
}
