/**
 * @file main/tg_tui.c
 * @brief tg-tui — interactive Telegram client entry point.
 *
 * V1 is an interactive command shell (readline-backed). Since P5-03/06
 * landed, the TUI also links tg-domain-write and exposes send / reply /
 * edit / delete / forward / read commands. A full curses-style TUI with
 * panes and live redraw is tracked under US-11 v2.
 */

#include "app/bootstrap.h"
#include "app/auth_flow.h"
#include "app/credentials.h"

#include "readline.h"
#include "platform/terminal.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/contacts.h"
#include "domain/read/search.h"

#include "domain/write/send.h"
#include "domain/write/edit.h"
#include "domain/write/delete.h"
#include "domain/write/forward.h"
#include "domain/write/read_history.h"
#include "domain/write/upload.h"

#include "tui/app.h"

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

/* ---- Write commands ---- */

/* Split "arg" on the first whitespace. Returns pointer to the remainder
 * (may be empty). The first token is NUL-terminated in place. */
static char *split_rest(char *arg) {
    while (*arg && *arg != ' ' && *arg != '\t') arg++;
    if (!*arg) return arg;
    *arg++ = '\0';
    while (*arg == ' ' || *arg == '\t') arg++;
    return arg;
}

static void do_send(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                     char *arg) {
    if (!arg || !*arg) { puts("usage: send <peer> <text>"); return; }
    char *text = split_rest(arg);
    if (!*text) { puts("usage: send <peer> <text>"); return; }

    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("send: cannot resolve '%s'\n", arg);
        return;
    }
    int32_t new_id = 0;
    RpcError err = {0};
    if (domain_send_message(cfg, s, t, &peer, text, &new_id, &err) != 0) {
        printf("send: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    if (new_id > 0) printf("sent, id=%d\n", new_id);
    else            puts("sent");
}

static void do_reply(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                      char *arg) {
    if (!arg || !*arg) { puts("usage: reply <peer> <msg_id> <text>"); return; }
    char *rest = split_rest(arg);
    if (!*rest) { puts("usage: reply <peer> <msg_id> <text>"); return; }
    char *text = split_rest(rest);
    if (!*text) { puts("usage: reply <peer> <msg_id> <text>"); return; }
    int32_t mid = atoi(rest);
    if (mid <= 0) { puts("reply: <msg_id> must be positive"); return; }

    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("reply: cannot resolve '%s'\n", arg); return;
    }
    int32_t new_id = 0; RpcError err = {0};
    if (domain_send_message_reply(cfg, s, t, &peer, text, mid,
                                    &new_id, &err) != 0) {
        printf("reply: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    if (new_id > 0) printf("sent, id=%d (reply to %d)\n", new_id, mid);
    else            printf("sent (reply to %d)\n", mid);
}

static void do_edit(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                     char *arg) {
    if (!arg || !*arg) { puts("usage: edit <peer> <msg_id> <text>"); return; }
    char *rest = split_rest(arg);
    if (!*rest) { puts("usage: edit <peer> <msg_id> <text>"); return; }
    char *text = split_rest(rest);
    if (!*text) { puts("usage: edit <peer> <msg_id> <text>"); return; }
    int32_t mid = atoi(rest);
    if (mid <= 0) { puts("edit: <msg_id> must be positive"); return; }

    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("edit: cannot resolve '%s'\n", arg); return;
    }
    RpcError err = {0};
    if (domain_edit_message(cfg, s, t, &peer, mid, text, &err) != 0) {
        printf("edit: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    printf("edited %d\n", mid);
}

static void do_delete(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                       char *arg) {
    if (!arg || !*arg) { puts("usage: delete <peer> <msg_id> [revoke]"); return; }
    char *rest = split_rest(arg);
    if (!*rest) { puts("usage: delete <peer> <msg_id> [revoke]"); return; }
    int revoke = 0;
    char *id_tok = rest;
    char *extra = split_rest(rest);
    if (*extra && !strcmp(extra, "revoke")) revoke = 1;
    int32_t mid = atoi(id_tok);
    if (mid <= 0) { puts("delete: <msg_id> must be positive"); return; }

    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("delete: cannot resolve '%s'\n", arg); return;
    }
    int32_t ids[1] = { mid };
    RpcError err = {0};
    if (domain_delete_messages(cfg, s, t, &peer, ids, 1, revoke, &err) != 0) {
        printf("delete: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    printf("deleted %d%s\n", mid, revoke ? " (revoke)" : "");
}

static void do_forward(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                        char *arg) {
    if (!arg || !*arg) { puts("usage: forward <from> <to> <msg_id>"); return; }
    char *rest = split_rest(arg);
    if (!*rest) { puts("usage: forward <from> <to> <msg_id>"); return; }
    char *id_tok = split_rest(rest);
    if (!*id_tok) { puts("usage: forward <from> <to> <msg_id>"); return; }
    int32_t mid = atoi(id_tok);
    if (mid <= 0) { puts("forward: <msg_id> must be positive"); return; }

    HistoryPeer from = {0}, to = {0};
    if (resolve_history_peer(cfg, s, t, arg, &from) != 0
        || resolve_history_peer(cfg, s, t, rest, &to) != 0) {
        puts("forward: cannot resolve peers"); return;
    }
    int32_t ids[1] = { mid };
    RpcError err = {0};
    if (domain_forward_messages(cfg, s, t, &from, &to, ids, 1, &err) != 0) {
        printf("forward: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    printf("forwarded %d\n", mid);
}

static void do_upload(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                       char *arg) {
    if (!arg || !*arg) { puts("usage: upload <peer> <path> [caption]"); return; }
    char *path = split_rest(arg);
    if (!*path) { puts("usage: upload <peer> <path> [caption]"); return; }
    char *caption = split_rest(path);
    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("upload: cannot resolve '%s'\n", arg); return;
    }
    RpcError err = {0};
    int as_photo = domain_path_is_image(path);
    int rc = as_photo
        ? domain_send_photo(cfg, s, t, &peer, path,
                             *caption ? caption : NULL, &err)
        : domain_send_file (cfg, s, t, &peer, path,
                             *caption ? caption : NULL, NULL, &err);
    if (rc != 0) {
        printf("upload: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    printf("uploaded %s as %s\n", path, as_photo ? "photo" : "document");
}

static void do_read(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                     const char *arg) {
    if (!arg || !*arg) { puts("usage: read <peer>"); return; }
    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("read: cannot resolve '%s'\n", arg); return;
    }
    RpcError err = {0};
    if (domain_mark_read(cfg, s, t, &peer, 0, &err) != 0) {
        printf("read: failed (%d: %s)\n", err.error_code, err.error_msg);
        return;
    }
    puts("marked as read");
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
        "Commands (accept '/' prefix too):\n"
        "\n"
        "Read commands:\n"
        "  me                           Show own profile\n"
        "  dialogs [N]                  List up to N dialogs (default 20)\n"
        "  history [<peer>] [N]         Saved Messages by default, or <peer>\n"
        "  contacts                     List my contacts\n"
        "  info <@peer>                 Resolve peer info\n"
        "  search <query>               Global message search (top 20)\n"
        "  poll                         One-shot updates.getDifference\n"
        "  read <peer>                  Mark peer's history as read\n"
        "\n"
        "Write commands:\n"
        "  send <peer> <text>           Send a text message\n"
        "  reply <peer> <msg_id> <text> Send as a reply to msg_id\n"
        "  edit <peer> <msg_id> <text>  Edit a previously sent message\n"
        "  delete <peer> <msg_id> [revoke]  Delete a message\n"
        "  forward <from> <to> <msg_id> Forward one message\n"
        "  upload <peer> <path> [caption] Upload a file (document)\n"
        "\n"
        "Session:\n"
        "  help, ?                      Show this help\n"
        "  quit, exit, :q               Leave the TUI (Ctrl-D also exits)\n"
        "\n"
        "Launch flags (pass on the command line, not inside the REPL):\n"
        "  --tui                        Curses-style three-pane UI instead of REPL\n"
        "  --phone <number>             Batch login phone (E.164)\n"
        "  --code <digits>              Batch login SMS/app code\n"
        "  --password <pass>            Batch login 2FA password\n"
        "  --logout                     Clear persisted session and exit\n"
        "\n"
        "See man tg-tui(1) for the full reference.\n"
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
        if (!strcmp(cmd, "send"))     { do_send(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "reply"))    { do_reply(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "edit"))     { do_edit(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "delete") || !strcmp(cmd, "del")) {
            do_delete(cfg, s, t, arg); continue;
        }
        if (!strcmp(cmd, "forward") || !strcmp(cmd, "fwd")) {
            do_forward(cfg, s, t, arg); continue;
        }
        if (!strcmp(cmd, "read"))     { do_read(cfg, s, t, arg); continue; }
        if (!strcmp(cmd, "upload"))   { do_upload(cfg, s, t, arg); continue; }

        printf("unknown command: %s  (try 'help')\n", cmd);
    }
}


/* Map a DialogEntry to a HistoryPeer. Legacy groups don't need an
 * access_hash; users/channels do, and since TUI-08 the DialogEntry
 * carries it when the server sent one. */
static int dialog_to_history_peer(const DialogEntry *d, HistoryPeer *out) {
    memset(out, 0, sizeof(*out));
    switch (d->kind) {
    case DIALOG_PEER_CHAT:
        out->kind = HISTORY_PEER_CHAT;
        out->peer_id = d->peer_id;
        return 0;
    case DIALOG_PEER_USER:
        if (!d->have_access_hash) return -1;
        out->kind = HISTORY_PEER_USER;
        out->peer_id = d->peer_id;
        out->access_hash = d->access_hash;
        return 0;
    case DIALOG_PEER_CHANNEL:
        if (!d->have_access_hash) return -1;
        out->kind = HISTORY_PEER_CHANNEL;
        out->peer_id = d->peer_id;
        out->access_hash = d->access_hash;
        return 0;
    default:
        return -1;
    }
}

/* Curses-style TUI loop (US-11 v2). Returns 0 on clean exit. */
static int run_tui_loop(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t) {
    int rows = terminal_rows(); if (rows < 3)  rows = 24;
    int cols = terminal_cols(); if (cols < 40) cols = 80;

    TuiApp app;
    if (tui_app_init(&app, rows, cols) != 0) {
        fprintf(stderr, "tg-tui: cannot initialize TUI (size %dx%d)\n",
                rows, cols);
        return 1;
    }

    /* Prime the dialog list. Failure is non-fatal; the user sees the
     * empty-placeholder and can still quit. */
    if (dialog_pane_refresh(&app.dialogs, cfg, s, t) != 0) {
        status_row_set_message(&app.status, "dialogs: load failed");
    }
    /* dialog_pane_set_entries resets the list_view but forgets the
     * viewport height set in tui_app_init; restore it. */
    app.dialogs.lv.rows_visible = app.layout.dialogs.rows;

    RAII_TERM_RAW TermRawState *raw = terminal_raw_enter();
    if (!raw) {
        fprintf(stderr, "tg-tui: cannot enter raw mode\n");
        tui_app_free(&app);
        return 1;
    }
    screen_cursor_visible(&app.screen, 0);

    terminal_enable_resize_notifications();

    /* Prime updates polling. If getState fails (eg. offline or server hiccup)
     * we silently skip live polling — the pane UI still works. */
    UpdatesState upd_state = {0};
    int upd_active = (domain_updates_state(cfg, s, t, &upd_state) == 0);

    tui_app_paint(&app);
    screen_flip(&app.screen);

    /* Poll cadence for updates.getDifference. 5 seconds is low enough to
     * feel live and high enough not to rate-limit ourselves. */
    const int POLL_INTERVAL_MS = 5000;

    int rc = 0;
    for (;;) {
        int ready = terminal_wait_key(POLL_INTERVAL_MS);

        /* SIGWINCH interrupts poll(): pick up the new size and repaint. */
        if (terminal_consume_resize()) {
            int nr = terminal_rows(); if (nr < 3)  nr = app.rows;
            int nc = terminal_cols(); if (nc < 40) nc = app.cols;
            if (nr != app.rows || nc != app.cols) {
                if (tui_app_resize(&app, nr, nc) == 0) {
                    screen_cursor_visible(&app.screen, 0);
                    tui_app_paint(&app);
                    screen_flip(&app.screen);
                }
            }
            if (ready < 0) continue;   /* poll returned -1 (EINTR) */
        }

        if (ready == 0) {
            /* No keystroke within the poll window: consult the server
             * for any changes since we last asked. If anything came in,
             * refresh the dialog pane (titles / unread counts) and, if a
             * dialog is currently open, its history too. */
            if (upd_active) {
                UpdatesDifference diff = {0};
                if (domain_updates_difference(cfg, s, t,
                                               &upd_state, &diff) == 0) {
                    upd_state = diff.next_state;
                    int changed = !diff.is_empty
                               && (diff.new_messages_count > 0
                                   || diff.other_updates_count > 0);
                    if (changed) {
                        dialog_pane_refresh(&app.dialogs, cfg, s, t);
                        app.dialogs.lv.rows_visible = app.layout.dialogs.rows;
                        if (app.history.peer_loaded) {
                            history_pane_load(&app.history, cfg, s, t,
                                              &app.history.peer);
                            app.history.lv.rows_visible = app.layout.history.rows;
                        }
                        tui_app_paint(&app);
                        screen_flip(&app.screen);
                    }
                }
            }
            continue;
        }

        if (ready < 0) continue;   /* interrupted before any key — retry */

        TermKey key = terminal_read_key();
        TuiEvent ev;
        if (key == TERM_KEY_IGNORE) {
            ev = tui_app_handle_char(&app, terminal_last_printable());
        } else {
            ev = tui_app_handle_key(&app, key);
        }

        if (ev == TUI_EVENT_QUIT) break;

        if (ev == TUI_EVENT_OPEN_DIALOG) {
            const DialogEntry *d = dialog_pane_selected(&app.dialogs);
            if (d) {
                HistoryPeer peer;
                if (dialog_to_history_peer(d, &peer) == 0) {
                    status_row_set_message(&app.status, "loading…");
                    tui_app_paint(&app);
                    screen_flip(&app.screen);
                    if (history_pane_load(&app.history, cfg, s, t, &peer) == 0) {
                        /* Restore the viewport height lost to the
                         * history_pane_set_entries reset. */
                        app.history.lv.rows_visible = app.layout.history.rows;
                        status_row_set_message(&app.status, NULL);
                    } else {
                        status_row_set_message(&app.status, "history: load failed");
                    }
                } else {
                    status_row_set_message(&app.status,
                                           "cannot open (access_hash missing)");
                }
            }
        }

        if (ev != TUI_EVENT_NONE) {
            tui_app_paint(&app);
            screen_flip(&app.screen);
        }
    }

    /* Reset the terminal for the shell prompt. */
    screen_cursor_visible(&app.screen, 1);
    screen_cursor(&app.screen, app.rows, 1);
    fputs("\r\n", stdout);
    fflush(stdout);
    tui_app_free(&app);
    return rc;
}

/* Parse argv for the `--tui` flag. Returns 1 if present. */
static int has_tui_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tui") == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int tui_mode = has_tui_flag(argc, argv);

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

    if (!tui_mode) {
        fputs("tg-tui — interactive Telegram client. "
              "Type 'help' for commands (or run with --tui for pane view).\n",
              stdout);
    }

    if (auth_flow_login(&cfg, &cb, &t, &s, NULL) != 0) {
        fprintf(stderr, "tg-tui: login failed\n");
        transport_close(&t);
        app_shutdown(&ctx);
        return 1;
    }

    int rc = tui_mode ? run_tui_loop(&cfg, &s, &t)
                      : repl(&cfg, &s, &t, &hist);
    transport_close(&t);
    app_shutdown(&ctx);
    return rc;
}
