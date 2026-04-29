/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/dc_config.c
 * @brief Hardcoded fallback DC endpoint table with runtime overrides (FEAT-38).
 */

#include "app/dc_config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const DcEndpoint ENDPOINTS[] = {
    { 1, "149.154.175.50",  443 },
    { 2, "149.154.167.50",  443 },
    { 3, "149.154.175.100", 443 },
    { 4, "149.154.167.91",  443 },
    { 5, "91.108.56.130",   443 },
};

#define ENDPOINT_COUNT (sizeof(ENDPOINTS) / sizeof(ENDPOINTS[0]))
#define DC_ID_MAX 5

/**
 * @brief Per-process redirect used by tests.
 *
 * When TG_CLI_DC_HOST and TG_CLI_DC_PORT are set in the environment every
 * dc_lookup() call returns a synthetic endpoint pointing at those
 * coordinates.  This lets the PTY-based functional tests redirect the
 * production binary to a local stub server without modifying the binary or
 * the DC table.
 */
static DcEndpoint g_env_override;

/**
 * FEAT-38: Per-DC host overrides from config.ini.
 * Index 0 = DC1 … index 4 = DC5.  NULL means use the compiled-in default.
 */
static char *g_host_overrides[DC_ID_MAX]; /* heap-allocated or NULL */
static DcEndpoint g_config_override;      /* scratch space for the returned value */

void dc_config_set_host_override(int dc_id, const char *host) {
    if (dc_id < 1 || dc_id > DC_ID_MAX) return;
    int idx = dc_id - 1;

    free(g_host_overrides[idx]);
    g_host_overrides[idx] = NULL;

    if (host && *host) {
        g_host_overrides[idx] = strdup(host);
    }
}

const DcEndpoint *dc_lookup(int dc_id) {
    /* Environment redirect takes highest priority (used by integration tests). */
    const char *env_host = getenv("TG_CLI_DC_HOST");
    const char *env_port = getenv("TG_CLI_DC_PORT");
    if (env_host && *env_host && env_port && *env_port) {
        g_env_override.id   = dc_id;
        g_env_override.host = env_host;
        g_env_override.port = atoi(env_port);
        return &g_env_override;
    }

    /* config.ini override (FEAT-38) — applies only if the DC id is valid. */
    if (dc_id >= 1 && dc_id <= DC_ID_MAX && g_host_overrides[dc_id - 1]) {
        /* Find the built-in entry to inherit the port. */
        for (size_t i = 0; i < ENDPOINT_COUNT; i++) {
            if (ENDPOINTS[i].id == dc_id) {
                g_config_override.id   = dc_id;
                g_config_override.host = g_host_overrides[dc_id - 1];
                g_config_override.port = ENDPOINTS[i].port;
                return &g_config_override;
            }
        }
    }

    /* Fall back to compiled-in table. */
    for (size_t i = 0; i < ENDPOINT_COUNT; i++) {
        if (ENDPOINTS[i].id == dc_id) return &ENDPOINTS[i];
    }
    return NULL;
}
