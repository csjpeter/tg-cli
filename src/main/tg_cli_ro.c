/**
 * @file main/tg_cli_ro.c
 * @brief tg-cli-ro — batch, read-only Telegram CLI entry point.
 *
 * This binary NEVER links write-capable domain code. Guarantees that the
 * running process cannot issue a mutating MTProto call by construction.
 * See docs/adr/0005-three-binary-architecture.md.
 */

#include "app/bootstrap.h"
#include "app/auth_flow.h"
#include "app/credentials.h"
#include "arg_parse.h"

#include "domain/read/self.h"

#include <stdio.h>
#include <string.h>

/* ---- Batch-mode input callbacks (values come from --phone/--code flags) ---- */

typedef struct {
    const char *phone;
    const char *code;
    const char *password;
} BatchCreds;

static int cb_get_phone(void *u, char *out, size_t cap) {
    const BatchCreds *c = (const BatchCreds *)u;
    if (!c->phone) {
        fprintf(stderr, "tg-cli-ro: --phone <number> required in batch mode\n");
        return -1;
    }
    snprintf(out, cap, "%s", c->phone);
    return 0;
}

static int cb_get_code(void *u, char *out, size_t cap) {
    const BatchCreds *c = (const BatchCreds *)u;
    if (!c->code) {
        fprintf(stderr, "tg-cli-ro: --code <digits> required in batch mode\n");
        return -1;
    }
    snprintf(out, cap, "%s", c->code);
    return 0;
}

static int cb_get_password(void *u, char *out, size_t cap) {
    const BatchCreds *c = (const BatchCreds *)u;
    if (!c->password) return -1;
    snprintf(out, cap, "%s", c->password);
    return 0;
}

/* ---- Subcommand implementations ---- */

static void print_self_plain(const SelfInfo *me) {
    printf("id:       %lld\n", (long long)me->id);
    if (me->username[0])   printf("username: @%s\n", me->username);
    if (me->first_name[0] || me->last_name[0])
        printf("name:     %s%s%s\n",
               me->first_name,
               me->last_name[0] ? " " : "",
               me->last_name);
    if (me->phone[0])      printf("phone:    +%s\n", me->phone);
    printf("premium:  %s\n", me->is_premium ? "yes" : "no");
    if (me->is_bot)        printf("bot:      yes\n");
}

static void print_self_json(const SelfInfo *me) {
    printf("{\"id\":%lld,\"username\":\"%s\",\"first_name\":\"%s\","
           "\"last_name\":\"%s\",\"phone\":\"%s\",\"premium\":%s,\"bot\":%s}\n",
           (long long)me->id,
           me->username, me->first_name, me->last_name, me->phone,
           me->is_premium ? "true" : "false",
           me->is_bot     ? "true" : "false");
}

static int cmd_me(const ArgResult *args) {
    ApiConfig cfg;
    if (credentials_load(&cfg) != 0) return 1;

    BatchCreds creds = {
        .phone    = args->phone,
        .code     = args->code,
        .password = args->password,
    };
    AuthFlowCallbacks cb = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = cb_get_password,
        .user         = &creds,
    };

    MtProtoSession s;
    Transport t;
    transport_init(&t);
    mtproto_session_init(&s);

    AuthFlowResult res = {0};
    if (auth_flow_login(&cfg, &cb, &t, &s, &res) != 0) {
        fprintf(stderr, "tg-cli-ro me: login failed (see logs)\n");
        transport_close(&t);
        return 1;
    }

    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro me: failed to fetch own profile\n");
        return 1;
    }

    if (args->json) print_self_json(&me);
    else            print_self_plain(&me);
    return 0;
}

static void print_usage(void) {
    puts(
        "Usage: tg-cli-ro [GLOBAL FLAGS] <subcommand> [ARGS]\n"
        "\n"
        "Read-only Telegram CLI (batch mode). Cannot mutate server state.\n"
        "See docs/SPECIFICATION.md and docs/userstory/ for the feature map.\n"
        "\n"
        "Subcommands:\n"
        "  me                               Show own profile (US-05)\n"
        "  dialogs  [--limit N]             List dialogs (US-04)\n"
        "  history  <peer> [--limit N]      Fetch history (US-06)\n"
        "  search   [<peer>] <query>        Search messages (US-10)\n"
        "  user-info <peer>                 User/channel info (US-09)\n"
        "  watch    [--peers X,Y]           Watch updates (US-07)\n"
        "\n"
        "Batch-mode login flags:\n"
        "  --phone <number>    E.g. +15551234567\n"
        "  --code <digits>     The SMS/app code received on the phone\n"
        "  --password <pass>   2FA password (when the account has one set)\n"
        "\n"
        "Credentials:\n"
        "  TG_CLI_API_ID / TG_CLI_API_HASH env vars, or\n"
        "  api_id= / api_hash= in ~/.config/tg-cli/config.ini\n"
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
    case ARG_HELP:    print_usage(); break;
    case ARG_VERSION: arg_print_version(); break;
    case ARG_ERROR:   exit_code = 1; break;
    case ARG_OK:
        switch (args.command) {
        case CMD_ME:
            exit_code = cmd_me(&args);
            break;
        case CMD_DIALOGS:
            fprintf(stderr, "tg-cli-ro dialogs: not implemented yet (US-04)\n");
            exit_code = 2; break;
        case CMD_HISTORY:
            fprintf(stderr, "tg-cli-ro history: not implemented yet (US-06)\n");
            exit_code = 2; break;
        case CMD_SEARCH:
            fprintf(stderr, "tg-cli-ro search: not implemented yet (US-10)\n");
            exit_code = 2; break;
        case CMD_CONTACTS:
        case CMD_USER_INFO:
            fprintf(stderr, "tg-cli-ro: not implemented yet (US-09)\n");
            exit_code = 2; break;
        case CMD_WATCH:
            fprintf(stderr, "tg-cli-ro watch: not implemented yet (US-07)\n");
            exit_code = 2; break;
        case CMD_SEND:
            fprintf(stderr, "tg-cli-ro: send is not available in read-only mode\n");
            exit_code = 2; break;
        case CMD_NONE:
        default:
            print_usage(); break;
        }
        break;
    default:
        exit_code = 1; break;
    }

    app_shutdown(&ctx);
    return exit_code;
}
