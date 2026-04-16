/**
 * @file arg_parse.c
 * @brief Custom command-line argument parser for tg-cli.
 */

#include "arg_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TG_CLI_VERSION "0.1.0"

/* ---- Internal helpers ---- */

/* Sentinels returned only by try_global_flag — never exposed to callers. */
#define TGF_HELP    -10
#define TGF_VERSION -11

static int str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int parse_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*end != '\0') return -1;
    *out = (int)v;
    return 0;
}

/* ---- Global flag parsing (returns number of argv entries consumed) ----
 *
 * Called for each positional slot; returns 0 if argv[i] is not a global
 * flag, 1 for a boolean flag, 2 for a flag that takes a value.
 */
static int take_value_flag(int argc, char **argv, int i,
                            const char *name, const char **out) {
    if (i + 1 >= argc) {
        fprintf(stderr, "tg-cli: %s requires a value\n", name);
        return -1;
    }
    *out = argv[i + 1];
    return 2;
}

static int try_global_flag(int argc, char **argv, int i, ArgResult *out) {
    const char *a = argv[i];

    if (str_eq(a, "--batch"))  { out->batch  = 1; return 1; }
    if (str_eq(a, "--json"))   { out->json   = 1; return 1; }
    if (str_eq(a, "--quiet"))  { out->quiet  = 1; return 1; }
    if (str_eq(a, "--help")   || str_eq(a, "-h")) return TGF_HELP;
    if (str_eq(a, "--version") || str_eq(a, "-v")) return TGF_VERSION;

    if (str_eq(a, "--config"))
        return take_value_flag(argc, argv, i, "--config", &out->config_path);
    if (str_eq(a, "--phone"))
        return take_value_flag(argc, argv, i, "--phone", &out->phone);
    if (str_eq(a, "--code"))
        return take_value_flag(argc, argv, i, "--code", &out->code);
    if (str_eq(a, "--password"))
        return take_value_flag(argc, argv, i, "--password", &out->password);

    return 0; /* not a global flag */
}

/* ---- Subcommand parsers ---- */

static int parse_dialogs(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_DIALOGS;
    out->limit = 20; /* default */

    while (i < argc) {
        if (str_eq(argv[i], "--limit")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tg-cli dialogs: --limit requires a number\n");
                return ARG_ERROR;
            }
            if (parse_int(argv[i + 1], &out->limit) != 0) {
                fprintf(stderr, "tg-cli dialogs: --limit value is not a number\n");
                return ARG_ERROR;
            }
            i += 2;
        } else {
            fprintf(stderr, "tg-cli dialogs: unknown option: %s\n", argv[i]);
            return ARG_ERROR;
        }
    }
    return ARG_OK;
}

static int parse_history(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_HISTORY;
    out->limit  = 50; /* default */
    out->offset = 0;

    if (i >= argc) {
        fprintf(stderr, "tg-cli history: <peer> argument required\n");
        return ARG_ERROR;
    }
    if (argv[i][0] == '-') {
        fprintf(stderr, "tg-cli history: <peer> argument required before options\n");
        return ARG_ERROR;
    }
    out->peer = argv[i++];

    while (i < argc) {
        if (str_eq(argv[i], "--limit")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tg-cli history: --limit requires a number\n");
                return ARG_ERROR;
            }
            if (parse_int(argv[i + 1], &out->limit) != 0) {
                fprintf(stderr, "tg-cli history: --limit value is not a number\n");
                return ARG_ERROR;
            }
            i += 2;
        } else if (str_eq(argv[i], "--offset")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tg-cli history: --offset requires a number\n");
                return ARG_ERROR;
            }
            if (parse_int(argv[i + 1], &out->offset) != 0) {
                fprintf(stderr, "tg-cli history: --offset value is not a number\n");
                return ARG_ERROR;
            }
            i += 2;
        } else {
            fprintf(stderr, "tg-cli history: unknown option: %s\n", argv[i]);
            return ARG_ERROR;
        }
    }
    return ARG_OK;
}

static int parse_send(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_SEND;

    if (i >= argc) {
        fprintf(stderr, "tg-cli send: <peer> argument required\n");
        return ARG_ERROR;
    }
    if (argv[i][0] == '-') {
        fprintf(stderr, "tg-cli send: <peer> argument required before options\n");
        return ARG_ERROR;
    }
    out->peer = argv[i++];

    /* Optional --reply <msg_id> before the message text. */
    while (i < argc && argv[i][0] == '-') {
        if (str_eq(argv[i], "--reply")) {
            if (i + 1 >= argc
                || parse_int(argv[i + 1], &out->reply_to) != 0
                || out->reply_to <= 0) {
                fprintf(stderr,
                        "tg-cli send: --reply needs a positive message id\n");
                return ARG_ERROR;
            }
            i += 2;
        } else if (str_eq(argv[i], "--stdin")) {
            /* Explicit pipe opt-in; tg_cli will detect anyway, but this
             * is convenient in scripts that redirect stdin. */
            i++;
        } else {
            fprintf(stderr, "tg-cli send: unknown option: %s\n", argv[i]);
            return ARG_ERROR;
        }
    }

    if (i >= argc) {
        /* message may be provided via stdin; tg_cli checks isatty later. */
        return ARG_OK;
    }
    out->message = argv[i];
    return ARG_OK;
}

