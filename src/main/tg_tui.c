/**
 * @file main/tg_tui.c
 * @brief tg-tui — interactive Telegram client entry point.
 *
 * V1 is an interactive command shell (readline-backed) that wraps the
 * same read-only domain functions as tg-cli-ro. A full curses-style TUI
 * with panes and live redraw is tracked under US-11 v2.
 *
 * Read-only in v1; write capability (send/edit/...) lands under US-12
 * once the write domain module exists. See docs/adr/0005.
 */

#include "app/bootstrap.h"
#include "app/auth_flow.h"
#include "app/credentials.h"

#include "readline.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/contacts.h"
#include "domain/read/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Interactive credential prompts ---- */

typedef struct {
    LineHistory *hist;
} PromptCtx;

static int tui_read_line(const char *label, char *out, size_t cap,
                          LineHistory *hist) {
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "%s: ", label);
    int n = rl_readline(prompt, out, cap, hist);
    return (n < 0) ? -1 : 0;
}

static int cb_get_phone(void *u, char *out, size_t cap) {
    PromptCtx *c = (PromptCtx *)u;
    return tui_read_line("phone (+...)", out, cap, c->hist);
}
static int cb_get_code(void *u, char *out, size_t cap) {
    PromptCtx *c = (PromptCtx *)u;
    return tui_read_line("code", out, cap, c->hist);
}
static int cb_get_password(void *u, char *out, size_t cap) {
    PromptCtx *c = (PromptCtx *)u;
    return tui_read_line("2FA password", out, cap, c->hist);
}

/* ---- Forward label helpers ---- */

static const char *kind_label(DialogPeerKind k);
static const char *resolved_label(ResolvedKind k);

/* ---- Commands dispatched from the interactive loop ---- */

static void do_me(const ApiConfig *cfg, MtProtoSession *s, Transport *t) {
    SelfInfo me = {0};
    if (domain_get_self(cfg, s, t, &me) != 0) {
        puts("me: request failed");
        return;
    }
    printf("id=%lld  @%s  %s %s  +%s  premium=%s\n",
           (long long)me.id,
           me.username[0] ? me.username : "(none)",
           me.first_name, me.last_name,
           me.phone[0] ? me.phone : "(hidden)",
           me.is_premium ? "yes" : "no");
}

static void do_dialogs(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                       int limit) {
    if (limit <= 0 || limit > 100) limit = 20;
    DialogEntry entries[100] = {0};
    int count = 0;
    if (domain_get_dialogs(cfg, s, t, limit, entries, &count) != 0) {
        puts("dialogs: request failed");
        return;
    }
    printf("%-8s %6s %-32s %s\n", "type", "unread", "title", "@username / id");
    for (int i = 0; i < count; i++) {
        const char *title = entries[i].title[0] ? entries[i].title : "(no title)";
        if (entries[i].username[0]) {
            printf("%-8s %6d %-32s @%s\n",
                   kind_label(entries[i].kind),
                   entries[i].unread_count, title, entries[i].username);
        } else {
            printf("%-8s %6d %-32s %lld\n",
                   kind_label(entries[i].kind),
                   entries[i].unread_count, title,
                   (long long)entries[i].peer_id);
        }
    }
}

static const char *kind_label(DialogPeerKind k) {
    switch (k) {
    case DIALOG_PEER_USER:    return "user";
    case DIALOG_PEER_CHAT:    return "chat";
    case DIALOG_PEER_CHANNEL: return "channel";
    default:                  return "unknown";
    }
}

static const char *resolved_label(ResolvedKind k) {
    switch (k) {
    case RESOLVED_KIND_USER:    return "user";
    case RESOLVED_KIND_CHAT:    return "chat";
    case RESOLVED_KIND_CHANNEL: return "channel";
    default:                    return "unknown";
    }
}

