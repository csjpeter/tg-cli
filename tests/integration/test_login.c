/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_login.c
 * @brief One-shot interactive login helper for integration tests.
 *
 * Reads credentials from ~/.config/tg-cli/test.ini when available;
 * prompts interactively for any missing fields.  After successful login
 * the auth_key is saved to the session_bin path (default:
 * ~/.config/tg-cli/test-session.bin) so that subsequent
 * `./manage.sh integration` runs reuse the saved session.
 *
 * Run once before using `./manage.sh integration`:
 *   ./manage.sh test-login
 */

#include "integ_config.h"
#include "test_helpers_integration.h"

#include "app/bootstrap.h"
#include "app/dc_config.h"
#include "app/auth_flow.h"
#include "telegram_server_key.h"
#include "api_call.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Global populated by integ_config_load() */
integration_config_t g_integration_config;

/* ---- Prompt helpers ---- */

static int prompt_string(const char *prompt, char *out, size_t cap)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) return -1;
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
    return out[0] ? 0 : -1;
}

/* ---- Callbacks ---- */

static int cb_get_phone(void *user, char *out, size_t cap)
{
    (void)user;
    const char *phone = g_integration_config.phone;
    if (phone && phone[0]) {
        snprintf(out, cap, "%s", phone);
        printf("Phone: %s\n", out);
        return 0;
    }
    return prompt_string("Phone number: ", out, cap);
}

static int cb_get_code(void *user, char *out, size_t cap)
{
    (void)user;
    const char *code = g_integration_config.code;
    if (code && code[0]) {
        snprintf(out, cap, "%s", code);
        printf("Code: %s\n", out);
        return 0;
    }
    return prompt_string("SMS / Telegram code: ", out, cap);
}

static int cb_get_password(void *user, char *out, size_t cap)
{
    (void)user;
    return prompt_string("2FA password: ", out, cap);
}

/* ---- File copy ---- */

static int copy_file(const char *src, const char *dst)
{
    FILE *fs = fopen(src, "rb");
    if (!fs) { perror(src); return -1; }
    FILE *fd = fopen(dst, "wb");
    if (!fd) { perror(dst); fclose(fs); return -1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fs)) > 0)
        fwrite(buf, 1, n, fd);

    fclose(fs);
    fclose(fd);
    chmod(dst, 0600);
    return 0;
}

/* ---- DC overrides ---- */

static void apply_dc_overrides(void)
{
    const integration_config_t *c = &g_integration_config;

    if (c->rsa_pem && c->rsa_pem[0]) {
        if (telegram_server_key_set_override(c->rsa_pem) != 0)
            fprintf(stderr, "[WARN] RSA PEM override rejected — using built-in key\n");
        else
            printf("[INFO] RSA override applied, fingerprint=0x%016llx\n",
                   (unsigned long long)telegram_server_key_get_fingerprint());
    }

    if (c->dc_host && c->dc_host[0]) {
        char host[256];
        strncpy(host, c->dc_host, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        char *colon = strchr(host, ':');
        if (colon) *colon = '\0';
        for (int id = 1; id <= 5; id++)
            dc_config_set_host_override(id, host);
    }
}

/* ---- Write minimal config.ini for app_bootstrap ---- */

/**
 * Write a minimal config.ini to <tmp_home>/.config/tg-cli/config.ini
 * so that app_bootstrap finds the RSA key (required for DH handshake).
 */
static int write_tmp_config(const char *tmp_home, const char *rsa_pem)
{
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s/.config", tmp_home);
    mkdir(dir, 0700);
    snprintf(dir, sizeof(dir), "%s/.config/tg-cli", tmp_home);
    mkdir(dir, 0700);

    char path[4096];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/config.ini", tmp_home);

    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }

    fprintf(f, "[config]\n");
    fprintf(f, "rsa_pem = ");
    for (const char *p = rsa_pem; *p; p++) {
        if (*p == '\n') fputs("\\n", f);
        else            fputc(*p, f);
    }
    fputc('\n', f);
    fclose(f);
    chmod(path, 0600);
    return 0;
}

/* ---- Ensure session_bin directory exists ---- */

