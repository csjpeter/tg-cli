#include "test_helpers.h"
#include "cache_store.h"
#include "fs_util.h"
#include "raii.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void test_cache_store(void) {
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
