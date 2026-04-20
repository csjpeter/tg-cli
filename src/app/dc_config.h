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

#endif /* APP_DC_CONFIG_H */
