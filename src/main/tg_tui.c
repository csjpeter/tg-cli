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
    printf("%-8s %-18s %6s %6s\n", "type", "id", "top", "unread");
    for (int i = 0; i < count; i++) {
        const char *kind = "unknown";
        switch (entries[i].kind) {
        case DIALOG_PEER_USER:    kind = "user";    break;
        case DIALOG_PEER_CHAT:    kind = "chat";    break;
        case DIALOG_PEER_CHANNEL: kind = "channel"; break;
        default: break;
        }
        printf("%-8s %-18lld %6d %6d\n",
               kind, (long long)entries[i].peer_id,
               entries[i].top_message_id, entries[i].unread_count);
    }
}

static void do_history_self(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, int limit) {
    if (limit <= 0 || limit > 100) limit = 20;
    HistoryEntry entries[100] = {0};
    int count = 0;
    if (domain_get_history_self(cfg, s, t, 0, limit, entries, &count) != 0) {
        puts("history: request failed");
        return;
    }
    printf("%-8s %-4s\n", "id", "out");
    for (int i = 0; i < count; i++) {
        printf("%-8d %-4s\n",
               entries[i].id, entries[i].out ? "yes" : "no");
    }
    if (count == 0) puts("(no messages)");
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
        "Commands (read-only v1):\n"
        "  me                    Show own profile\n"
        "  dialogs [N]           List up to N dialogs (default 20)\n"
        "  history [N]           Saved Messages, last N (default 20)\n"
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

        if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit") ||
            !strcmp(cmd, ":q")) return 0;
        if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { print_help(); continue; }
        if (!strcmp(cmd, "me"))      { do_me(cfg, s, t); continue; }
        if (!strcmp(cmd, "dialogs")) { do_dialogs(cfg, s, t, atoi(arg)); continue; }
        if (!strcmp(cmd, "history")) { do_history_self(cfg, s, t, atoi(arg)); continue; }
        if (!strcmp(cmd, "poll"))    { do_poll(cfg, s, t); continue; }

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
