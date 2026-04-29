/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/dc_config.h
 * @brief Static table of Telegram data-center endpoints.
 *
 * First-boot connection uses a hardcoded IP:port for each DC id. Once
 * authenticated, `help.getConfig` gives an up-to-date list that this table
 * can be superseded by at runtime.
 *
 * Endpoints are the production ("v4") IPs published in the Telegram API
 * documentation. Subject to change — tracking this list is part of normal
 * maintenance.
 */

#ifndef APP_DC_CONFIG_H
#define APP_DC_CONFIG_H

typedef struct {
    int         id;
    const char *host;
    int         port;
} DcEndpoint;

/** @brief The default DC id used before migration (DC2). */
#define DEFAULT_DC_ID 2

/**
 * @brief Look up an endpoint by DC id.
 * @param dc_id DC id (1..5).
 * @return Pointer to a static DcEndpoint, or NULL if @p dc_id is unknown.
 */
const DcEndpoint *dc_lookup(int dc_id);

/**
 * @brief Override the host for a specific DC id (FEAT-38).
 *
 * When a non-NULL, non-empty @p host is provided, subsequent calls to
 * dc_lookup(@p dc_id) return the overridden host instead of the compiled-in
 * value.  The string is copied internally; the caller may free @p host after
 * this call returns.
 *
 * Pass NULL or an empty string to clear the override for that DC id.
 *
 * @param dc_id DC id (1..5).
 * @param host  Replacement hostname or IP string, or NULL to clear.
 */
void dc_config_set_host_override(int dc_id, const char *host);

#endif /* APP_DC_CONFIG_H */
