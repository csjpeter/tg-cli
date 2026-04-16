#include "test_helpers.h"
#include "cache_store.h"
#include "fs_util.h"
#include "raii.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static void test_cache_basic(void) {
    char *old_home = getenv("HOME");
    setenv("HOME", "/tmp/tg-cli-cache-test-home", 1);

    const char *category = "messages";
    const char *key      = "12345/42";

    /* 1. Not cached initially */
    ASSERT(cache_exists(category, key) == 0, "cache_exists: should be 0 before save");

    {
        RAII_STRING char *loaded = cache_load(category, key);
        ASSERT(loaded == NULL, "cache_load: should return NULL before save");
    }

    /* 2. Save and verify existence */
    const char *content = "{\"text\":\"Cache Test\",\"chat_id\":12345}";
    int rc = cache_save(category, key, content, strlen(content));
    ASSERT(rc == 0, "cache_save: should return 0 on success");
    ASSERT(cache_exists(category, key) == 1, "cache_exists: should be 1 after save");

    /* 3. Load and verify content */
    {
        RAII_STRING char *loaded = cache_load(category, key);
        ASSERT(loaded != NULL, "cache_load: should not be NULL after save");
        ASSERT(strcmp(loaded, content) == 0, "cache_load: content mismatch");
    }

    /* 4. Overwrite with new content */
    const char *content2 = "{\"text\":\"Updated\",\"chat_id\":12345}";
    rc = cache_save(category, key, content2, strlen(content2));
    ASSERT(rc == 0, "cache_save: overwrite should return 0");
    {
        RAII_STRING char *loaded = cache_load(category, key);
        ASSERT(loaded != NULL, "cache_load: should not be NULL after overwrite");
        ASSERT(strcmp(loaded, content2) == 0, "cache_load: overwritten content mismatch");
    }

    /* 5. Different keys are independent */
    ASSERT(cache_exists(category, "12345/99") == 0, "cache_exists: different key should not exist");

    /* Cleanup */
    RAII_STRING char *path = cache_path(category, key);
    if (path) unlink(path);

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ---- cache_load: NULL/missing paths ---- */
static void test_cache_load_missing(void) {
    char *old_home = getenv("HOME");
    setenv("HOME", "/tmp/tg-cli-cache-missing", 1);

    /* no file exists → NULL */
    RAII_STRING char *v = cache_load("messages", "nonexistent/9999");
    ASSERT(v == NULL, "cache_load: missing file must return NULL");

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ---- cache_save: failing to create parent dir returns -1 ---- */
static void test_cache_save_mkdir_fails(void) {
    char *old_home = getenv("HOME");
    /* Create a regular file where tg-cli would place its cache dir so
     * fs_mkdir_p fails. */
    const char *bad_home = "/tmp/tg-cli-cache-bad-home";
    /* Clean slate */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/.cache/tg-cli/media", bad_home);
        (void)remove(path);
    }
    mkdir(bad_home, 0700);
    char file_in_way[256];
    snprintf(file_in_way, sizeof(file_in_way), "%s/.cache", bad_home);
    /* Make ".cache" a regular file → mkdir_p will fail */
    FILE *f = fopen(file_in_way, "w");
    if (f) { fputs("x", f); fclose(f); }

    setenv("HOME", bad_home, 1);

    int rc = cache_save("media", "key1", "data", 4);
    ASSERT(rc == -1, "cache_save: must fail when parent cannot be created");

    /* cleanup */
    unlink(file_in_way);
    rmdir(bad_home);

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ---- cache_evict_stale: removes entries not in keep-list ---- */
static void test_cache_evict_stale(void) {
    char *old_home = getenv("HOME");
    setenv("HOME", "/tmp/tg-cli-cache-evict", 1);

    /* Populate 3 entries */
    const char *cat = "messages";
    cache_save(cat, "a", "aa", 2);
    cache_save(cat, "b", "bb", 2);
    cache_save(cat, "c", "cc", 2);
    ASSERT(cache_exists(cat, "a") == 1, "a exists");
    ASSERT(cache_exists(cat, "b") == 1, "b exists");
    ASSERT(cache_exists(cat, "c") == 1, "c exists");

    /* Keep only "b" — "a" and "c" should be evicted */
    const char *keep[] = {"b"};
    cache_evict_stale(cat, keep, 1);

    ASSERT(cache_exists(cat, "a") == 0, "a evicted");
    ASSERT(cache_exists(cat, "b") == 1, "b preserved");
    ASSERT(cache_exists(cat, "c") == 0, "c evicted");

    /* cleanup */
    RAII_STRING char *pb = cache_path(cat, "b");
    if (pb) unlink(pb);

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ---- cache_evict_stale: empty keep list removes everything ---- */
static void test_cache_evict_stale_empty_keep(void) {
    char *old_home = getenv("HOME");
    setenv("HOME", "/tmp/tg-cli-cache-evict2", 1);

    const char *cat = "media";
    cache_save(cat, "x", "x", 1);
    cache_save(cat, "y", "y", 1);
    ASSERT(cache_exists(cat, "x") == 1, "x exists");

    const char *keep[] = {"__sentinel__"}; /* keep list with no matches */
    cache_evict_stale(cat, keep, 1);

    ASSERT(cache_exists(cat, "x") == 0, "x evicted");
    ASSERT(cache_exists(cat, "y") == 0, "y evicted");

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

/* ---- cache_evict_stale: nonexistent category is a no-op ---- */
static void test_cache_evict_stale_no_dir(void) {
    char *old_home = getenv("HOME");
    setenv("HOME", "/tmp/tg-cli-cache-evict-nodir", 1);

    const char *keep[] = {"a"};
    /* Directory does not exist → function should return without error */
    cache_evict_stale("nonexistent_category", keep, 1);

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
}

void test_cache_store(void) {
    RUN_TEST(test_cache_basic);
    RUN_TEST(test_cache_load_missing);
    RUN_TEST(test_cache_save_mkdir_fails);
    RUN_TEST(test_cache_evict_stale);
    RUN_TEST(test_cache_evict_stale_empty_keep);
    RUN_TEST(test_cache_evict_stale_no_dir);
}
