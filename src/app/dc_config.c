/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/dc_config.c
 * @brief Hardcoded fallback DC endpoint table.
 */

#include "app/dc_config.h"

#include <stddef.h>
#include <stdlib.h>

static const DcEndpoint ENDPOINTS[] = {
    { 1, "149.154.175.50",  443 },
    { 2, "149.154.167.50",  443 },
    { 3, "149.154.175.100", 443 },
    { 4, "149.154.167.91",  443 },
    { 5, "91.108.56.130",   443 },
};

#define ENDPOINT_COUNT (sizeof(ENDPOINTS) / sizeof(ENDPOINTS[0]))

/**
 * @brief Per-process redirect used by tests.
 *
 * When TG_CLI_DC_HOST and TG_CLI_DC_PORT are set in the environment every
 * dc_lookup() call returns a synthetic endpoint pointing at those
 * coordinates.  This lets the PTY-based functional tests redirect the
 * production binary to a local stub server without modifying the binary or
 * the DC table.
 */
static DcEndpoint g_override;

const DcEndpoint *dc_lookup(int dc_id) {
    const char *host = getenv("TG_CLI_DC_HOST");
    const char *port = getenv("TG_CLI_DC_PORT");
    if (host && *host && port && *port) {
        g_override.id   = dc_id;
        g_override.host = host;
        g_override.port = atoi(port);
        return &g_override;
    }
    for (size_t i = 0; i < ENDPOINT_COUNT; i++) {
        if (ENDPOINTS[i].id == dc_id) return &ENDPOINTS[i];
    }
    return NULL;
}