static int parse_search(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_SEARCH;

    if (i >= argc) {
        fprintf(stderr, "tg-cli search: <query> argument required\n");
        return ARG_ERROR;
    }

    /* Optional peer before query when two positional args are present */
    if (i + 1 < argc && argv[i][0] != '-' && argv[i + 1][0] != '-') {
        out->peer  = argv[i++];
        out->query = argv[i];
    } else if (argv[i][0] != '-') {
        out->query = argv[i];
    } else {
        fprintf(stderr, "tg-cli search: <query> argument required\n");
        return ARG_ERROR;
    }
    return ARG_OK;
}

static int parse_contacts(int argc, char **argv, int i, ArgResult *out) {
    (void)argc; (void)argv; (void)i;
    out->command = CMD_CONTACTS;
    return ARG_OK;
}

static int parse_me(int argc, char **argv, int i, ArgResult *out) {
    (void)argc; (void)argv; (void)i;
    out->command = CMD_ME;
    return ARG_OK;
}

static int parse_watch(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_WATCH;
    while (i < argc) {
        if (str_eq(argv[i], "--peers")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tg-cli watch: --peers requires a value\n");
                return ARG_ERROR;
            }
            out->peer = argv[i + 1]; /* comma-separated list stored as-is */
            i += 2;
        } else {
            fprintf(stderr, "tg-cli watch: unknown option: %s\n", argv[i]);
            return ARG_ERROR;
        }
    }
    return ARG_OK;
}

static int parse_user_info(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_USER_INFO;

    if (i >= argc) {
        fprintf(stderr, "tg-cli user-info: <peer> argument required\n");
        return ARG_ERROR;
    }
    out->peer = argv[i];
    return ARG_OK;
}

static int parse_download(int argc, char **argv, int i, ArgResult *out) {
    out->command = CMD_DOWNLOAD;

    if (i >= argc || argv[i][0] == '-') {
        fprintf(stderr, "tg-cli download: <peer> argument required\n");
        return ARG_ERROR;
    }
    out->peer = argv[i++];

    if (i >= argc || argv[i][0] == '-') {
        fprintf(stderr, "tg-cli download: <msg_id> argument required\n");
        return ARG_ERROR;
    }
    if (parse_int(argv[i], &out->msg_id) != 0 || out->msg_id <= 0) {
        fprintf(stderr, "tg-cli download: <msg_id> must be a positive integer\n");
        return ARG_ERROR;
    }
    i++;

    while (i < argc) {
        if (str_eq(argv[i], "--out")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tg-cli download: --out requires a path\n");
                return ARG_ERROR;
            }
            out->out_path = argv[i + 1];
            i += 2;
        } else {
            fprintf(stderr, "tg-cli download: unknown option: %s\n", argv[i]);
            return ARG_ERROR;
        }
    }
    return ARG_OK;
}

/* ---- Public API ---- */

