#include "test_helpers.h"
#include "config_store.h"
#include "fs_util.h"
#include "logger.h"
#include "raii.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void config_cleanup(void *ptr) {
    Config **cfg = (Config **)ptr;
    if (cfg && *cfg) {
        config_free(*cfg);
        *cfg = NULL;
    }
}

void test_config_store(void) {
    char *old_home = getenv("HOME");
    char *old_xdg  = getenv("XDG_CONFIG_HOME");
    setenv("HOME", "/tmp/tg-cli-test-home", 1);
    unsetenv("XDG_CONFIG_HOME");   /* ensure we use the HOME-based path */
    fs_mkdir_p("/tmp/tg-cli-test-home/.config/tg-cli", 0700);

    // 1. Test Save
    {
        Config cfg = {0};
        cfg.api_base = strdup("https://api.telegram.org");
        cfg.token    = strdup("123456:ABC-DEF");

        int res = config_save_to_store(&cfg);
        ASSERT(res == 0, "config_save_to_store should return 0");

        free(cfg.api_base);
        free(cfg.token);
    }

    // 2. Test Load - full config
    {
        RAII_WITH_CLEANUP(config_cleanup) Config *loaded = config_load_from_store();
        ASSERT(loaded != NULL, "config_load_from_store should not return NULL");
        ASSERT(strcmp(loaded->token, "123456:ABC-DEF") == 0, "Token should match");
        ASSERT(loaded->api_base != NULL, "api_base should not be NULL");
    }

    // 3. Test Load - incomplete config (missing TOKEN) → should return NULL
    {
        FILE *fp = fopen("/tmp/tg-cli-test-home/.config/tg-cli/config.ini", "w");
        if (fp) {
            fprintf(fp, "API_BASE=https://api.telegram.org\n");
            fclose(fp);
        }
        /* logger not initialised → logger_log() returns early, no output */
        RAII_WITH_CLEANUP(config_cleanup) Config *loaded = config_load_from_store();
        ASSERT(loaded == NULL, "config_load_from_store should return NULL for incomplete config");
    }

    // 4. Test Load - no config file → should return NULL
    {
        unlink("/tmp/tg-cli-test-home/.config/tg-cli/config.ini");
        RAII_WITH_CLEANUP(config_cleanup) Config *loaded = config_load_from_store();
        ASSERT(loaded == NULL, "config_load_from_store should return NULL when file is missing");
    }

    // 5. Test Save/Load with ssl_no_verify=1
    {
        Config cfg2 = {0};
        cfg2.api_base      = strdup("https://api.telegram.org");
        cfg2.token         = strdup("123456:ABC-DEF");
        cfg2.ssl_no_verify = 1;

        int res = config_save_to_store(&cfg2);
        ASSERT(res == 0, "config_save_to_store with ssl_no_verify=1 should return 0");

        RAII_WITH_CLEANUP(config_cleanup) Config *loaded2 = config_load_from_store();
        ASSERT(loaded2 != NULL, "config_load_from_store should not return NULL");
        ASSERT(loaded2->ssl_no_verify == 1, "ssl_no_verify should be 1 after load");

        free(cfg2.api_base);
        free(cfg2.token);
    }

    // 6. Test config_free with NULL (should not crash)
    config_free(NULL);

    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");
    if (old_xdg) setenv("XDG_CONFIG_HOME", old_xdg, 1);
}
