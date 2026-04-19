/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#include "config_store.h"
#include "config.h"
#include "fs_util.h"
#include "platform/path.h"
#include "raii.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CONFIG_APP_DIR "tg-cli"
#define CONFIG_FILE   "config.ini"

/** @brief Trims leading and trailing whitespace from a string in-place. */
static char* trim(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

/** @brief Returns a heap-allocated path to the config file. Caller must free. */
static char* get_config_path(void) {
    const char *config_base = platform_config_dir();
    if (!config_base) return NULL;
    char *path = NULL;
    if (asprintf(&path, "%s/%s/%s", config_base, CONFIG_APP_DIR, CONFIG_FILE) == -1) {
        return NULL;
    }
    return path;
}

Config* config_load_from_store(void) {
    RAII_STRING char *path = get_config_path();
    if (!path) return NULL;

    RAII_FILE FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    Config *cfg = calloc(1, sizeof(Config));
    if (!cfg) return NULL;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            key = trim(key);
            val = trim(val);
            if (strcmp(key, "API_BASE") == 0) cfg->api_base = strdup(val);
            else if (strcmp(key, "TOKEN") == 0) cfg->token = strdup(val);
            else if (strcmp(key, "SSL_NO_VERIFY") == 0) cfg->ssl_no_verify = atoi(val);
        }
    }

    /* Set default API base if missing */
    if (!cfg->api_base) cfg->api_base = strdup("https://api.telegram.org");

    if (!cfg->token) {
        logger_log(LOG_WARN, "Config found but incomplete (missing TOKEN).");
        config_free(cfg);
        return NULL;
    }

    return cfg;
}

int config_save_to_store(const Config *cfg) {
    const char *config_base = platform_config_dir();
    if (!config_base) return -1;

    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", config_base, CONFIG_APP_DIR);

    /* Create directory with 0700 */
    if (fs_mkdir_p(dir_path, 0700) != 0) {
        logger_log(LOG_ERROR, "Failed to create config directory %s", dir_path);
        return -1;
    }

    RAII_STRING char *path = get_config_path();
    if (!path) {
        logger_log(LOG_ERROR, "Failed to determine config directory");
        return -1;
    }
    RAII_FILE FILE *fp = fopen(path, "w");
    if (!fp) {
        logger_log(LOG_ERROR, "Failed to open config file for writing: %s", path);
        return -1;
    }

    /* Set 0600 immediately */
    if (fs_ensure_permissions(path, 0600) != 0) {
        logger_log(LOG_WARN, "Failed to set strict permissions on %s", path);
    }

    fprintf(fp, "API_BASE=%s\n", cfg->api_base ? cfg->api_base : "https://api.telegram.org");
    fprintf(fp, "TOKEN=%s\n", cfg->token ? cfg->token : "");
    if (cfg->ssl_no_verify)
        fprintf(fp, "SSL_NO_VERIFY=1\n");

    logger_log(LOG_INFO, "Config saved to %s", path);
    return 0;
}
