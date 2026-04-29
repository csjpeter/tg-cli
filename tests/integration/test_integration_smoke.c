/**
 * @file test_integration_smoke.c
 * @brief Placeholder smoke test — verifies the integration runner itself works.
 *
 * This suite intentionally contains no real network calls.  It exists only to
 * prove that the runner binary compiles, links, reads env vars, and skips
 * cleanly when credentials are absent.  Real test cases will be added in
 * TEST-89.
 */
#include "test_helpers_integration.h"

/**
 * @brief Smoke: passes immediately when credentials are absent, otherwise
 * verifies that the config struct was loaded correctly.
 */
static void test_integration_smoke(void)
{
    SKIP_IF_NO_CREDS();

    /* When credentials are present, sanity-check the loaded values. */
    ASSERT(g_integration_config.api_id   != NULL, "api_id is NULL");
    ASSERT(g_integration_config.api_hash != NULL, "api_hash is NULL");
    ASSERT(g_integration_config.dc_host  != NULL, "dc_host is NULL");
    ASSERT(g_integration_config.dc_port  != NULL, "dc_port is NULL");
}

void run_integration_smoke_tests(void)
{
    RUN_TEST(test_integration_smoke);
}
