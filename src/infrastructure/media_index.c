/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file infrastructure/media_index.c
 * @brief media_id → local path index stored at ~/.cache/tg-cli/media.idx
 *
 * The file is a plain-text tab-delimited table:
 *   <int64 media_id>\t<absolute path>\n
 *
 * Reads are O(n) sequential scans — the file is expected to hold at
 * most a few thousand entries in typical usage.  Writes rewrite the
 * entire file after updating the relevant entry to avoid stale lines.
 */

#include "media_index.h"
#include "platform/path.h"
#include "fs_util.h"
#include "logger.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_IDX_MAX_LINE 2048
#define MEDIA_IDX_MAX_ENTRIES 8192

/** Returns a heap-allocated path to the index file.  Caller must free. */
static char *index_file_path(void) {
    const char *cache = platform_cache_dir();
    if (!cache) return NULL;
    char *p = NULL;
    if (asprintf(&p, "%s/tg-cli/media.idx", cache) == -1) return NULL;
    return p;
}

/** Ensure parent directory of the index file exists. */
static int ensure_dir(const char *index_path) {
    RAII_STRING char *dir = strdup(index_path);
    if (!dir) return -1;
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    return fs_mkdir_p(dir, 0700);
}

int media_index_put(int64_t media_id, const char *local_path) {
    if (!local_path) return -1;

    RAII_STRING char *idx = index_file_path();
    if (!idx) return -1;
    if (ensure_dir(idx) != 0) {
        logger_log(LOG_ERROR, "media_index_put: cannot create index directory");
        return -1;
    }

    /* Read existing entries (if any). */
    char (*lines)[MEDIA_IDX_MAX_LINE] = NULL;
    int count = 0;
    int found = 0;

    {
        RAII_FILE FILE *rf = fopen(idx, "r");
        if (rf) {
            lines = calloc(MEDIA_IDX_MAX_ENTRIES,
                           sizeof(*lines));
            if (!lines) return -1;
            char buf[MEDIA_IDX_MAX_LINE];
            while (count < MEDIA_IDX_MAX_ENTRIES
                   && fgets(buf, sizeof(buf), rf)) {
                /* Strip trailing newline */
                size_t n = strlen(buf);
                while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
                    buf[--n] = '\0';
                if (n == 0) continue;

                /* Check if this line is for our media_id */
                int64_t id = 0;
                char *tab = strchr(buf, '\t');
                if (tab) {
                    *tab = '\0';
                    id = (int64_t)strtoll(buf, NULL, 10);
                    *tab = '\t';
                }
                if (id == media_id) {
                    /* Overwrite with new path */
                    snprintf(lines[count], MEDIA_IDX_MAX_LINE,
                             "%lld\t%s", (long long)media_id, local_path);
                    found = 1;
                } else {
                    snprintf(lines[count], MEDIA_IDX_MAX_LINE, "%s", buf);
                }
                count++;
            }
        } else {
            lines = calloc(MEDIA_IDX_MAX_ENTRIES, sizeof(*lines));
            if (!lines) return -1;
        }
    }

    if (!found) {
        if (count < MEDIA_IDX_MAX_ENTRIES) {
            snprintf(lines[count], MEDIA_IDX_MAX_LINE,
                     "%lld\t%s", (long long)media_id, local_path);
            count++;
        } else {
            logger_log(LOG_WARN, "media_index_put: index full (%d entries)",
                       count);
            free(lines);
            return -1;
        }
    }

    /* Write all entries back. */
    RAII_FILE FILE *wf = fopen(idx, "w");
    if (!wf) {
        logger_log(LOG_ERROR, "media_index_put: cannot open index for writing");
        free(lines);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        fprintf(wf, "%s\n", lines[i]);
    }
    free(lines);
    return 0;
}

int media_index_get(int64_t media_id, char *out_path, size_t out_cap) {
    if (!out_path || out_cap == 0) return -1;

    RAII_STRING char *idx = index_file_path();
    if (!idx) return -1;

    RAII_FILE FILE *rf = fopen(idx, "r");
    if (!rf) return 0;   /* file doesn't exist → not cached */

    char buf[MEDIA_IDX_MAX_LINE];
    while (fgets(buf, sizeof(buf), rf)) {
        char *tab = strchr(buf, '\t');
        if (!tab) continue;
        *tab = '\0';
        int64_t id = (int64_t)strtoll(buf, NULL, 10);
        if (id != media_id) { *tab = '\t'; continue; }

        /* Found — copy path (strip trailing newline). */
        char *path = tab + 1;
        size_t plen = strlen(path);
        while (plen > 0 && (path[plen-1] == '\n' || path[plen-1] == '\r'))
            path[--plen] = '\0';

        if (plen == 0) return 0;   /* empty path → treat as not cached */
        snprintf(out_path, out_cap, "%s", path);
        return 1;
    }
    return 0;
}