static int resolve_history_peer(const ApiConfig *cfg, MtProtoSession *s,
                                 Transport *t, const char *arg,
                                 HistoryPeer *out) {
    if (!arg || !*arg || !strcmp(arg, "self")) {
        out->kind = HISTORY_PEER_SELF; out->peer_id = 0; out->access_hash = 0;
        return 0;
    }
    ResolvedPeer rp = {0};
    if (domain_resolve_username(cfg, s, t, arg, &rp) != 0) return -1;
    switch (rp.kind) {
    case RESOLVED_KIND_USER:    out->kind = HISTORY_PEER_USER;    break;
    case RESOLVED_KIND_CHANNEL: out->kind = HISTORY_PEER_CHANNEL; break;
    case RESOLVED_KIND_CHAT:    out->kind = HISTORY_PEER_CHAT;    break;
    default: return -1;
    }
    out->peer_id = rp.id;
    out->access_hash = rp.access_hash;
    if ((out->kind == HISTORY_PEER_USER || out->kind == HISTORY_PEER_CHANNEL)
        && !rp.have_hash) return -1;
    return 0;
}

static void do_history_any(const ApiConfig *cfg, MtProtoSession *s,
                            Transport *t, const char *arg, int limit) {
    if (limit <= 0 || limit > 100) limit = 20;
    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("history: could not resolve '%s'\n", arg ? arg : "self");
        return;
    }
    HistoryEntry entries[100] = {0};
    int count = 0;
    if (domain_get_history(cfg, s, t, &peer, 0, limit, entries, &count) != 0) {
        puts("history: request failed");
        return;
    }
    for (int i = 0; i < count; i++) {
        printf("[%d] %s %d %s\n",
               entries[i].id,
               entries[i].out ? ">" : "<",
               entries[i].date,
               entries[i].complex ? "(complex — text not parsed)"
                                   : entries[i].text);
    }
    if (count == 0) puts("(no messages)");
}

static void do_contacts(const ApiConfig *cfg, MtProtoSession *s, Transport *t) {
    ContactEntry entries[64] = {0};
    int count = 0;
    if (domain_get_contacts(cfg, s, t, entries, 64, &count) != 0) {
        puts("contacts: request failed");
        return;
    }
    printf("%-18s %s\n", "user_id", "mutual");
    for (int i = 0; i < count; i++) {
        printf("%-18lld %s\n",
               (long long)entries[i].user_id,
               entries[i].mutual ? "yes" : "no");
    }
    if (count == 0) puts("(no contacts)");
}

static void do_info(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                    const char *peer) {
    if (!peer || !*peer) { puts("usage: info <@name>"); return; }
    ResolvedPeer r = {0};
    if (domain_resolve_username(cfg, s, t, peer, &r) != 0) {
        puts("info: resolve failed");
        return;
    }
    printf("type:         %s\nid:           %lld\nusername:     @%s\n"
           "access_hash:  %s\n",
           resolved_label(r.kind),
           (long long)r.id,
           r.username[0] ? r.username : "",
           r.have_hash ? "present" : "none");
}

static void do_search(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                      const char *query) {
    if (!query || !*query) { puts("usage: search <query>"); return; }
    HistoryEntry e[20] = {0};
    int n = 0;
    if (domain_search_global(cfg, s, t, query, 20, e, &n) != 0) {
        puts("search: request failed");
        return;
    }
    for (int i = 0; i < n; i++) {
        printf("[%d] %s\n", e[i].id,
               e[i].complex ? "(complex)" : e[i].text);
    }
    if (n == 0) puts("(no matches)");
}

static void do_poll(const ApiConfig *cfg, MtProtoSession *s, Transport *t) {
    UpdatesState st = {0};
    if (domain_updates_state(cfg, s, t, &st) != 0) {
        puts("poll: getState failed");
        return;
    }
    UpdatesDifference diff = {0};
    if (domain_updates_difference(cfg, s, t, &st, &diff) != 0) {
        puts("poll: getDifference failed");
        return;
    }
    printf("pts=%d  new_messages=%d  empty=%d\n",
           diff.next_state.pts, diff.new_messages_count, diff.is_empty);
}

/* ---- Help ---- */

