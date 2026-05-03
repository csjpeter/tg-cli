/**
 * @file test_dc_config.c
 * @brief Unit tests for the static DC endpoint table.
 */

#include "test_helpers.h"
#include "app/dc_config.h"

#include <string.h>

static void test_lookup_default(void) {
    const DcEndpoint *ep = dc_lookup(DEFAULT_DC_ID);
    ASSERT(ep != NULL, "default DC must be found");
    ASSERT(ep->id == DEFAULT_DC_ID, "id matches");
    ASSERT(ep->host != NULL && strlen(ep->host) > 0, "host non-empty");
    ASSERT(ep->port > 0, "port > 0");
}

static void test_lookup_all_five(void) {
    for (int id = 1; id <= 5; id++) {
        const DcEndpoint *ep = dc_lookup(id);
        ASSERT(ep != NULL, "DC 1..5 all resolvable");
        ASSERT(ep->id == id, "id matches");
    }
}

static void test_lookup_unknown(void) {
    ASSERT(dc_lookup(6)   == NULL, "DC 6 is unknown");
    ASSERT(dc_lookup(999) == NULL, "DC 999 unknown");
}

static void test_lookup_zero_wildcard(void) {
    /* dc=0 is a wildcard for Telegram test servers; maps to DEFAULT_DC_ID endpoint. */
    const DcEndpoint *ep = dc_lookup(0);
    ASSERT(ep != NULL, "DC 0 must resolve to DEFAULT_DC_ID endpoint");
    ASSERT(ep->port > 0, "DC 0 endpoint has valid port");
}

static void test_lookup_negative_test_dcs(void) {
    /* Negative IDs are used by Telegram test DCs (p_q_inner_data_dc convention). */
    for (int id = -5; id <= -1; id++) {
        const DcEndpoint *ep = dc_lookup(id);
        ASSERT(ep != NULL, "negative DC -1..-5 must resolve");
        ASSERT(ep->port > 0, "negative DC endpoint has valid port");
    }
}

void run_dc_config_tests(void) {
    RUN_TEST(test_lookup_default);
    RUN_TEST(test_lookup_all_five);
    RUN_TEST(test_lookup_unknown);
    RUN_TEST(test_lookup_zero_wildcard);
    RUN_TEST(test_lookup_negative_test_dcs);
}
