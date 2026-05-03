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
    if (!fp) return -1;

    char line[INTEG_LINE_MAX];
    int  in_section = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing CR/LF */
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

        if      (strcmp(key, "dc_host")     == 0)
            cfg->dc_host     = strdup(val);
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

    /* Fallback to legacy TG_TEST_* env vars for fields not set in the file. */
    if (!cfg->dc_host   && getenv("TG_TEST_DC_HOST"))
        cfg->dc_host   = strdup(getenv("TG_TEST_DC_HOST"));
    if (!cfg->api_id    && getenv("TG_TEST_API_ID"))
        cfg->api_id    = strdup(getenv("TG_TEST_API_ID"));
    if (!cfg->api_hash  && getenv("TG_TEST_API_HASH"))
        cfg->api_hash  = strdup(getenv("TG_TEST_API_HASH"));
    if (!cfg->phone     && getenv("TG_TEST_PHONE"))
        cfg->phone     = strdup(getenv("TG_TEST_PHONE"));
    if (!cfg->rsa_pem   && getenv("TG_TEST_RSA_PEM"))
        cfg->rsa_pem   = unescape_nl(getenv("TG_TEST_RSA_PEM"));

    /* Apply defaults for fields not present in the file. */
    if (!cfg->dc_port)
        cfg->dc_port = strdup("443");

    /* Default session_bin: ~/.config/tg-cli/test-session.bin */
    if (!cfg->session_bin) {
        const char *home = getenv("HOME");
        if (home)
            asprintf(&cfg->session_bin,
                     "%s/.config/tg-cli/test-session.bin", home);
    }

    return 0;
}
