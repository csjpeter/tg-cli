/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_login.c
 * @brief One-shot interactive login helper for integration tests.
 *
 * Reads credentials from ~/.config/tg-cli/test.ini, performs a full
 * Telegram login (DH + auth.sendCode + auth.signIn) and saves the resulting
 * auth_key to the session_bin path configured in test.ini (default:
 * ~/.config/tg-cli/test-session.bin).
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
    printf("Phone number: ");
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) return -1;
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
    return out[0] ? 0 : -1;
}

static int cb_get_code(void *user, char *out, size_t cap)
{
    (void)user;
    /* Always prompt interactively — this is the one-time flow. */
    printf("SMS / Telegram code: ");
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) return -1;
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
    return out[0] ? 0 : -1;
}

static int cb_get_password(void *user, char *out, size_t cap)
{
    (void)user;
    printf("2FA password: ");
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) return -1;
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
    return out[0] ? 0 : -1;
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

/* ---- Main ---- */

int main(void)
{
    memset(&g_integration_config, 0, sizeof(g_integration_config));

    if (integ_config_load(&g_integration_config) != 0) {
        char *path = integ_config_path();
        fprintf(stderr, "ERROR: config file not found: %s\n\n",
                path ? path : "~/.config/tg-cli/test.ini");
        fprintf(stderr, "Create it with at least:\n"
                        "  [integration]\n"
                        "  dc_host  = 149.154.167.40\n"
                        "  dc_id    = 0\n"
                        "  api_id   = <your test api_id>\n"
                        "  api_hash = <your test api_hash>\n"
                        "  phone    = +99966XXXXXXX\n");
        free(path);
        return 1;
    }

    if (!g_integration_config.api_id || !g_integration_config.api_id[0]) {
        fprintf(stderr, "ERROR: api_id not set in test.ini\n");
        return 1;
    }

    printf("=== tg-test-login ===\n");
    printf("DC: %s:%s  dc_id=%d\n",
           g_integration_config.dc_host ? g_integration_config.dc_host : "(env)",
           g_integration_config.dc_port ? g_integration_config.dc_port : "443",
           g_integration_config.dc_id);
    printf("Session will be saved to: %s\n\n",
           g_integration_config.session_bin
               ? g_integration_config.session_bin
               : "(default)");

    apply_dc_overrides();

    /* Use a temp HOME so app_bootstrap and session_store don't touch the
     * user's real config directory. */
    char tmpl[] = "/tmp/tg-test-login-XXXXXX";
    const char *tmp_home = mkdtemp(tmpl);
    if (!tmp_home) { perror("mkdtemp"); return 1; }

    setenv("HOME", tmp_home, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");

    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-test-login") != 0) {
        fprintf(stderr, "ERROR: app_bootstrap failed\n");
        return 1;
    }

    ApiConfig cfg;
    api_config_init(&cfg);
    if (g_integration_config.api_id)
        cfg.api_id = atoi(g_integration_config.api_id);
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

    const char *dst = g_integration_config.session_bin;
    if (!dst || !dst[0]) {
        /* Should not happen — integ_config_load sets a default */
        fprintf(stderr, "ERROR: session_bin path not set\n");
        app_shutdown(&ctx);
        return 1;
    }

    if (copy_file(session_src, dst) != 0) {
        fprintf(stderr, "ERROR: failed to copy session to %s\n", dst);
        app_shutdown(&ctx);
        return 1;
    }

    printf("Session saved to: %s\n\n", dst);
    printf("Integration tests can now be run without re-entering credentials:\n");
    printf("  ./manage.sh integration\n");

    app_shutdown(&ctx);
    return 0;
}
