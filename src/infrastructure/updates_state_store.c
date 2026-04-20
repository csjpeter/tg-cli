/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file infrastructure/updates_state_store.c
 * @brief Persist UpdatesState to ~/.cache/tg-cli/updates.state (INI format).
 */

#include "updates_state_store.h"
#include "fs_util.h"
#include "logger.h"
#include "platform/path.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/** Maximum path length used internally. */
#define STATE_PATH_MAX 2048

/** Build the full path to the state file into @p buf (size STATE_PATH_MAX). */
static int build_state_path(char *buf) {
    const char *cache_base = platform_cache_dir();
    if (!cache_base) return -1;
    int n = snprintf(buf, STATE_PATH_MAX, "%s/tg-cli/updates.state", cache_base);
    if (n <= 0 || n >= STATE_PATH_MAX) return -1;
    return 0;
}

int updates_state_load(UpdatesState *out) {
    if (!out) return -1;

    char path[STATE_PATH_MAX];
    if (build_state_path(path) != 0) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Missing file is not an error — caller falls back to getState. */
        return -1;
    }

    UpdatesState tmp = {0};
    char line[256];
    int fields = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        char key[64];
        long long val;
        if (sscanf(line, "%63[^=]=%lld", key, &val) != 2) {
            fclose(fp);
            logger_log(LOG_WARN, "updates_state_load: malformed line: %s", line);
            return -2;
        }

        if (strcmp(key, "pts") == 0)  { tmp.pts  = (int32_t)val; fields++; }
        else if (strcmp(key, "qts") == 0)  { tmp.qts  = (int32_t)val; fields++; }
        else if (strcmp(key, "date") == 0) { tmp.date = (int64_t)val; fields++; }
        else if (strcmp(key, "seq") == 0)  { tmp.seq  = (int32_t)val; fields++; }
        /* unread_count is informational only — not required. */
        else if (strcmp(key, "unread_count") == 0) {
            tmp.unread_count = (int32_t)val;
        }
    }

    fclose(fp);

    /* Require at least pts/qts/date/seq to be present. */
    if (fields < 4) {
        logger_log(LOG_WARN, "updates_state_load: incomplete state file (%d/4 fields)", fields);
        return -2;
    }

    *out = tmp;
    return 0;
}

int updates_state_save(const UpdatesState *st) {
    if (!st) return -1;

    char path[STATE_PATH_MAX];
    if (build_state_path(path) != 0) return -1;

    /* Ensure the directory exists with mode 0700. */
    char dir[STATE_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    if (fs_mkdir_p(dir, 0700) != 0) {
        logger_log(LOG_ERROR, "updates_state_save: cannot create dir %s", dir);
        return -1;
    }

    /* Write to a temp file then rename for atomicity.
     * ".tmp" suffix is 4 chars; allocate extra room to silence -Wformat-truncation. */
    char tmp_path[STATE_PATH_MAX + 8];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        logger_log(LOG_ERROR, "updates_state_save: fopen %s: %s",
                   tmp_path, strerror(errno));
        return -1;
    }

    /* Set permissions to 0600 before writing any data. */
    if (fchmod(fileno(fp), 0600) != 0) {
        logger_log(LOG_WARN, "updates_state_save: fchmod failed: %s", strerror(errno));
        /* Non-fatal — continue. */
    }

    fprintf(fp, "# tg-cli updates state — do not edit by hand\n");
    fprintf(fp, "pts=%d\n",          (int)st->pts);
    fprintf(fp, "qts=%d\n",          (int)st->qts);
    fprintf(fp, "date=%lld\n",        (long long)st->date);
    fprintf(fp, "seq=%d\n",          (int)st->seq);
    fprintf(fp, "unread_count=%d\n", (int)st->unread_count);

    if (fclose(fp) != 0) {
        logger_log(LOG_ERROR, "updates_state_save: fclose failed: %s", strerror(errno));
        return -1;
    }

    if (rename(tmp_path, path) != 0) {
        logger_log(LOG_ERROR, "updates_state_save: rename failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}
