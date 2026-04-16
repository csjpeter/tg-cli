/**
 * @file main/tg_cli_ro.c
 * @brief tg-cli-ro — batch, read-only Telegram CLI entry point.
 *
 * This binary NEVER links write-capable domain code. Guarantees that the
 * running process cannot issue a mutating MTProto call by construction.
 * See docs/adr/0005-three-binary-architecture.md.
 */

#include "app/bootstrap.h"
#include "arg_parse.h"

#include "domain/read/self.h"

#include <stdio.h>
#include <string.h>

static int cmd_me(const ArgResult *args) {
    /* TODO: bring up session + auth, then call domain_get_self().
     * This wiring depends on P4-04 (DC migration) and the full login flow
     * landing in src/app/auth.c. For now, print a placeholder so the
     * subcommand surface is observable. */
    (void)args;
    fprintf(stderr,
            "tg-cli-ro me: pending app/auth.c integration "
            "(see US-03, US-05, P4-04)\n");
    return 2;
}

static void print_usage(void) {
    puts(
        "Usage: tg-cli-ro [GLOBAL FLAGS] <subcommand> [ARGS]\n"
        "\n"
        "Read-only Telegram CLI (batch mode). Cannot mutate server state.\n"
        "See docs/SPECIFICATION.md and docs/userstory/ for the feature map.\n"
        "\n"
        "Subcommands (tracked by user stories):\n"
        "  me                               Show own profile (US-05)\n"
        "  dialogs  [--limit N]             List dialogs (US-04)\n"
        "  history  <peer> [--limit N]      Fetch history (US-06)\n"
        "  search   [<peer>] <query>        Search messages (US-10)\n"
        "  user-info <peer>                 User/channel info (US-09)\n"
        "  watch    [--peers X,Y]           Watch updates (US-07)\n"
        "\n"
        "Global flags: --json --quiet --config <path> --batch\n"
    );
}

int main(int argc, char **argv) {
    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-cli-ro") != 0) {
        fprintf(stderr, "tg-cli-ro: bootstrap failed\n");
        return 1;
    }

    ArgResult args;
    int rc = arg_parse(argc, argv, &args);
    int exit_code = 0;

    switch (rc) {
    case ARG_HELP:
        print_usage();
        break;
    case ARG_VERSION:
        arg_print_version();
        break;
    case ARG_ERROR:
        exit_code = 1;
        break;
    case ARG_OK:
        switch (args.command) {
        case CMD_USER_INFO:
            /* Temporarily aliased to `me` when peer == "self". Real user-info
             * support is US-09. */
            if (args.peer && strcmp(args.peer, "self") == 0) {
                exit_code = cmd_me(&args);
            } else {
                fprintf(stderr, "tg-cli-ro user-info: not implemented yet (US-09)\n");
                exit_code = 2;
            }
            break;
        case CMD_DIALOGS:
            fprintf(stderr, "tg-cli-ro dialogs: not implemented yet (US-04)\n");
            exit_code = 2;
            break;
        case CMD_HISTORY:
            fprintf(stderr, "tg-cli-ro history: not implemented yet (US-06)\n");
            exit_code = 2;
            break;
        case CMD_SEARCH:
            fprintf(stderr, "tg-cli-ro search: not implemented yet (US-10)\n");
            exit_code = 2;
            break;
        case CMD_CONTACTS:
            fprintf(stderr, "tg-cli-ro contacts: not implemented yet (US-09)\n");
            exit_code = 2;
            break;
        case CMD_NONE:
        default:
            print_usage();
            break;
        }
        break;
    default:
        exit_code = 1;
        break;
    }

    app_shutdown(&ctx);
    return exit_code;
}
