/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

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
#include "app/config_wizard.h"
#include "app/credentials.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "infrastructure/auth_logout.h"
#include "logger.h"

#include "readline.h"
#include "platform/terminal.h"
#include "arg_parse.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/contacts.h"
#include "domain/read/search.h"
#include "domain/read/media.h"

#include "infrastructure/media_index.h"

#include "platform/path.h"
#include "fs_util.h"

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
#include <time.h>

static void fmt_date(char *buf, size_t cap, int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, cap, "%Y-%m-%d %H:%M", &tm);
}

/* ---- Credential callbacks: batch values fall back to interactive prompts ---- */

typedef struct {
    LineHistory *hist;
    const char  *phone;     /**< from --phone (may be NULL → interactive) */
    const char  *code;      /**< from --code  (may be NULL → interactive) */
    const char  *password;  /**< from --password (may be NULL → interactive) */
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
    if (c->phone) { snprintf(out, cap, "%s", c->phone); return 0; }
    return tui_read_line("phone (+...)", out, cap, c->hist);
}
static int cb_get_code(void *u, char *out, size_t cap) {
    PromptCtx *c = (PromptCtx *)u;
    if (c->code) { snprintf(out, cap, "%s", c->code); return 0; }
    return tui_read_line("code", out, cap, c->hist);
}
static int cb_get_password(void *u, char *out, size_t cap) {
    PromptCtx *c = (PromptCtx *)u;
    if (c->password) { snprintf(out, cap, "%s", c->password); return 0; }
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
    if (domain_get_dialogs(cfg, s, t, limit, 0, entries, &count, NULL) != 0) {
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

static int is_numeric_id(const char *s) {
    if (!s || !*s) return 0;
    const char *p = (*s == '-') ? s + 1 : s;
    if (!*p) return 0;
    for (; *p; p++) if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int resolve_numeric_peer(const ApiConfig *cfg, MtProtoSession *s,
                                  Transport *t, int64_t peer_id,
                                  HistoryPeer *out) {
    DialogEntry de = {0};
    if (domain_dialogs_find_by_id(peer_id, &de) != 0) {
        DialogEntry tmp[200];
        int tc = 0;
        domain_get_dialogs(cfg, s, t, 200, 0, tmp, &tc, NULL);
        if (domain_dialogs_find_by_id(peer_id, &de) != 0) return -1;
    }
    switch (de.kind) {
    case DIALOG_PEER_USER:    out->kind = HISTORY_PEER_USER;    break;
    case DIALOG_PEER_CHAT:    out->kind = HISTORY_PEER_CHAT;    break;
    case DIALOG_PEER_CHANNEL: out->kind = HISTORY_PEER_CHANNEL; break;
    default: return -1;
    }
    out->peer_id     = de.peer_id;
    out->access_hash = de.access_hash;
    return 0;
}

static int resolve_history_peer(const ApiConfig *cfg, MtProtoSession *s,
                                 Transport *t, const char *arg,
                                 HistoryPeer *out) {
    if (!arg || !*arg || !strcmp(arg, "self")) {
        out->kind = HISTORY_PEER_SELF; out->peer_id = 0; out->access_hash = 0;
        return 0;
    }
    if (is_numeric_id(arg)) {
        int64_t pid = (int64_t)strtoll(arg, NULL, 10);
        return resolve_numeric_peer(cfg, s, t, pid, out);
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
        char dstr[20]; fmt_date(dstr, sizeof(dstr), entries[i].date);
        printf("[%d] %s %s %s\n",
               entries[i].id, entries[i].out ? ">" : "<",
               dstr, entries[i].text);
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
    printf("%-12s %-30s %-20s %s\n",
           "user_id", "name", "username", "mutual");
    for (int i = 0; i < count; i++) {
        printf("%-12lld %-30s %-20s %s\n",
               (long long)entries[i].user_id,
               entries[i].name,
               entries[i].username,
               entries[i].mutual ? "yes" : "no");
    }
    if (count == 0) puts("(no contacts)");
}

static void do_info(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                    const char *peer) {
    if (!peer || !*peer) { puts("usage: info <@name|numeric_id>"); return; }
    ResolvedPeer r = {0};
    int rc;
    if (is_numeric_id(peer)) {
        int64_t pid = (int64_t)strtoll(peer, NULL, 10);
        HistoryPeer hp = {0};
        rc = resolve_numeric_peer(cfg, s, t, pid, &hp);
        if (rc == 0) {
            r.id   = hp.peer_id;
            r.access_hash = hp.access_hash;
            r.have_hash   = (hp.access_hash != 0) ? 1 : 0;
            /* map kind */
            switch (hp.kind) {
            case HISTORY_PEER_USER:    r.kind = RESOLVED_KIND_USER;    break;
            case HISTORY_PEER_CHAT:    r.kind = RESOLVED_KIND_CHAT;    break;
            case HISTORY_PEER_CHANNEL: r.kind = RESOLVED_KIND_CHANNEL; break;
            default: r.kind = RESOLVED_KIND_UNKNOWN; break;
            }
            /* Fill title/username from dialogs cache */
            DialogEntry de = {0};
            if (domain_dialogs_find_by_id(pid, &de) == 0) {
                snprintf(r.title,    sizeof(r.title),    "%s", de.title);
                snprintf(r.username, sizeof(r.username), "%s", de.username);
            }
        }
    } else {
        rc = domain_resolve_username(cfg, s, t, peer, &r);
    }
    if (rc != 0) { puts("info: resolve failed"); return; }
    printf("type:         %s\nid:           %lld\n",
           resolved_label(r.kind), (long long)r.id);
    if (r.title[0])    printf("name:         %s\n", r.title);
    if (r.username[0]) printf("username:     @%s\n", r.username);
    printf("access_hash:  %s\n", r.have_hash ? "present" : "none");
}

/* Returns 1 if the token looks like a peer specifier:
 *   @username, a numeric id, or the literal "self". */
static int is_peer_token(const char *tok) {
    if (!tok || !*tok) return 0;
    if (*tok == '@') return tok[1] != '\0';
    if (!strcmp(tok, "self")) return 1;
    /* Pure numeric (possibly negative) → peer id */
    const char *p = tok;
    if (*p == '-') p++;
    if (!*p) return 0;
    while (*p >= '0' && *p <= '9') p++;
    return (*p == '\0');
}

static void do_search(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                      char *arg) {
    if (!arg || !*arg) {
        puts("usage: search [<peer>] <query>\n"
             "  <peer>  @username, numeric id, or 'self' (omit for global search)");
        return;
    }

    /* Split arg on the first whitespace to check for an optional peer token. */
    char peer_buf[128] = "";
    const char *query = arg;
    char *space = arg;
    while (*space && *space != ' ' && *space != '\t') space++;
    if (*space) {
        /* There is a second token — check if the first is a peer. */
        size_t pn = (size_t)(space - arg);
        char first[128] = "";
        if (pn < sizeof(first)) { memcpy(first, arg, pn); first[pn] = '\0'; }
        if (is_peer_token(first)) {
            memcpy(peer_buf, first, pn + 1);
            query = space;
            while (*query == ' ' || *query == '\t') query++;
        }
    }

    if (!*query) {
        puts("usage: search [<peer>] <query>");
        return;
    }

    HistoryEntry e[20] = {0};
    int n = 0;

    if (peer_buf[0]) {
        /* Per-peer search */
        HistoryPeer peer = {0};
        if (resolve_history_peer(cfg, s, t, peer_buf, &peer) != 0) {
            printf("search: cannot resolve '%s'\n", peer_buf);
            return;
        }
        if (domain_search_peer(cfg, s, t, &peer, query, 20, e, &n) != 0) {
            puts("search: request failed");
            return;
        }
    } else {
        /* Global search */
        if (domain_search_global(cfg, s, t, query, 20, e, &n) != 0) {
            puts("search: request failed");
            return;
        }
    }

    for (int i = 0; i < n; i++) {
        char dstr[20]; fmt_date(dstr, sizeof(dstr), e[i].date);
        printf("%d  %s  %s\n", e[i].id, dstr, e[i].text);
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

static void do_download(const ApiConfig *cfg, MtProtoSession *s, Transport *t,
                         char *arg) {
    /* Syntax: download <peer> <msg_id> [out] */
    if (!arg || !*arg) {
        puts("usage: download <peer> <msg_id> [out]");
        return;
    }
    char *rest = split_rest(arg);
    if (!*rest) { puts("usage: download <peer> <msg_id> [out]"); return; }
    char *out_arg = split_rest(rest);

    int32_t mid = atoi(rest);
    if (mid <= 0) { puts("download: <msg_id> must be positive"); return; }

    HistoryPeer peer = {0};
    if (resolve_history_peer(cfg, s, t, arg, &peer) != 0) {
        printf("download: cannot resolve '%s'\n", arg);
        return;
    }

    /* Fetch the single message using offset_id = msg_id + 1, limit = 1. */
    HistoryEntry entry = {0};
    int count = 0;
    if (domain_get_history(cfg, s, t, &peer, mid + 1, 1, &entry, &count) != 0
        || count == 0 || entry.id != mid) {
        printf("download: message %d not found in this peer\n", mid);
        return;
    }
    if (entry.media != MEDIA_PHOTO && entry.media != MEDIA_DOCUMENT) {
        printf("download: message %d has no downloadable photo/document "
               "(media kind=%d)\n", mid, (int)entry.media);
        return;
    }
    if (entry.media_info.access_hash == 0
        || entry.media_info.file_reference_len == 0) {
        puts("download: missing access_hash or file_reference");
        return;
    }

    /* Compose output path: explicit [out] > default cache path. */
    char path_buf[2048];
    const char *out_path = (*out_arg) ? out_arg : NULL;
    if (!out_path) {
        const char *cache = platform_cache_dir();
        if (!cache) cache = "/tmp";
        char dir_buf[1536];
        snprintf(dir_buf, sizeof(dir_buf), "%s/tg-cli/downloads", cache);
        fs_mkdir_p(dir_buf, 0700);
        if (entry.media == MEDIA_DOCUMENT) {
            const char *fn = entry.media_info.document_filename;
            if (fn[0]) {
                snprintf(path_buf, sizeof(path_buf), "%s/%s", dir_buf, fn);
            } else {
                snprintf(path_buf, sizeof(path_buf), "%s/doc-%lld",
                         dir_buf, (long long)entry.media_info.document_id);
            }
        } else {
            snprintf(path_buf, sizeof(path_buf), "%s/photo-%lld.jpg",
                     dir_buf, (long long)entry.media_info.photo_id);
        }
        out_path = path_buf;
    }

    if (domain_download_media_cross_dc(cfg, s, t, &entry.media_info,
                                        out_path) != 0) {
        puts("download: failed (see logs)");
        return;
    }

    /* Record in the media index so `history` can show inline paths. */
    int64_t media_id = (entry.media == MEDIA_DOCUMENT)
                     ? entry.media_info.document_id
                     : entry.media_info.photo_id;
    if (media_index_put(media_id, out_path) != 0) {
        puts("download: warning: failed to update media index");
    }

    printf("saved: %s\n", out_path);
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
        "  me, self                     Show own profile\n"
        "  dialogs, list [N]            List up to N dialogs (default 20)\n"
        "  history [<peer>] [N]         Saved Messages by default, or <peer>\n"
        "  contacts                     List my contacts\n"
        "  info <@peer>                 Resolve peer info\n"
        "  search [<peer>] <query>       Message search: per-peer or global (top 20)\n"
        "  poll                         One-shot updates.getDifference\n"
        "  read <peer>                  Mark peer's history as read\n"
        "  download <peer> <msg_id> [out]  Download photo or document from message\n"
        "\n"
        "Write commands:\n"
        "  send <peer> <text>           Send a text message\n"
        "  reply <peer> <msg_id> <text> Send as a reply to msg_id\n"
        "  edit <peer> <msg_id> <text>  Edit a previously sent message\n"
        "  delete, del <peer> <msg_id> [revoke]  Delete a message\n"
        "  forward, fwd <from> <to> <msg_id>     Forward one message\n"
        "  upload <peer> <path> [caption] Upload a file (document)\n"
        "\n"
        "Session:\n"
        "  help, ?                      Show this help\n"
        "  quit, exit, :q               Leave the TUI (Ctrl-D also exits)\n"
        "\n"
        "Launch flags (pass on the command line, not inside the REPL):\n"
        "  --help, -h                   Show this help and exit\n"
        "  --version, -v                Show version and exit\n"
        "  --tui                        Curses-style three-pane UI instead of REPL\n"
        "  --phone <number>             Pre-fill login phone (E.164)\n"
        "  --code <digits>              Pre-fill SMS/app code\n"
        "  --password <pass>            Pre-fill 2FA password\n"
        "  --logout                     Clear persisted session and exit\n"
        "  login [--api-id N --api-hash HEX] [--force]  First-run config wizard\n"
        "      Interactive when stdin is a TTY. From a script, pass both\n"
        "      flags; otherwise the command exits with 1 (never blocks on input).\n"
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
        if (!strcmp(cmd, "me") || !strcmp(cmd, "self")) { do_me(cfg, s, t); continue; }
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
        if (!strcmp(cmd, "search"))   { do_search(cfg, s, t, arg); continue; }  /* arg is mutable */
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
        if (!strcmp(cmd, "download")) { do_download(cfg, s, t, arg); continue; }

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
    /* Ensure SIGTERM / SIGHUP / SIGINT restore the terminal before exiting,
     * even if the default handler bypasses our RAII cleanup. */
    terminal_install_cleanup_handlers(raw);
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

/* Scan argv for --tui flag (used after full arg parse to decide mode). */
static int has_tui_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tui") == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    platform_normalize_argv(&argc, &argv);
    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-tui") != 0) {
        fprintf(stderr, "tg-tui: bootstrap failed\n");
        return 1;
    }

    /* Drop the session-scoped resolver cache when the user logs out so a
     * subsequent login does not see stale @peer → id mappings. */
    auth_logout_set_cache_flush_cb(resolve_cache_flush);

    /* --logout: invalidate session server-side, then wipe the local file. */
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
                            "tg-tui: logout: cannot connect to DC%d, clearing local session",
                            loaded_dc);
                        session_store_clear();
                    }
                } else {
                    session_store_clear();
                }
            } else {
                session_store_clear();
            }
            fprintf(stderr, "tg-tui: persisted session cleared.\n");
            app_shutdown(&ctx);
            return 0;
        }
    }

    /* Handle `login` subcommand before credentials are loaded.
     * Syntax: tg-tui login [--api-id N --api-hash HEX [--force]] */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "login") == 0) {
            /* Parse any --api-id / --api-hash / --force flags after "login". */
            const char *api_id_str  = NULL;
            const char *api_hash_str = NULL;
            int force = 0;
            for (int j = i + 1; j < argc; j++) {
                if (strcmp(argv[j], "--api-id") == 0 && j + 1 < argc)
                    { api_id_str = argv[++j]; }
                else if (strcmp(argv[j], "--api-hash") == 0 && j + 1 < argc)
                    { api_hash_str = argv[++j]; }
                else if (strcmp(argv[j], "--force") == 0)
                    { force = 1; }
            }
            int wrc;
            if (api_id_str || api_hash_str)
                wrc = config_wizard_run_batch(api_id_str, api_hash_str, force);
            else
                wrc = config_wizard_run_interactive(force);
            app_shutdown(&ctx);
            return wrc != 0 ? 1 : 0;
        }
    }

    /* Handle --help / --version before anything else. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            app_shutdown(&ctx);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            arg_print_version();
            app_shutdown(&ctx);
            return 0;
        }
    }

    /* Parse --phone / --code / --password / --tui flags. */
    const char *opt_phone    = NULL;
    const char *opt_code     = NULL;
    const char *opt_password = NULL;
    int tui_mode = has_tui_flag(argc, argv);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--phone") == 0 && i + 1 < argc) {
            opt_phone = argv[++i];
        } else if (strcmp(argv[i], "--code") == 0 && i + 1 < argc) {
            opt_code = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            opt_password = argv[++i];
        }
    }

    ApiConfig cfg;
    if (credentials_load(&cfg) != 0) {
        app_shutdown(&ctx);
        return 1;
    }

    LineHistory hist;
    rl_history_init(&hist);

    PromptCtx pctx = {
        .hist     = &hist,
        .phone    = opt_phone,
        .code     = opt_code,
        .password = opt_password,
    };
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