static void print_help(void) {
    puts(
        "Commands (read-only v1, accept '/' prefix too):\n"
        "  me                    Show own profile\n"
        "  dialogs [N]           List up to N dialogs (default 20)\n"
        "  history [<peer>] [N]  Saved Messages by default, or <peer>\n"
        "  contacts              List my contacts\n"
        "  info <@peer>          Resolve peer info\n"
        "  search <query>        Global message search (top 20)\n"
        "  poll                  One-shot updates.getDifference\n"
        "  help                  This help\n"
        "  quit, exit, :q        Leave the TUI\n"
        "\n"
        "Write commands (send/edit/...) arrive under US-12.\n"
    );
}

/* ---- REPL ---- */

static int repl(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                 LineHistory *hist) {
    char line[512];
    for (;;) {
        int n = rl_readline("tg> ", line, sizeof(line), hist);
        if (n < 0) return 0; /* EOF / Ctrl-C */
        if (n == 0) continue;

        rl_history_add(hist, line);

        /* Parse the very small command language in-place. */
        char *cmd = line;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        if (*cmd == '\0') continue;

        char *arg = cmd;
        while (*arg && *arg != ' ' && *arg != '\t') arg++;
        if (*arg) { *arg++ = '\0'; while (*arg == ' ') arg++; }

        /* Allow an optional leading '/' for IRC-style commands. */
        if (*cmd == '/') cmd++;

        if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit") ||
            !strcmp(cmd, ":q")) return 0;
        if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { print_help(); continue; }
        if (!strcmp(cmd, "me"))       { do_me(cfg, s, t); continue; }
        if (!strcmp(cmd, "dialogs") || !strcmp(cmd, "list")) {
            do_dialogs(cfg, s, t, atoi(arg));
            continue;
        }
        if (!strcmp(cmd, "history")) {
            /* Accept "history <peer> <N>" or "history <N>" or "history". */
            char peer[128] = "";
            int lim = 0;
            if (*arg) {
                char *space = strpbrk(arg, " \t");
                if (space) {
                    size_t pn = (size_t)(space - arg);
                    if (pn >= sizeof(peer)) pn = sizeof(peer) - 1;
                    memcpy(peer, arg, pn);
                    peer[pn] = '\0';
                    while (*space == ' ' || *space == '\t') space++;
                    lim = atoi(space);
                } else {
                    if (*arg >= '0' && *arg <= '9') {
                        lim = atoi(arg);
                    } else {
                        size_t an = strlen(arg);
                        if (an >= sizeof(peer)) an = sizeof(peer) - 1;
                        memcpy(peer, arg, an);
                        peer[an] = '\0';
                    }
                }
            }
            do_history_any(cfg, s, t, peer[0] ? peer : NULL, lim);
            continue;
        }
        if (!strcmp(cmd, "contacts")) { do_contacts(cfg, s, t); continue; }
        if (!strcmp(cmd, "info"))     { do_info(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "search"))   { do_search(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "poll"))     { do_poll(cfg, s, t); continue; }

        printf("unknown command: %s  (try 'help')\n", cmd);
    }
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;

    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-tui") != 0) {
        fprintf(stderr, "tg-tui: bootstrap failed\n");
        return 1;
    }

    ApiConfig cfg;
    if (credentials_load(&cfg) != 0) {
        app_shutdown(&ctx);
        return 1;
    }

    LineHistory hist;
    rl_history_init(&hist);

    PromptCtx pctx = { .hist = &hist };
    AuthFlowCallbacks cb = {
        .get_phone    = cb_get_phone,
        .get_code     = cb_get_code,
        .get_password = cb_get_password,
        .user         = &pctx,
    };

    MtProtoSession s;
    Transport t;
    transport_init(&t);
    mtproto_session_init(&s);

    fputs("tg-tui — read-only interactive Telegram client. "
          "Type 'help' for commands.\n", stdout);

    if (auth_flow_login(&cfg, &cb, &t, &s, NULL) != 0) {
        fprintf(stderr, "tg-tui: login failed\n");
        transport_close(&t);
        app_shutdown(&ctx);
        return 1;
    }

    int rc = repl(&cfg, &s, &t, &hist);
    transport_close(&t);
    app_shutdown(&ctx);
    return rc;
}