int arg_parse(int argc, char **argv, ArgResult *out) {
    if (!argv || !out) return ARG_ERROR;

    memset(out, 0, sizeof(*out));
    out->limit = -1; /* -1 means "not set" (subcommand may supply default) */

    int i = 1; /* skip argv[0] (program name) */

    /* Collect global flags that may appear before the subcommand */
    while (i < argc && argv[i][0] == '-') {
        int r = try_global_flag(argc, argv, i, out);
        if (r == TGF_HELP)    return ARG_HELP;
        if (r == TGF_VERSION) return ARG_VERSION;
        if (r < 0)            return ARG_ERROR;
        if (r == 0)           break; /* unknown flag — let subcommand handle */
        i += r;
    }

    if (i >= argc) {
        /* No subcommand given */
        if (out->batch || out->json) {
            fprintf(stderr, "tg-cli: subcommand required\n");
            return ARG_ERROR;
        }
        /* Interactive mode — no subcommand is fine */
        return ARG_OK;
    }

    const char *subcmd = argv[i++];

    if (str_eq(subcmd, "dialogs"))   return parse_dialogs  (argc, argv, i, out);
    if (str_eq(subcmd, "history"))   return parse_history  (argc, argv, i, out);
    if (str_eq(subcmd, "send"))      return parse_send     (argc, argv, i, out);
    if (str_eq(subcmd, "search"))    return parse_search   (argc, argv, i, out);
    if (str_eq(subcmd, "contacts"))  return parse_contacts (argc, argv, i, out);
    if (str_eq(subcmd, "user-info")) return parse_user_info(argc, argv, i, out);
    if (str_eq(subcmd, "me")   || str_eq(subcmd, "self"))
                                      return parse_me       (argc, argv, i, out);
    if (str_eq(subcmd, "watch"))     return parse_watch    (argc, argv, i, out);
    if (str_eq(subcmd, "download"))  return parse_download (argc, argv, i, out);
    if (str_eq(subcmd, "edit")) {
        out->command = CMD_EDIT;
        if (i >= argc || argv[i][0] == '-') {
            fprintf(stderr, "tg-cli edit: <peer> required\n"); return ARG_ERROR;
        }
        out->peer = argv[i++];
        if (i >= argc || parse_int(argv[i], &out->msg_id) != 0
            || out->msg_id <= 0) {
            fprintf(stderr, "tg-cli edit: positive <msg_id> required\n");
            return ARG_ERROR;
        }
        i++;
        if (i >= argc) {
            fprintf(stderr, "tg-cli edit: <new-text> required\n");
            return ARG_ERROR;
        }
        out->message = argv[i];
        return ARG_OK;
    }
    if (str_eq(subcmd, "delete")) {
        out->command = CMD_DELETE;
        if (i >= argc || argv[i][0] == '-') {
            fprintf(stderr, "tg-cli delete: <peer> required\n"); return ARG_ERROR;
        }
        out->peer = argv[i++];
        if (i >= argc || parse_int(argv[i], &out->msg_id) != 0
            || out->msg_id <= 0) {
            fprintf(stderr, "tg-cli delete: positive <msg_id> required\n");
            return ARG_ERROR;
        }
        i++;
        while (i < argc) {
            if (str_eq(argv[i], "--revoke")) { out->revoke = 1; i++; }
            else {
                fprintf(stderr, "tg-cli delete: unknown option: %s\n", argv[i]);
                return ARG_ERROR;
            }
        }
        return ARG_OK;
    }
    if (str_eq(subcmd, "forward")) {
        out->command = CMD_FORWARD;
        if (i + 2 >= argc) {
            fprintf(stderr,
                    "tg-cli forward: <from_peer> <to_peer> <msg_id> required\n");
            return ARG_ERROR;
        }
        out->peer  = argv[i++];
        out->peer2 = argv[i++];
        if (parse_int(argv[i], &out->msg_id) != 0 || out->msg_id <= 0) {
            fprintf(stderr, "tg-cli forward: positive <msg_id> required\n");
            return ARG_ERROR;
        }
        return ARG_OK;
    }
    if (str_eq(subcmd, "read")) {
        out->command = CMD_READ;
        if (i >= argc || argv[i][0] == '-') {
            fprintf(stderr, "tg-cli read: <peer> argument required\n");
            return ARG_ERROR;
        }
        out->peer = argv[i++];
        if (i < argc && str_eq(argv[i], "--max-id")) {
            if (i + 1 >= argc
                || parse_int(argv[i + 1], &out->msg_id) != 0) {
                fprintf(stderr, "tg-cli read: --max-id needs a number\n");
                return ARG_ERROR;
            }
        }
        return ARG_OK;
    }
    if (str_eq(subcmd, "help"))      return ARG_HELP;
    if (str_eq(subcmd, "version"))   return ARG_VERSION;

    fprintf(stderr, "tg-cli: unknown subcommand: %s\n", subcmd);
    return ARG_ERROR;
}

void arg_print_help(void) {
    puts(
        "Usage: tg-cli [GLOBAL FLAGS] <subcommand> [ARGS]\n"
        "\n"
        "Global flags:\n"
        "  --batch            Non-interactive batch mode\n"
        "  --json             Machine-readable JSON output\n"
        "  --quiet            Suppress informational output\n"
        "  --config <path>    Custom config file path\n"
        "  --phone <number>   Phone number for login (batch)\n"
        "  --code <digits>    SMS/app code for login (batch)\n"
        "  --password <pass>  2FA password if required (batch)\n"
        "  --help, -h         Show this help\n"
        "  --version, -v      Show version\n"
        "\n"
        "Subcommands:\n"
        "  me                                      Show own profile\n"
        "  dialogs [--limit N]                     List dialogs/chats\n"
        "  history <peer> [--limit N] [--offset N] Fetch message history\n"
        "  search  [<peer>] <query>                Search messages\n"
        "  contacts                                List contacts\n"
        "  user-info <peer>                        Show user/channel info\n"
        "  watch   [--peers X,Y]                   Watch incoming updates\n"
        "  download <peer> <msg_id> [--out PATH]   Download photo from message\n"
        "  send    <peer> [--reply N] <message>    Send a message (tg-cli only)\n"
        "  read    <peer> [--max-id N]             Mark as read (tg-cli only)\n"
        "  edit    <peer> <msg_id> <text>          Edit a message (tg-cli only)\n"
        "  delete  <peer> <msg_id> [--revoke]      Delete a message (tg-cli only)\n"
        "  forward <from_peer> <to_peer> <msg_id>  Forward (tg-cli only)\n"
    );
}

void arg_print_version(void) {
    printf("tg-cli %s\n", TG_CLI_VERSION);
}
