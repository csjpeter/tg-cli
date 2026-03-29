/**
 * @file main.c
 * @brief Entry point for tg-cli — unofficial read-only Telegram user client.
 *
 * MTProto 2.0 handshake and interactive loop will be wired here
 * once the protocol stack is complete.
 */

#include "logger.h"
#include "config.h"
#include "fs_util.h"
#include "platform/path.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    /* Initialise directories */
    const char *cache = platform_cache_dir();
    const char *config = platform_config_dir();

    if (cache)  fs_mkdir_p(cache, 0700);
    if (config) fs_mkdir_p(config, 0700);

    /* Initialise logger: cache_dir/logs/session.log */
    char log_path[2048];
    const char *base = cache ? cache : "/tmp/tg-cli";
    snprintf(log_path, sizeof(log_path), "%s/logs", base);
    fs_mkdir_p(log_path, 0700);
    size_t dir_len = strlen(log_path);
    snprintf(log_path + dir_len, sizeof(log_path) - dir_len,
             "/session.log");
    logger_init(log_path, LOG_INFO);

    logger_log(LOG_INFO, "tg-cli starting...");

    /* TODO: MTProto handshake, auth, interactive loop */

    logger_log(LOG_INFO, "tg-cli exiting.");
    logger_close();
    return 0;
}
