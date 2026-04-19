/**
 * @file test_media_index.c
 * @brief Unit tests for media_index_put / media_index_get (FEAT-07).
 */

#include "test_helpers.h"
#include "infrastructure/media_index.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- helpers ---- */

static void set_test_home(const char *home) {
    setenv("HOME", home, 1);
}

/* ---- tests ---- */

/** Basic round-trip: put then get returns the path. */
static void test_media_index_basic(void) {
    set_test_home("/tmp/tg-cli-midx-test1");

    int rc = media_index_put(12345678LL, "/tmp/photo-12345678.jpg");
    ASSERT(rc == 0, "media_index_put: must succeed");

    char path[512] = {0};
    int found = media_index_get(12345678LL, path, sizeof(path));
    ASSERT(found == 1, "media_index_get: must find the entry");
    ASSERT(strcmp(path, "/tmp/photo-12345678.jpg") == 0,
           "media_index_get: path must match");
}

/** Looking up an id that was never stored returns 0. */
static void test_media_index_miss(void) {
    set_test_home("/tmp/tg-cli-midx-test2");

    char path[512] = {0};
    int found = media_index_get(999999999LL, path, sizeof(path));
    ASSERT(found == 0, "media_index_get: miss returns 0");
    ASSERT(path[0] == '\0', "media_index_get: output buffer empty on miss");
}

/** Overwriting an entry updates the stored path. */
static void test_media_index_overwrite(void) {
    set_test_home("/tmp/tg-cli-midx-test3");

    media_index_put(42LL, "/old/path.jpg");

    int rc = media_index_put(42LL, "/new/path.jpg");
    ASSERT(rc == 0, "overwrite must succeed");

    char path[512] = {0};
    int found = media_index_get(42LL, path, sizeof(path));
    ASSERT(found == 1, "entry found after overwrite");
    ASSERT(strcmp(path, "/new/path.jpg") == 0, "overwritten path returned");
}

/** Multiple entries coexist and are individually addressable. */
static void test_media_index_multiple(void) {
    set_test_home("/tmp/tg-cli-midx-test4");

    media_index_put(1LL, "/cache/photo-1.jpg");
    media_index_put(2LL, "/cache/photo-2.jpg");
    media_index_put(3LL, "/cache/doc-3.pdf");

    char p1[512]={0}, p2[512]={0}, p3[512]={0};
    ASSERT(media_index_get(1LL, p1, sizeof(p1)) == 1, "id=1 found");
    ASSERT(media_index_get(2LL, p2, sizeof(p2)) == 1, "id=2 found");
    ASSERT(media_index_get(3LL, p3, sizeof(p3)) == 1, "id=3 found");
    ASSERT(strcmp(p1, "/cache/photo-1.jpg") == 0, "path 1 correct");
    ASSERT(strcmp(p2, "/cache/photo-2.jpg") == 0, "path 2 correct");
    ASSERT(strcmp(p3, "/cache/doc-3.pdf")   == 0, "path 3 correct");

    char pm[512] = {0};
    ASSERT(media_index_get(99LL, pm, sizeof(pm)) == 0, "non-existent id=99 miss");
}

/** Null path to media_index_put is rejected. */
static void test_media_index_null_path(void) {
    set_test_home("/tmp/tg-cli-midx-test5");
    ASSERT(media_index_put(1LL, NULL) == -1, "null path rejected");
}

/** media_index_get with null output buffer returns -1. */
static void test_media_index_null_out(void) {
    set_test_home("/tmp/tg-cli-midx-test6");
    ASSERT(media_index_get(1LL, NULL, 0) == -1, "null out_path rejected");
}

void run_media_index_tests(void) {
    RUN_TEST(test_media_index_basic);
    RUN_TEST(test_media_index_miss);
    RUN_TEST(test_media_index_overwrite);
    RUN_TEST(test_media_index_multiple);
    RUN_TEST(test_media_index_null_path);
    RUN_TEST(test_media_index_null_out);
}
