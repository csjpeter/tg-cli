/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/credentials.c
 * @brief Env + INI-file api_id/api_hash loader.
 *
 * The INI parser tolerates (US-33):
 *   - CRLF line endings (\r stripped in trim).
 *   - UTF-8 BOM (EF BB BF) at file start.
 *   - Comment lines starting with `#` or `;` (ignored entirely).
 *   - Leading/trailing whitespace around both key and value.
 *   - Double-quoted values: api_hash="abcdef..." → quotes stripped.
 *   - Duplicate keys: last occurrence wins, one LOG_WARN per duplicate.
 *   - Empty values (`api_id=`) → treated as missing, targeted diagnostic.
 *
 * Partial credentials produce explicit diagnostics pointing at the wizard:
 *   - Only api_id missing  → "api_id not found ..."
 *   - Only api_hash missing → "api_hash not found ..."
 *   - Both missing         → combined message (legacy wording).
 *
 * api_hash must be a 32-char lowercase hex string; any other length is
 * rejected with a dedicated LOG_ERROR so a truncated copy-paste never
 * silently becomes the production credential.
 */

#include "app/credentials.h"

#include "logger.h"
#include "platform/path.h"
#include "raii.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_HASH_MAX     64
#define API_HASH_EXPECT  32  /* Telegram api_hash is 32 lowercase hex chars. */

static char g_api_hash_buf[API_HASH_MAX + 1];

