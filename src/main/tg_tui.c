/**
 * @file main/tg_tui.c
 * @brief tg-tui — interactive Telegram TUI entry point.
 *
 * Read-only in v1; write capability added later (US-12). Links both
 * tg-domain-read and (eventually) tg-domain-write via feature flags.
 * See docs/adr/0005-three-binary-architecture.md.
 */

#include "app/bootstrap.h"

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-tui") != 0) {
        fprintf(stderr, "tg-tui: bootstrap failed\n");
        return 1;
    }

    /* TODO: TUI main loop (US-11) */
    fprintf(stderr, "tg-tui: interactive mode not implemented yet\n");

    app_shutdown(&ctx);
    return 0;
}
