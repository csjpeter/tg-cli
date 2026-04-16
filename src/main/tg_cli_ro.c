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
#include "app/session_store.h"
#include "arg_parse.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/search.h"
#include "domain/read/contacts.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/** @brief Bring up a fully authenticated session (shared by read commands).
 *
 * On success the caller owns @p t (must call transport_close) and @p s.
 * Returns 0 on success, non-zero exit code on failure (already logged).
 */
static int session_bringup(const ArgResult *args, ApiConfig *cfg,
                            MtProtoSession *s, Transport *t) {
    if (credentials_load(cfg) != 0) return 1;

    BatchCreds *creds = (BatchCreds *)calloc(1, sizeof(BatchCreds));
    if (!creds) return 1;
    creds->phone    = args->phone;
    creds->code     = args->code;
    creds->password = args->password;

    static AuthFlowCallbacks cb;
    cb.get_phone    = cb_get_phone;
    cb.get_code     = cb_get_code;
    cb.get_password = cb_get_password;
    cb.user         = creds;

    transport_init(t);
    mtproto_session_init(s);

    AuthFlowResult res = {0};
    if (auth_flow_login(cfg, &cb, t, s, &res) != 0) {
        fprintf(stderr, "tg-cli-ro: login failed (see logs)\n");
        transport_close(t);
        free(creds);
        return 1;
    }
    free(creds);
    return 0;
}

static int cmd_me(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

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

static const char *peer_kind_name(DialogPeerKind k) {
    switch (k) {
    case DIALOG_PEER_USER:    return "user";
    case DIALOG_PEER_CHAT:    return "chat";
    case DIALOG_PEER_CHANNEL: return "channel";
    default:                  return "unknown";
    }
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static int cmd_watch(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    signal(SIGINT, on_sigint);

    UpdatesState state = {0};
    if (domain_updates_state(&cfg, &s, &t, &state) != 0) {
        fprintf(stderr, "tg-cli-ro watch: getState failed\n");
        transport_close(&t);
        return 1;
    }
    if (!args->quiet)
        fprintf(stderr, "watch: seeded pts=%d qts=%d date=%d, "
                        "polling every 30s (SIGINT to quit)\n",
                        state.pts, state.qts, state.date);

    while (!g_stop) {
        UpdatesDifference diff = {0};
        if (domain_updates_difference(&cfg, &s, &t, &state, &diff) != 0) {
            fprintf(stderr, "watch: getDifference failed, retrying in 30s\n");
        } else {
            state = diff.next_state;
            if (args->json) {
                printf("{\"new_messages\":%d,\"empty\":%s,\"too_long\":%s,"
                       "\"pts\":%d,\"date\":%d,\"items\":[",
                       diff.new_messages_count,
                       diff.is_empty ? "true" : "false",
                       diff.is_too_long ? "true" : "false",
                       state.pts, state.date);
                for (int i = 0; i < diff.new_messages_count; i++) {
                    if (i) printf(",");
                    printf("{\"id\":%d,\"date\":%d,\"text\":\"%s\"}",
                           diff.new_messages[i].id,
                           diff.new_messages[i].date,
                           diff.new_messages[i].text);
                }
                printf("]}\n");
            } else {
                for (int i = 0; i < diff.new_messages_count; i++) {
                    printf("[%d] %d %s\n",
                           diff.new_messages[i].id,
                           diff.new_messages[i].date,
                           diff.new_messages[i].complex
                               ? "(complex — text not parsed)"
                               : diff.new_messages[i].text);
                }
                if (diff.new_messages_count == 0 && !args->quiet) {
                    printf("(no new messages; pts=%d date=%d)\n",
                           state.pts, state.date);
                }
            }
            fflush(stdout);
        }
        /* Sleep in 1-second chunks so SIGINT is responsive. */
        for (int i = 0; i < 30 && !g_stop; i++) sleep(1);
    }

    transport_close(&t);
    return 0;
}

static int resolve_peer_arg(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, const char *peer_arg,
                             HistoryPeer *out);

static int cmd_contacts(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    ContactEntry *entries = calloc(CONTACTS_MAX, sizeof(ContactEntry));
    if (!entries) { transport_close(&t); return 1; }
    int count = 0;
    int rc = domain_get_contacts(&cfg, &s, &t, entries, CONTACTS_MAX, &count);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro contacts: failed (see logs)\n");
        free(entries);
        return 1;
    }
    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"user_id\":%lld,\"mutual\":%s}",
                   (long long)entries[i].user_id,
                   entries[i].mutual ? "true" : "false");
        }
        printf("]\n");
    } else {
        printf("%-18s %s\n", "user_id", "mutual");
        for (int i = 0; i < count; i++) {
            printf("%-18lld %s\n",
                   (long long)entries[i].user_id,
                   entries[i].mutual ? "yes" : "no");
        }
        if (count == 0) printf("(no contacts)\n");
    }
    free(entries);
    return 0;
}

