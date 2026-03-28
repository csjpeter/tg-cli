#include "cache_store.h"
#include "fs_util.h"
#include "platform/path.h"
#include "raii.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/** @brief Returns a heap-allocated path for a cache entry. Caller must free. */
char *cache_path(const char *category, const char *key) {
    const char *cache_base = platform_cache_dir();
    if (!cache_base) return NULL;
    char *path = NULL;
    if (asprintf(&path, "%s/tg-cli/%s/%s",
                 cache_base, category, key) == -1)
        return NULL;
    return path;
}

int cache_exists(const char *category, const char *key) {
    RAII_STRING char *path = cache_path(category, key);
    if (!path) return 0;
    RAII_FILE FILE *fp = fopen(path, "r");
    return fp != NULL;
}

int cache_save(const char *category, const char *key,
               const char *content, size_t len) {
    const char *cache_base = platform_cache_dir();
    if (!cache_base) return -1;

    /* Build directory path: ~/.cache/tg-cli/<category>/
     * key may contain slashes (e.g. "chat_id/msg_id") — create parent dirs. */
    RAII_STRING char *full = cache_path(category, key);
    if (!full) return -1;

    /* Find last '/' to isolate directory portion */
    RAII_STRING char *dir = strdup(full);
    if (!dir) return -1;
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';

    if (fs_mkdir_p(dir, 0700) != 0) {
        logger_log(LOG_ERROR, "Failed to create cache directory %s", dir);
        return -1;
    }

    RAII_FILE FILE *fp = fopen(full, "w");
    if (!fp) {
        logger_log(LOG_ERROR, "Failed to open cache file for writing: %s", full);
        return -1;
    }

    if (fwrite(content, 1, len, fp) != len) {
        logger_log(LOG_ERROR, "Failed to write cache file: %s", full);
        return -1;
    }

    logger_log(LOG_DEBUG, "Cached %s/%s (%zu bytes)", category, key, len);
    return 0;
}

/* ── Shared file I/O helper ──────────────────────────────────────────── */

static char *load_file(const char *path) {
    RAII_FILE FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) return NULL;
    long size = ftell(fp);
    if (size <= 0) return NULL;
    rewind(fp);
    char *buf = malloc((size_t)size + 1);
    if (!buf) return NULL;
    if ((long)fread(buf, 1, (size_t)size, fp) != size) { free(buf); return NULL; }
    buf[size] = '\0';
    return buf;
}

char *cache_load(const char *category, const char *key) {
    RAII_STRING char *path = cache_path(category, key);
    if (!path) return NULL;
    return load_file(path);
}

/* ── Stale entry eviction ────────────────────────────────────────────── */

static int cmp_str_evict(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void cache_evict_stale(const char *category,
                       const char **keep_keys, int keep_count) {
    const char *cache_base = platform_cache_dir();
    if (!cache_base) return;

    RAII_STRING char *dir = NULL;
    if (asprintf(&dir, "%s/tg-cli/%s", cache_base, category) == -1)
        return;

    /* Sort a local copy of keys for bsearch */
    const char **sorted = malloc((size_t)keep_count * sizeof(char *));
    if (!sorted) return;
    memcpy(sorted, keep_keys, (size_t)keep_count * sizeof(char *));
    qsort(sorted, (size_t)keep_count, sizeof(char *), cmp_str_evict);

    RAII_DIR DIR *d = opendir(dir);
    if (!d) { free(sorted); return; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;  /* skip . and .. */
        if (!bsearch(&name, sorted, (size_t)keep_count,
                     sizeof(char *), cmp_str_evict)) {
            RAII_STRING char *path = NULL;
            if (asprintf(&path, "%s/%s", dir, name) != -1) {
                remove(path);
                logger_log(LOG_DEBUG, "Evicted stale cache entry: %s/%s",
                           category, name);
            }
        }
    }
    free(sorted);
}
