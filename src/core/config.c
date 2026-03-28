#include "config.h"
#include <stdlib.h>

/**
 * @brief Frees all heap-allocated fields of cfg, then frees cfg itself.
 */
void config_free(Config *cfg) {
    if (!cfg) return;
    free(cfg->api_base);
    free(cfg->token);
    free(cfg);
}