static int cmd_search(const ArgResult *args) {
    if (!args->query) {
        fprintf(stderr, "tg-cli-ro search: <query> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    int limit = args->limit > 0 ? args->limit : 20;
    if (limit > 100) limit = 100;

    HistoryEntry *entries = calloc((size_t)limit, sizeof(HistoryEntry));
    if (!entries) { transport_close(&t); return 1; }
    int count = 0, rc;

    if (args->peer) {
        HistoryPeer peer = {0};
        if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
            transport_close(&t); free(entries); return 1;
        }
        rc = domain_search_peer(&cfg, &s, &t, &peer, args->query, limit,
                                entries, &count);
    } else {
        rc = domain_search_global(&cfg, &s, &t, args->query, limit,
                                   entries, &count);
    }
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro search: failed (see logs)\n");
        free(entries);
        return 1;
    }

    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"id\":%d,\"out\":%s}",
                   entries[i].id, entries[i].out ? "true" : "false");
        }
        printf("]\n");
    } else {
        printf("%-8s %-4s\n", "id", "out");
        for (int i = 0; i < count; i++) {
            printf("%-8d %-4s\n",
                   entries[i].id, entries[i].out ? "yes" : "no");
        }
        if (count == 0) printf("(no matches)\n");
    }
    free(entries);
    return 0;
}

static const char *resolved_kind_name(ResolvedKind k) {
    switch (k) {
    case RESOLVED_KIND_USER:    return "user";
    case RESOLVED_KIND_CHAT:    return "chat";
    case RESOLVED_KIND_CHANNEL: return "channel";
    default:                    return "unknown";
    }
}

