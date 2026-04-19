#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/**
 * @file test_helpers.h
 * @brief Minimal custom unit test assertion macros.
 */

extern int g_tests_run;
extern int g_tests_failed;
/** Optional substring filter set by the test runner from argv[1]. */
extern const char *g_test_filter;

#define ASSERT(condition, message) do { \
    g_tests_run++; \
    if (!(condition)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, message); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

/**
 * Run test_func only when no filter is set or the filter is a substring of
 * the stringified function name.
 */
#define RUN_TEST(test_func) do { \
    if (!g_test_filter || strstr(#test_func, g_test_filter)) { \
        printf("Running %s...\n", #test_func); \
        test_func(); \
    } \
} while(0)

#endif // TEST_HELPERS_H
