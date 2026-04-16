/**
 * @file app/dc_config.c
 * @brief Hardcoded fallback DC endpoint table.
 */

#include "app/dc_config.h"

#include <stddef.h>

static const DcEndpoint ENDPOINTS[] = {
    { 1, "149.154.175.50",  443 },
    { 2, "149.154.167.50",  443 },
    { 3, "149.154.175.100", 443 },
    { 4, "149.154.167.91",  443 },
    { 5, "91.108.56.130",   443 },
};

#define ENDPOINT_COUNT (sizeof(ENDPOINTS) / sizeof(ENDPOINTS[0]))

const DcEndpoint *dc_lookup(int dc_id) {
    for (size_t i = 0; i < ENDPOINT_COUNT; i++) {
        if (ENDPOINTS[i].id == dc_id) return &ENDPOINTS[i];
    }
    return NULL;
}
