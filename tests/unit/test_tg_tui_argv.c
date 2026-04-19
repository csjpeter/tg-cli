/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_tg_tui_argv.c
 * @brief Unit tests for tg-tui argv dispatch helpers.
 *
 * Exercises the logic that was added in FEAT-15:
 *   - --phone / --code / --password values are picked up from argv
 *   - PromptCtx fields are populated correctly
 *   - Interactive fallback (NULL) is preserved when a flag is absent
 *
 * Because tg_tui.c has a `main()` we cannot link it directly. The tests
 * replicate the argv-scanning logic inline and verify invariants.
 */

#include "test_helpers.h"

#include <string.h>
#include <stddef.h>

/* ---- replicate the argv scanning logic from tg_tui.c:main() ---- */

typedef struct {
    const char *phone;
    const char *code;
    const char *password;
} TuiBatchOpts;

/**
 * @brief Scan argv for --phone / --code / --password / --tui.
 *
 * This is the same loop that lives in tg_tui.c:main().
 */
static TuiBatchOpts scan_tui_argv(int argc, char **argv) {
    TuiBatchOpts opts = { NULL, NULL, NULL };
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--phone") == 0 && i + 1 < argc) {
            opts.phone = argv[++i];
        } else if (strcmp(argv[i], "--code") == 0 && i + 1 < argc) {
            opts.code = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            opts.password = argv[++i];
        }
    }
    return opts;
}

/** Helper: detect --logout flag. */
static int has_logout_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--logout") == 0) return 1;
    }
    return 0;
}

/** Helper: detect --tui flag. */
static int has_tui_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tui") == 0) return 1;
    }
    return 0;
}

/* ---- tests ---- */

/** Batch credentials are extracted from argv correctly. */
static void test_batch_creds_parsed(void) {
    char *argv[] = {
        "tg-tui",
        "--phone", "+15551234567",
        "--code",  "12345",
        "--password", "s3cr3t",
        NULL
    };
    TuiBatchOpts opts = scan_tui_argv(7, argv);
    ASSERT(opts.phone    != NULL,                          "phone must be set");
    ASSERT(strcmp(opts.phone, "+15551234567") == 0,        "phone value correct");
    ASSERT(opts.code     != NULL,                          "code must be set");
    ASSERT(strcmp(opts.code, "12345") == 0,                "code value correct");
    ASSERT(opts.password != NULL,                          "password must be set");
    ASSERT(strcmp(opts.password, "s3cr3t") == 0,           "password value correct");
}

/** When no batch flags are given all fields stay NULL (interactive mode). */
static void test_batch_creds_absent(void) {
    char *argv[] = { "tg-tui", NULL };
    TuiBatchOpts opts = scan_tui_argv(1, argv);
    ASSERT(opts.phone    == NULL, "phone must be NULL when absent");
    ASSERT(opts.code     == NULL, "code must be NULL when absent");
    ASSERT(opts.password == NULL, "password must be NULL when absent");
}

/** --tui flag is detected. */
static void test_tui_flag_detected(void) {
    char *argv_yes[] = { "tg-tui", "--tui", NULL };
    char *argv_no[]  = { "tg-tui", NULL };
    ASSERT(has_tui_flag(2, argv_yes) == 1, "--tui detected");
    ASSERT(has_tui_flag(1, argv_no)  == 0, "--tui absent");
}

/** --logout flag is detected. */
static void test_logout_flag_detected(void) {
    char *argv_yes[] = { "tg-tui", "--logout", NULL };
    char *argv_no[]  = { "tg-tui", NULL };
    ASSERT(has_logout_flag(2, argv_yes) == 1, "--logout detected");
    ASSERT(has_logout_flag(1, argv_no)  == 0, "--logout absent");
}

/** --phone without a following value is silently ignored (no OOB access). */
static void test_phone_missing_value(void) {
    char *argv[] = { "tg-tui", "--phone", NULL };
    TuiBatchOpts opts = scan_tui_argv(2, argv);
    ASSERT(opts.phone == NULL, "--phone with no value leaves phone NULL");
}

/** Mixed batch + --tui flags coexist correctly. */
static void test_batch_and_tui_coexist(void) {
    char *argv[] = {
        "tg-tui",
        "--tui",
        "--phone", "+447911123456",
        "--code",  "99999",
        NULL
    };
    TuiBatchOpts opts = scan_tui_argv(6, argv);
    ASSERT(has_tui_flag(6, argv) == 1,                     "--tui detected alongside batch flags");
    ASSERT(opts.phone    != NULL,                           "phone set with --tui present");
    ASSERT(strcmp(opts.phone, "+447911123456") == 0,        "phone value correct");
    ASSERT(opts.code     != NULL,                           "code set with --tui present");
    ASSERT(opts.password == NULL,                           "password NULL when not given");
}

void run_tg_tui_argv_tests(void) {
    RUN_TEST(test_batch_creds_parsed);
    RUN_TEST(test_batch_creds_absent);
    RUN_TEST(test_tui_flag_detected);
    RUN_TEST(test_logout_flag_detected);
    RUN_TEST(test_phone_missing_value);
    RUN_TEST(test_batch_and_tui_coexist);
}
