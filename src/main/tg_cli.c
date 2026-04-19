/**
 * @file main/tg_cli.c
 * @brief tg-cli — batch read+write Telegram CLI entry point.
 *
 * Thin wrapper over tg-domain-read + tg-domain-write (ADR-0005). For
 * read commands the behaviour matches tg-cli-ro. `send` is added
 * on top. More write commands (edit, delete, forward, upload, read
 * markers) will follow as their domain modules land.
 */

#include "app/bootstrap.h"
#include "app/auth_flow.h"
#include "app/credentials.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "infrastructure/auth_logout.h"
#include "logger.h"
#include "arg_parse.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/search.h"
#include "domain/read/contacts.h"
#include "domain/read/media.h"
#include "domain/write/send.h"
#include "domain/write/read_history.h"
#include "domain/write/edit.h"
#include "domain/write/delete.h"
#include "domain/write/forward.h"
#include "domain/write/upload.h"
#include "fs_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *phone;
    const char *code;
    const char *password;
} BatchCreds;

static int cb_get_phone(void *u, char *out, size_t cap) {
    const BatchCreds *c = (const BatchCreds *)u;
    if (!c->phone) {
        fprintf(stderr, "tg-cli: --phone <number> required in batch mode\n");
        return -1;
    }
    snprintf(out, cap, "%s", c->phone);
    return 0;
}
static int cb_get_code(void *u, char *out, size_t cap) {
    const BatchCreds *c = (const BatchCreds *)u;
    if (!c->code) {
        fprintf(stderr, "tg-cli: --code <digits> required in batch mode\n");
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

static int session_bringup(const ArgResult *args, ApiConfig *cfg,
                            MtProtoSession *s, Transport *t) {
    if (credentials_load(cfg) != 0) return 1;
    static BatchCreds creds;
    creds.phone = args->phone;
    creds.code = args->code;
    creds.password = args->password;

    static AuthFlowCallbacks cb;
    cb.get_phone = cb_get_phone;
    cb.get_code = cb_get_code;
    cb.get_password = cb_get_password;
    cb.user = &creds;

    transport_init(t);
    mtproto_session_init(s);
    AuthFlowResult res = {0};
    if (auth_flow_login(cfg, &cb, t, s, &res) != 0) {
        fprintf(stderr, "tg-cli: login failed (see logs)\n");
        transport_close(t);
        return 1;
    }
    return 0;
}

static int resolve_peer_arg(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, const char *peer_arg,
                             HistoryPeer *out) {
    if (!peer_arg || strcmp(peer_arg, "self") == 0) {
        out->kind = HISTORY_PEER_SELF;
        return 0;
    }
    ResolvedPeer rp = {0};
    if (domain_resolve_username(cfg, s, t, peer_arg, &rp) != 0) return -1;
    switch (rp.kind) {
    case RESOLVED_KIND_USER:    out->kind = HISTORY_PEER_USER;    break;
    case RESOLVED_KIND_CHANNEL: out->kind = HISTORY_PEER_CHANNEL; break;
    case RESOLVED_KIND_CHAT:    out->kind = HISTORY_PEER_CHAT;    break;
    default: return -1;
    }
    out->peer_id = rp.id;
    out->access_hash = rp.access_hash;
    return 0;
}

static int cmd_edit(const ArgResult *args) {
    if (!args->peer || args->msg_id <= 0 || !args->message) {
        fprintf(stderr, "tg-cli edit: <peer> <msg_id> <text> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t); return 1;
    }
    RpcError err = {0};
    int rc = domain_edit_message(&cfg, &s, &t, &peer, args->msg_id,
                                   args->message, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli edit: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) printf("{\"edited\":%d}\n", args->msg_id);
    else if (!args->quiet) printf("edited %d\n", args->msg_id);
    return 0;
}

static int cmd_delete(const ArgResult *args) {
    if (!args->peer || args->msg_id <= 0) {
        fprintf(stderr, "tg-cli delete: <peer> <msg_id> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t); return 1;
    }
    RpcError err = {0};
    int32_t ids[1] = { args->msg_id };
    int rc = domain_delete_messages(&cfg, &s, &t, &peer, ids, 1,
                                      args->revoke, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli delete: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) printf("{\"deleted\":%d,\"revoke\":%s}\n",
                            args->msg_id, args->revoke ? "true" : "false");
    else if (!args->quiet) printf("deleted %d\n", args->msg_id);
    return 0;
}

static int cmd_send_file(const ArgResult *args) {
    if (!args->peer || !args->out_path) {
        fprintf(stderr, "tg-cli send-file: <peer> <path> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t); return 1;
    }
    RpcError err = {0};
    int as_photo = domain_path_is_image(args->out_path);
    int rc = as_photo
        ? domain_send_photo(&cfg, &s, &t, &peer, args->out_path,
                             args->message, &err)
        : domain_send_file (&cfg, &s, &t, &peer, args->out_path,
                             args->message, NULL, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli send-file: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) printf("{\"uploaded\":\"%s\",\"kind\":\"%s\"}\n",
                            args->out_path, as_photo ? "photo" : "document");
    else if (!args->quiet) printf("uploaded %s as %s\n",
                                   args->out_path,
                                   as_photo ? "photo" : "document");
    return 0;
}

static int cmd_forward(const ArgResult *args) {
    if (!args->peer || !args->peer2 || args->msg_id <= 0) {
        fprintf(stderr,
                "tg-cli forward: <from_peer> <to_peer> <msg_id> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer from = {0}, to = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &from) != 0
        || resolve_peer_arg(&cfg, &s, &t, args->peer2, &to) != 0) {
        transport_close(&t); return 1;
    }
    RpcError err = {0};
    int32_t ids[1] = { args->msg_id };
    int rc = domain_forward_messages(&cfg, &s, &t, &from, &to, ids, 1, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli forward: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) printf("{\"forwarded\":%d}\n", args->msg_id);
    else if (!args->quiet) printf("forwarded %d\n", args->msg_id);
    return 0;
}

static int cmd_read(const ArgResult *args) {
    if (!args->peer) {
        fprintf(stderr, "tg-cli read: <peer> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        fprintf(stderr, "tg-cli read: failed to resolve peer '%s'\n",
                args->peer);
        transport_close(&t);
        return 1;
    }
    RpcError err = {0};
    int rc = domain_mark_read(&cfg, &s, &t, &peer, args->msg_id, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli read: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) printf("{\"read\":true}\n");
    else if (!args->quiet) printf("marked as read\n");
    return 0;
}

static int cmd_send(const ArgResult *args) {
    if (!args->peer) {
        fprintf(stderr, "tg-cli send: <peer> required\n");
        return 1;
    }
    const char *msg = args->message;
    char stdin_buf[4096];

    /* If no inline message and stdin is a pipe, read it (P8-03 done here). */
    if ((!msg || !*msg)) {
        if (isatty(0)) {
            fprintf(stderr, "tg-cli send: <message> required "
                            "(or pipe it on stdin)\n");
            return 1;
        }
        size_t n = fread(stdin_buf, 1, sizeof(stdin_buf) - 1, stdin);
        if (n == 0) {
            fprintf(stderr, "tg-cli send: empty stdin\n");
            return 1;
        }
        stdin_buf[n] = '\0';
        /* Strip one trailing newline for convenience. */
        if (n > 0 && stdin_buf[n - 1] == '\n') stdin_buf[n - 1] = '\0';
        msg = stdin_buf;
    }

    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        fprintf(stderr, "tg-cli send: failed to resolve peer '%s'\n",
                args->peer);
        transport_close(&t);
        return 1;
    }

    int32_t new_id = 0;
    RpcError err = {0};
    int rc = domain_send_message_reply(&cfg, &s, &t, &peer, msg,
                                         args->reply_to, &new_id, &err);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli send: failed (%d: %s)\n",
                err.error_code, err.error_msg);
        return 1;
    }
    if (args->json) {
        printf("{\"sent\":true,\"message_id\":%d}\n", new_id);
    } else if (!args->quiet) {
        if (new_id > 0) printf("sent, id=%d\n", new_id);
        else            printf("sent\n");
    }
    return 0;
}

static void print_usage(void) {
    puts(
        "Usage: tg-cli [GLOBAL FLAGS] <subcommand> [ARGS]\n"
        "\n"
        "Batch-mode Telegram CLI for write operations.\n"
        "For read-only scripted use, invoke tg-cli-ro(1).\n"
        "For the interactive REPL, invoke tg-tui(1).\n"
        "\n"
        "Write subcommands:\n"
        "  send <peer> [--reply N] <message>  Send a text message (US-12)\n"
        "  send <peer> --stdin                Read message body from stdin\n"
        "  read <peer> [--max-id N]           Mark peer's history as read (US-12)\n"
        "  edit <peer> <msg_id> <text>        Edit a message (US-13)\n"
        "  delete <peer> <msg_id> [--revoke]  Delete a message (US-13)\n"
        "  forward <from> <to> <msg_id>       Forward a message (US-13)\n"
        "  send-file|upload <peer> <path> [--caption T]  Upload a file (US-14)\n"
        "\n"
        "Global flags:\n"
        "  --batch             Batch mode (default); no interactive prompts\n"
        "  --config <path>     Use non-default config file\n"
        "  --json              Emit JSON output where supported\n"
        "  --quiet             Suppress informational output\n"
        "  --help, -h          Show this help and exit\n"
        "  --version, -v       Show version and exit\n"
        "\n"
        "Batch-mode login flags:\n"
        "  --phone <number>    E.g. +15551234567\n"
        "  --code <digits>     SMS/app code\n"
        "  --password <pass>   2FA password\n"
        "  --logout            Clear persisted session and exit\n"
        "\n"
        "Credentials:\n"
        "  TG_CLI_API_ID / TG_CLI_API_HASH env vars, or\n"
        "  api_id= / api_hash= in ~/.config/tg-cli/config.ini\n"
        "\n"
        "See man tg-cli(1) for the full reference.\n"
    );
}

int main(int argc, char **argv) {
    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-cli") != 0) {
        fprintf(stderr, "tg-cli: bootstrap failed\n");
        return 1;
    }

    /* --logout: invalidate the session server-side, then wipe the local file. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--logout") == 0) {
            ApiConfig cfg;
            MtProtoSession s;
            Transport t;
            if (credentials_load(&cfg) == 0) {
                transport_init(&t);
                mtproto_session_init(&s);
                int loaded_dc = 0;
                if (session_store_load(&s, &loaded_dc) == 0) {
                    const DcEndpoint *ep = dc_lookup(loaded_dc);
                    if (ep && transport_connect(&t, ep->host, ep->port) == 0) {
                        t.dc_id = loaded_dc;
                        auth_logout(&cfg, &s, &t);
                        transport_close(&t);
                    } else {
                        logger_log(LOG_WARN,
                            "tg-cli: logout: cannot connect to DC%d, clearing local session",
                            loaded_dc);
                        session_store_clear();
                    }
                } else {
                    session_store_clear();
                }
            } else {
                session_store_clear();
            }
            fprintf(stderr, "tg-cli: persisted session cleared.\n");
            app_shutdown(&ctx);
            return 0;
        }
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
        case CMD_SEND:
            exit_code = cmd_send(&args); break;
        case CMD_READ:
            exit_code = cmd_read(&args); break;
        case CMD_EDIT:
            exit_code = cmd_edit(&args); break;
        case CMD_DELETE:
            exit_code = cmd_delete(&args); break;
        case CMD_FORWARD:
            exit_code = cmd_forward(&args); break;
        case CMD_SEND_FILE:
            exit_code = cmd_send_file(&args); break;
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