/** Strip trailing whitespace / CR / LF in place. */
static void rtrim_inplace(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

/** Strip one pair of matched double quotes surrounding @p s, in place. */
static void unquote_inplace(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

/**
 * Reads the value of @p key from the INI at @p path into @p out.
 *
 * @return  0 on success (value copied),
 *         -1 if the file cannot be opened,
 *         -2 if the key is not present,
 *         -3 if the key exists but the value is empty.
 *
 * Emits LOG_WARN if the key appears more than once; the last occurrence
 * wins.
 */
static int read_ini_key(const char *path, const char *key,
                        char *out, size_t cap) {
    RAII_FILE FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    size_t klen = strlen(key);
    int found = 0;
    int empty_value = 0;

    /* UTF-8 BOM (EF BB BF) at file start is consumed on the first read so
     * that the first key on line 1 is still recognised. */
    int first_line = 1;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;

        if (first_line) {
            first_line = 0;
            if ((unsigned char)p[0] == 0xEF &&
                (unsigned char)p[1] == 0xBB &&
                (unsigned char)p[2] == 0xBF) {
                p += 3;
            }
        }

        /* Skip leading whitespace. */
        while (*p == ' ' || *p == '\t') p++;

        /* Blank line or comment (# or ;) — skip entirely. */
        if (*p == '\0' || *p == '\n' || *p == '\r' ||
            *p == '#'  || *p == ';') {
            continue;
        }

        if (strncmp(p, key, klen) != 0) continue;
        p += klen;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;

        /* Copy the remainder then normalise: trim tail, strip quotes. */
        size_t n = strlen(p);
        if (n >= cap) n = cap - 1;

        if (found) {
            logger_log(LOG_WARN,
                       "credentials: duplicate '%s=' in %s — using the "
                       "last occurrence", key, path);
        }

        memcpy(out, p, n);
        out[n] = '\0';
        rtrim_inplace(out);
        unquote_inplace(out);

        empty_value = (out[0] == '\0');
        found = 1;
        /* Do not early-return: keep scanning so we detect duplicates and
         * honour last-wins semantics. */
    }

    if (!found) return -2;
    if (empty_value) return -3;
    return 0;
}

/** Return 1 if @p s is a 32-char lowercase-hex string, else 0. */
static int is_valid_api_hash(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (n != API_HASH_EXPECT) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

int credentials_load(ApiConfig *out) {
    if (!out) return -1;
    api_config_init(out);

    /* -- api_id -- */
    int api_id = 0;
    const char *env_id = getenv("TG_CLI_API_ID");
    if (env_id && *env_id) {
        api_id = atoi(env_id);
    }

    /* -- api_hash from env -- */
    const char *env_hash = getenv("TG_CLI_API_HASH");
    if (env_hash && *env_hash) {
        size_t n = strlen(env_hash);
        if (n > API_HASH_MAX) n = API_HASH_MAX;
        memcpy(g_api_hash_buf, env_hash, n);
        g_api_hash_buf[n] = '\0';
    } else {
        g_api_hash_buf[0] = '\0';
    }

    /* -- Fall back to ~/.config/tg-cli/config.ini for missing values -- */
    int hash_len_rejected = 0;
    if (api_id == 0 || g_api_hash_buf[0] == '\0') {
        const char *cfg_dir = platform_config_dir();
        if (cfg_dir) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/tg-cli/config.ini", cfg_dir);

            if (api_id == 0) {
                char buf[32];
                int rc = read_ini_key(path, "api_id", buf, sizeof(buf));
                if (rc == 0) {
                    api_id = atoi(buf);
                } else if (rc == -3) {
                    logger_log(LOG_WARN,
                               "credentials: api_id is set to an empty "
                               "value in %s — ignoring", path);
                }
            }
            if (g_api_hash_buf[0] == '\0') {
                int rc = read_ini_key(path, "api_hash",
                                      g_api_hash_buf,
                                      sizeof(g_api_hash_buf));
                if (rc == -3) {
                    logger_log(LOG_WARN,
                               "credentials: api_hash is set to an empty "
                               "value in %s — ignoring", path);
                    g_api_hash_buf[0] = '\0';
                } else if (rc == 0 && !is_valid_api_hash(g_api_hash_buf)) {
                    logger_log(LOG_ERROR,
                               "credentials: api_hash in %s is not a 32-"
                               "character lowercase hex string — rejecting",
                               path);
                    g_api_hash_buf[0] = '\0';
                    hash_len_rejected = 1;
                }
            }
        }
    } else if (!is_valid_api_hash(g_api_hash_buf)) {
        /* Env-var supplied a bad-length hash. */
        logger_log(LOG_ERROR,
                   "credentials: TG_CLI_API_HASH is not a 32-character "
                   "lowercase hex string — rejecting");
        g_api_hash_buf[0] = '\0';
        hash_len_rejected = 1;
    }

    int id_missing   = (api_id == 0);
    int hash_missing = (g_api_hash_buf[0] == '\0');

    if (id_missing && hash_missing) {
        logger_log(LOG_ERROR,
                   "credentials: api_id/api_hash not found. Set "
                   "TG_CLI_API_ID and TG_CLI_API_HASH env vars, or add "
                   "api_id=/api_hash= lines to ~/.config/tg-cli/config.ini "
                   "(run `tg-cli config --wizard` to generate it).");
        return -1;
    }
    if (id_missing) {
        logger_log(LOG_ERROR,
                   "credentials: api_id not found. Set TG_CLI_API_ID or "
                   "add `api_id=...` to ~/.config/tg-cli/config.ini "
                   "(run `tg-cli config --wizard`).");
        return -1;
    }
    if (hash_missing) {
        if (hash_len_rejected) {
            logger_log(LOG_ERROR,
                       "credentials: api_hash rejected (wrong length or "
                       "non-hex). Obtain a 32-char lowercase hex hash "
                       "from https://my.telegram.org and re-run "
                       "`tg-cli config --wizard`.");
        } else {
            logger_log(LOG_ERROR,
                       "credentials: api_hash not found. Set "
                       "TG_CLI_API_HASH or add `api_hash=...` to "
                       "~/.config/tg-cli/config.ini "
                       "(run `tg-cli config --wizard`).");
        }
        return -1;
    }

    out->api_id = api_id;
    out->api_hash = g_api_hash_buf;
    return 0;
}