static void ensure_parent_dir(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = '\0';
    /* mkdir -p equivalent: walk from root */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

/* ---- Main ---- */

int main(void)
{
    memset(&g_integration_config, 0, sizeof(g_integration_config));

    /* Config file is optional — missing fields will be prompted below. */
    integ_config_load(&g_integration_config);

    /* Prompt for any mandatory fields not supplied by the config file. */
    char buf_host[256]   = {0};
    char buf_apiid[32]   = {0};
    char buf_apihash[64] = {0};

    if (!g_integration_config.dc_host || !g_integration_config.dc_host[0]) {
        if (prompt_string("DC host [149.154.167.40]: ", buf_host, sizeof(buf_host)) != 0)
            snprintf(buf_host, sizeof(buf_host), "149.154.167.40");
        g_integration_config.dc_host = buf_host;
    }

    if (!g_integration_config.api_id || !g_integration_config.api_id[0]) {
        if (prompt_string("API ID: ", buf_apiid, sizeof(buf_apiid)) != 0) {
            fprintf(stderr, "ERROR: api_id is required\n");
            return 1;
        }
        g_integration_config.api_id = buf_apiid;
    }

    if (!g_integration_config.api_hash || !g_integration_config.api_hash[0]) {
        if (prompt_string("API hash: ", buf_apihash, sizeof(buf_apihash)) != 0) {
            fprintf(stderr, "ERROR: api_hash is required\n");
            return 1;
        }
        g_integration_config.api_hash = buf_apihash;
    }

    /* RSA PEM: read from a file if not supplied by config or env. */
    char *rsa_pem_buf = NULL;
    if (!g_integration_config.rsa_pem || !g_integration_config.rsa_pem[0]) {
        char pem_path[4096] = {0};
        if (prompt_string("Path to RSA PEM file (or press Enter to skip): ",
                          pem_path, sizeof(pem_path)) == 0 && pem_path[0]) {
            FILE *pf = fopen(pem_path, "r");
            if (!pf) {
                perror(pem_path);
                return 1;
            }
            fseek(pf, 0, SEEK_END);
            long sz = ftell(pf);
            rewind(pf);
            if (sz > 0 && sz < 8192) {
                rsa_pem_buf = malloc((size_t)sz + 1);
                if (rsa_pem_buf) {
                    fread(rsa_pem_buf, 1, (size_t)sz, pf);
                    rsa_pem_buf[sz] = '\0';
                    g_integration_config.rsa_pem = rsa_pem_buf;
                }
            }
            fclose(pf);
        }
    }

    /* session_bin default is set by integ_config_load; if config was absent
     * we fall back to the canonical default path. */
    char default_session[4096] = {0};
    if (!g_integration_config.session_bin || !g_integration_config.session_bin[0]) {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(default_session, sizeof(default_session),
                 "%s/.config/tg-cli/test-session.bin", home);
        g_integration_config.session_bin = default_session;
    }

    printf("=== tg-test-login ===\n");
    printf("DC: %s:%s  dc_id=%d\n",
           g_integration_config.dc_host,
           g_integration_config.dc_port ? g_integration_config.dc_port : "443",
           g_integration_config.dc_id);
    printf("Session will be saved to: %s\n\n", g_integration_config.session_bin);

    apply_dc_overrides();

    /* Use a temp HOME so app_bootstrap and session_store don't touch the
     * user's real config directory. */
    char tmpl[] = "/tmp/tg-test-login-XXXXXX";
    const char *tmp_home = mkdtemp(tmpl);
    if (!tmp_home) { perror("mkdtemp"); free(rsa_pem_buf); return 1; }

    setenv("HOME", tmp_home, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");

    /* Write RSA key to tmp config.ini so app_bootstrap finds it. */
    const char *rsa = g_integration_config.rsa_pem;
    if (rsa && rsa[0]) {
        if (write_tmp_config(tmp_home, rsa) != 0) {
            fprintf(stderr, "ERROR: could not write tmp config.ini\n");
            free(rsa_pem_buf);
            return 1;
        }
    }

    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-test-login") != 0) {
        fprintf(stderr, "ERROR: app_bootstrap failed\n");
        free(rsa_pem_buf);
        return 1;
    }

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id       = atoi(g_integration_config.api_id);
    cfg.api_hash     = g_integration_config.api_hash;
    cfg.start_dc     = g_integration_config.dc_id;
    cfg.start_dc_set = 1;

    AuthFlowCallbacks cbs = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = cb_get_password,
        .user         = NULL,
    };

    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    printf("Connecting...\n");
    int rc = auth_flow_login(&cfg, &cbs, &t, &s, NULL);
    if (rc != 0) {
        fprintf(stderr, "ERROR: login failed\n");
        app_shutdown(&ctx);
        return 1;
    }

    printf("\nLogin successful!\n");
    transport_close(&t);

    /* Copy session from temp home to the configured session_bin path. */
    char session_src[4096];
    snprintf(session_src, sizeof(session_src),
             "%s/.config/tg-cli/session.bin", tmp_home);

    ensure_parent_dir(g_integration_config.session_bin);
    if (copy_file(session_src, g_integration_config.session_bin) != 0) {
        fprintf(stderr, "ERROR: failed to copy session to %s\n",
                g_integration_config.session_bin);
        app_shutdown(&ctx);
        return 1;
    }

    printf("Session saved to: %s\n\n", g_integration_config.session_bin);
    printf("Integration tests can now be run without re-entering credentials:\n");
    printf("  ./manage.sh integration\n");

    app_shutdown(&ctx);
    free(rsa_pem_buf);
    return 0;
}