static int cmd_user_info(const ArgResult *args) {
    if (!args->peer) {
        fprintf(stderr, "tg-cli-ro user-info: <peer> argument required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    ResolvedPeer r = {0};
    int rc = domain_resolve_username(&cfg, &s, &t, args->peer, &r);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro user-info: resolve failed (see logs)\n");
        return 1;
    }

    if (args->json) {
        printf("{\"type\":\"%s\",\"id\":%lld,\"username\":\"%s\","
               "\"access_hash\":\"%s\"}\n",
               resolved_kind_name(r.kind),
               (long long)r.id,
               r.username,
               r.have_hash ? "present" : "none");
    } else {
        printf("type:         %s\n", resolved_kind_name(r.kind));
        printf("id:           %lld\n", (long long)r.id);
        if (r.username[0]) printf("username:     @%s\n", r.username);
        printf("access_hash:  %s\n", r.have_hash ? "present" : "none");
    }
    return 0;
}

/* Resolve @peer or "self" into a HistoryPeer while the transport is up. */
static int resolve_peer_arg(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, const char *peer_arg,
                             HistoryPeer *out) {
    if (!peer_arg || strcmp(peer_arg, "self") == 0) {
        out->kind = HISTORY_PEER_SELF;
        out->peer_id = 0;
        out->access_hash = 0;
        return 0;
    }
    ResolvedPeer rp = {0};
    if (domain_resolve_username(cfg, s, t, peer_arg, &rp) != 0) return -1;

    switch (rp.kind) {
    case RESOLVED_KIND_USER:    out->kind = HISTORY_PEER_USER;    break;
    case RESOLVED_KIND_CHANNEL: out->kind = HISTORY_PEER_CHANNEL; break;
    case RESOLVED_KIND_CHAT:    out->kind = HISTORY_PEER_CHAT;    break;
    default:
        fprintf(stderr, "tg-cli-ro history: unknown peer type for %s\n",
                peer_arg);
        return -1;
    }
    out->peer_id = rp.id;
    out->access_hash = rp.access_hash;
    if ((out->kind == HISTORY_PEER_USER || out->kind == HISTORY_PEER_CHANNEL)
        && !rp.have_hash) {
        fprintf(stderr,
                "tg-cli-ro history: %s resolved but no access_hash was "
                "exposed by the server — cannot fetch history\n",
                peer_arg);
        return -1;
    }
    return 0;
}

static int cmd_history(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t);
        return 1;
    }

    int limit = args->limit > 0 ? args->limit : 20;
    if (limit > 100) limit = 100;
    int offset = args->offset > 0 ? args->offset : 0;

    HistoryEntry *entries = calloc((size_t)limit, sizeof(HistoryEntry));
    if (!entries) { transport_close(&t); return 1; }

    int count = 0;
    int rc = domain_get_history(&cfg, &s, &t, &peer, offset, limit,
                                 entries, &count);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro history: failed (see logs)\n");
        free(entries);
        return 1;
    }

    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"id\":%d,\"out\":%s,\"date\":%d,"
                   "\"text\":\"%s\",\"complex\":%s}",
                   entries[i].id,
                   entries[i].out ? "true" : "false",
                   entries[i].date,
                   entries[i].text,
                   entries[i].complex ? "true" : "false");
        }
        printf("]\n");
    } else {
        for (int i = 0; i < count; i++) {
            printf("[%d] %s %d %s\n",
                   entries[i].id,
                   entries[i].out ? ">" : "<",
                   entries[i].date,
                   entries[i].complex ? "(complex — text not parsed)"
                                       : entries[i].text);
        }
        if (count == 0) printf("(no messages)\n");
    }
    free(entries);
    return 0;
}

static int cmd_dialogs(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    int limit = args->limit > 0 ? args->limit : 20;
    if (limit > 100) limit = 100;

    DialogEntry *entries = calloc((size_t)limit, sizeof(DialogEntry));
    if (!entries) { transport_close(&t); return 1; }

    int count = 0;
    int rc = domain_get_dialogs(&cfg, &s, &t, limit, entries, &count);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro dialogs: failed (see logs)\n");
        free(entries);
        return 1;
    }

    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"type\":\"%s\",\"id\":%lld,\"top\":%d,\"unread\":%d}",
                   peer_kind_name(entries[i].kind),
                   (long long)entries[i].peer_id,
                   entries[i].top_message_id,
                   entries[i].unread_count);
        }
        printf("]\n");
    } else {
        printf("%-8s %-18s %6s %6s\n", "type", "id", "top", "unread");
        for (int i = 0; i < count; i++) {
            printf("%-8s %-18lld %6d %6d\n",
                   peer_kind_name(entries[i].kind),
                   (long long)entries[i].peer_id,
                   entries[i].top_message_id,
                   entries[i].unread_count);
        }
    }
    free(entries);
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
        "  --logout            Clear persisted session (~/.config/tg-cli/session.bin)\n"
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

    /* --logout is a special top-level flag: clears the persisted session
     * and exits. Handled before arg_parse so it works standalone. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--logout") == 0) {
            session_store_clear();
            fprintf(stderr, "tg-cli-ro: persisted session cleared.\n");
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
        case CMD_ME:
            exit_code = cmd_me(&args);
            break;
        case CMD_DIALOGS:
            exit_code = cmd_dialogs(&args); break;
        case CMD_HISTORY:
            exit_code = cmd_history(&args); break;
        case CMD_SEARCH:
            exit_code = cmd_search(&args); break;
        case CMD_USER_INFO:
            exit_code = cmd_user_info(&args); break;
        case CMD_CONTACTS:
            exit_code = cmd_contacts(&args); break;
        case CMD_WATCH:
            exit_code = cmd_watch(&args); break;
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
