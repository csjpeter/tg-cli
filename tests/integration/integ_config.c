/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

#include "integ_config.h"
#include "test_helpers_integration.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lines up to 8 KiB to accommodate a full RSA PEM on one line. */
#define INTEG_LINE_MAX 8192

/* ---- String helpers ---- */

static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

static void rtrim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

/* Convert literal \n sequences → actual newlines. Caller frees result. */
static char *unescape_nl(const char *src) {
    size_t len = strlen(src);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len && src[i + 1] == 'n') {
            *p++ = '\n';
            i++;
        } else {
            *p++ = src[i];
        }
    }
    *p = '\0';
    return out;
}

/* Expand leading ~/ to $HOME/. Caller frees result. */
static char *expand_home(const char *path) {
    if (path[0] != '~' || path[1] != '/') return strdup(path);
    const char *home = getenv("HOME");
    if (!home) return strdup(path);
    char *result = NULL;
    if (asprintf(&result, "%s%s", home, path + 1) < 0) return NULL;
    return result;
}

/* ---- Public API ---- */

char *integ_config_path(void) {
    const char *home = getenv("HOME");
    if (!home) return strdup("~/.config/tg-cli/test.ini");
    char *p = NULL;
    if (asprintf(&p, "%s/.config/tg-cli/test.ini", home) < 0) return NULL;
    return p;
}

int integ_config_load(integration_config_t *cfg) {
    char *path = integ_config_path();
    if (!path) return -1;

    FILE *fp = fopen(path, "r");
    free(path);
    int file_found = (fp != NULL);

    if (fp) {
        char line[INTEG_LINE_MAX];
        int  in_section = 0;

        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';

            char *p = ltrim(line);
            if (!*p || *p == '#' || *p == ';') continue;

            if (*p == '[') {
                char *end = strchr(p, ']');
                if (end) *end = '\0';
                in_section = (strcmp(p + 1, "integration") == 0);
                continue;
            }
            if (!in_section) continue;

            char *eq = strchr(p, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = p;
            char *val = ltrim(eq + 1);
            rtrim(key);
            rtrim(val);

            if      (strcmp(key, "dc_host")     == 0) {
                char *colon = strchr(val, ':');
                if (colon && !cfg->dc_port) {
                    cfg->dc_port = strdup(colon + 1);
                    *colon = '\0';
                }
                cfg->dc_host = strdup(val);
        }
            else if (strcmp(key, "dc_port")     == 0)
                cfg->dc_port     = strdup(val);
            else if (strcmp(key, "dc_id")       == 0)
                cfg->dc_id       = atoi(val);
            else if (strcmp(key, "api_id")      == 0)
                cfg->api_id      = strdup(val);
            else if (strcmp(key, "api_hash")    == 0)
                cfg->api_hash    = strdup(val);
            else if (strcmp(key, "phone")       == 0)
                cfg->phone       = strdup(val);
            else if (strcmp(key, "code")        == 0)
                cfg->code        = strdup(val);
            else if (strcmp(key, "rsa_pem")     == 0)
                cfg->rsa_pem     = unescape_nl(val);
            else if (strcmp(key, "session_bin") == 0)
                cfg->session_bin = expand_home(val);
        }

        fclose(fp);
    }

    /* Fallback to legacy TG_TEST_* env vars for any field not set in the file.
     * This runs unconditionally so env vars work even without a config file. */
    if (!cfg->dc_host && getenv("TG_TEST_DC_HOST")) {
        char *h = strdup(getenv("TG_TEST_DC_HOST"));
        if (h) {
            char *colon = strchr(h, ':');
            if (colon && !cfg->dc_port) {
                cfg->dc_port = strdup(colon + 1);
                *colon = '\0';
            }
            cfg->dc_host = h;
        }
    }
    if (!cfg->api_id    && getenv("TG_TEST_API_ID"))
        cfg->api_id    = strdup(getenv("TG_TEST_API_ID"));
    if (!cfg->api_hash  && getenv("TG_TEST_API_HASH"))
        cfg->api_hash  = strdup(getenv("TG_TEST_API_HASH"));
    if (!cfg->rsa_pem   && getenv("TG_TEST_RSA_PEM"))
        cfg->rsa_pem   = unescape_nl(getenv("TG_TEST_RSA_PEM"));
    if (!cfg->code      && getenv("TG_TEST_CODE"))
        cfg->code      = strdup(getenv("TG_TEST_CODE"));

    /* Default to Telegram test DC 2 (149.154.167.40) when not configured.
     * dc_id=0 is treated as "not set" — the actual test DC is 2. */
    if (!cfg->dc_host)
        cfg->dc_host = strdup("149.154.167.40");
    if (cfg->dc_id == 0)
        cfg->dc_id = 2;

    /* Default test phone for Telegram test DC 2. */
    if (!cfg->phone)
        cfg->phone = strdup("+99962123456");

    /* Telegram test DC: +9996XXXXXXXX numbers always accept code 12345. */
    if (!cfg->code && cfg->phone && strncmp(cfg->phone, "+9996", 5) == 0)
        cfg->code = strdup("12345");

    /* Apply defaults for fields not present anywhere. */
    if (!cfg->dc_port)
        cfg->dc_port = strdup("443");

    if (!cfg->session_bin) {
        const char *home = getenv("HOME");
        if (home)
            asprintf(&cfg->session_bin,
                     "%s/.config/tg-cli/test-session.bin", home);
    }

    return file_found ? 0 : -1;
}
