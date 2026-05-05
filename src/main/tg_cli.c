/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file main/tg_cli.c
 * @brief tg-cli — batch read+write Telegram CLI entry point.
 *
 * Thin wrapper over tg-domain-read + tg-domain-write (ADR-0005). Exposes
 * every read command (identical to tg-cli-ro) plus all write commands.
 * For the interactive REPL/TUI use tg-tui(1).
 */

#include "app/bootstrap.h"
#include "app/auth_flow.h"
#include "app/credentials.h"
#include "app/config_wizard.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "infrastructure/auth_logout.h"
#include "infrastructure/updates_state_store.h"
#include "infrastructure/media_index.h"
#include "logger.h"
#include "arg_parse.h"
#include "json_util.h"
#include "platform/path.h"

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

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SEC-01: set once at startup; 1 when stdout is a real terminal. */
static int g_stdout_is_tty = 0;

static void fmt_date(char *buf, size_t cap, int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, cap, "%Y-%m-%d %H:%M", &tm);
}

/**
 * @brief Sanitize @p src into @p dst for terminal display (SEC-01).
 */
static void tty_sanitize(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    if (!g_stdout_is_tty) {
        size_t i = 0;
        while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
        return;
    }
    size_t i = 0;
    while (*src && i + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        if ((c < 0x20 && c != 0x09 && c != 0x0A) || c == 0x7F || c == 0x9B)
            dst[i++] = '.';
        else
            dst[i++] = (char)c;
    }
    dst[i] = '\0';
}

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

/* ---- Read-command helpers (matching tg_cli_ro) ---- */

static const char *peer_kind_name(DialogPeerKind k) {
    switch (k) {
    case DIALOG_PEER_USER:    return "user";
    case DIALOG_PEER_CHAT:    return "chat";
    case DIALOG_PEER_CHANNEL: return "channel";
    default:                  return "unknown";
    }
}

static const char *resolved_kind_name(ResolvedKind k) {
    switch (k) {
    case RESOLVED_KIND_USER:    return "user";
    case RESOLVED_KIND_CHAT:    return "chat";
    case RESOLVED_KIND_CHANNEL: return "channel";
    default:                    return "unknown";
    }
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

#define WATCH_BACKOFF_CAP_S  300
#define WATCH_BACKOFF_INIT_S   5
#define WATCH_PEERS_MAX 64

static int watch_peers_resolve(const ApiConfig *cfg, MtProtoSession *s,
                                Transport *t, const char *spec,
                                int64_t *ids, int cap) {
    if (!spec || !*spec) return 0;
    char buf[4096];
    size_t slen = strlen(spec);
    if (slen >= sizeof(buf)) {
        fprintf(stderr, "watch: --peers value too long\n");
        return -1;
    }
    memcpy(buf, spec, slen + 1);
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < cap) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        if (!*tok) { tok = strtok(NULL, ","); continue; }
        if (strcmp(tok, "self") == 0) { ids[n++] = 0; tok = strtok(NULL, ","); continue; }
        char *numend = NULL;
        long long numid = strtoll(tok, &numend, 10);
        if (numend && *numend == '\0') { ids[n++] = (int64_t)numid; tok = strtok(NULL, ","); continue; }
        ResolvedPeer rp = {0};
        if (domain_resolve_username(cfg, s, t, tok, &rp) != 0) {
            fprintf(stderr, "watch: --peers: cannot resolve '%s'\n", tok);
            return -1;
        }
        ids[n++] = rp.id;
        tok = strtok(NULL, ",");
    }
    return n;
}

static int watch_peer_allowed(const int64_t *ids, int n, int64_t peer_id) {
    if (n == 0) return 1;
    for (int i = 0; i < n; i++) if (ids[i] == peer_id) return 1;
    return 0;
}

static int resolve_peer_arg(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, const char *peer_arg,
                             HistoryPeer *out);

/* Returns 1 if s looks like a decimal integer (optionally negative). */
static int is_numeric_id(const char *s) {
    if (!s || !*s) return 0;
    const char *p = (*s == '-') ? s + 1 : s;
    if (!*p) return 0;
    for (; *p; p++) if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* Resolve a numeric peer_id via the dialogs cache.
 * If the cache is cold, fetches the inbox first to populate it. */
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

static int cmd_me(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;
    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    transport_close(&t);
    if (rc != 0) { fprintf(stderr, "tg-cli me: failed\n"); return 1; }
    if (args->json) {
        printf("{\"id\":%lld,\"username\":\"%s\",\"first_name\":\"%s\","
               "\"last_name\":\"%s\",\"phone\":\"%s\",\"premium\":%s,\"bot\":%s}\n",
               (long long)me.id, me.username, me.first_name, me.last_name, me.phone,
               me.is_premium ? "true" : "false", me.is_bot ? "true" : "false");
    } else {
        char s1[128], s2[128], s3[128];
        tty_sanitize(s1, sizeof(s1), me.username);
        tty_sanitize(s2, sizeof(s2), me.first_name);
        tty_sanitize(s3, sizeof(s3), me.last_name);
        printf("id:       %lld\n", (long long)me.id);
        if (me.username[0])   printf("username: @%s\n", s1);
        if (me.first_name[0] || me.last_name[0])
            printf("name:     %s%s%s\n", s2, me.last_name[0] ? " " : "", s3);
        if (me.phone[0])      printf("phone:    +%s\n", me.phone);
        printf("premium:  %s\n", me.is_premium ? "yes" : "no");
        if (me.is_bot)        printf("bot:      yes\n");
    }
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
    int rc = domain_get_dialogs(&cfg, &s, &t, limit, args->archived, entries, &count, NULL);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli dialogs: failed (see logs)\n");
        free(entries); return 1;
    }
    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"type\":\"%s\",\"id\":%lld,\"title\":\"%s\","
                   "\"username\":\"%s\",\"top\":%d,\"unread\":%d}",
                   peer_kind_name(entries[i].kind),
                   (long long)entries[i].peer_id,
                   entries[i].title, entries[i].username,
                   entries[i].top_message_id, entries[i].unread_count);
        }
        printf("]\n");
    } else {
        printf("%-8s %6s %-32s %s\n", "type", "unread", "title", "@username / id");
        for (int i = 0; i < count; i++) {
            char stitle[128], susername[64];
            tty_sanitize(stitle, sizeof(stitle), entries[i].title);
            tty_sanitize(susername, sizeof(susername), entries[i].username);
            const char *title = entries[i].title[0] ? stitle : "(no title)";
            if (entries[i].username[0])
                printf("%-8s %6d %-32s @%s\n",
                       peer_kind_name(entries[i].kind), entries[i].unread_count,
                       title, susername);
            else
                printf("%-8s %6d %-32s %lld\n",
                       peer_kind_name(entries[i].kind), entries[i].unread_count,
                       title, (long long)entries[i].peer_id);
        }
    }
    free(entries);
    return 0;
}

static int cmd_history(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;
    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t); return 1;
    }
    int limit = args->limit > 0 ? args->limit : 20;
    if (limit > 100) limit = 100;
    int offset = args->offset > 0 ? args->offset : 0;
    HistoryEntry *entries = calloc((size_t)limit, sizeof(HistoryEntry));
    if (!entries) { transport_close(&t); return 1; }
    int count = 0;
    int rc = domain_get_history(&cfg, &s, &t, &peer, offset, limit, entries, &count);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli history: failed (see logs)\n");
        free(entries); return 1;
    }
    static const char *media_label[] = {
        [MEDIA_NONE] = "", [MEDIA_EMPTY] = "", [MEDIA_UNSUPPORTED] = "unsup",
        [MEDIA_PHOTO] = "photo", [MEDIA_DOCUMENT] = "document", [MEDIA_GEO] = "geo",
        [MEDIA_CONTACT] = "contact", [MEDIA_VENUE] = "venue",
        [MEDIA_GEO_LIVE] = "geo_live", [MEDIA_DICE] = "dice",
        [MEDIA_WEBPAGE] = "webpage", [MEDIA_POLL] = "poll",
        [MEDIA_INVOICE] = "invoice", [MEDIA_STORY] = "story",
        [MEDIA_GIVEAWAY] = "giveaway", [MEDIA_GAME] = "game",
        [MEDIA_PAID] = "paid", [MEDIA_OTHER] = "other",
    };
    if (args->json) {
        printf("[");
        int first = 1;
        for (int i = 0; i < count; i++) {
            if (args->no_media && entries[i].media != MEDIA_NONE
                    && entries[i].media != MEDIA_EMPTY
                    && entries[i].text[0] == '\0') continue;
            if (!first) printf(",");
            first = 0;
            const char *ml = args->no_media ? "" : media_label[entries[i].media];
            long long mid = args->no_media ? 0LL : (long long)entries[i].media_id;
            char cached_path[2048] = {0};
            int has_cache = 0;
            if (!args->no_media && entries[i].media_id != 0
                && (entries[i].media == MEDIA_PHOTO || entries[i].media == MEDIA_DOCUMENT))
                has_cache = (media_index_get(entries[i].media_id, cached_path, sizeof(cached_path)) == 1);
            printf("{\"id\":%d,\"out\":%s,\"date\":%lld,\"text\":\"%s\","
                   "\"complex\":%s,\"media\":\"%s\",\"media_id\":%lld"
                   ",\"media_path\":\"%s\"}",
                   entries[i].id, entries[i].out ? "true" : "false",
                   (long long)entries[i].date, entries[i].text,
                   entries[i].complex ? "true" : "false",
                   ml, mid, has_cache ? cached_path : "");
        }
        printf("]\n");
    } else {
        int printed = 0;
        for (int i = 0; i < count; i++) {
            const char *ml = media_label[entries[i].media];
            char cached_path[2048] = {0};
            int has_cache = 0;
            if (entries[i].media_id != 0
                && (entries[i].media == MEDIA_PHOTO || entries[i].media == MEDIA_DOCUMENT))
                has_cache = (media_index_get(entries[i].media_id, cached_path, sizeof(cached_path)) == 1);
            char stext[HISTORY_TEXT_MAX];
            tty_sanitize(stext, sizeof(stext), entries[i].text);
            char dstr[20];
            fmt_date(dstr, sizeof(dstr), entries[i].date);
            if (ml[0] && args->no_media) {
                if (entries[i].text[0] == '\0') continue;
                printf("[%d] %s %s %s\n",
                       entries[i].id, entries[i].out ? ">" : "<", dstr, stext);
                printed++;
            } else if (ml[0]) {
                if (has_cache)
                    printf("[%d] %s %s [%s: %s] %s\n",
                           entries[i].id, entries[i].out ? ">" : "<",
                           dstr, ml, cached_path, stext);
                else
                    printf("[%d] %s %s [%s] %s\n",
                           entries[i].id, entries[i].out ? ">" : "<",
                           dstr, ml, stext);
                printed++;
            } else {
                printf("[%d] %s %s %s\n",
                       entries[i].id, entries[i].out ? ">" : "<", dstr, stext);
                printed++;
            }
        }
        if (printed == 0) printf("(no messages)\n");
    }
    free(entries);
    return 0;
}

static int cmd_search(const ArgResult *args) {
    if (!args->query) { fprintf(stderr, "tg-cli search: <query> required\n"); return 1; }
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
        rc = domain_search_peer(&cfg, &s, &t, &peer, args->query, limit, entries, &count);
    } else {
        rc = domain_search_global(&cfg, &s, &t, args->query, limit, entries, &count);
    }
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli search: failed (see logs)\n");
        free(entries); return 1;
    }
    if (args->json) {
        char esc[HISTORY_TEXT_MAX * 6 + 1];
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            json_escape_str(esc, sizeof(esc), entries[i].text);
            printf("{\"id\":%d,\"out\":%s,\"date\":%lld,\"text\":\"%s\","
                   "\"complex\":%s}",
                   entries[i].id, entries[i].out ? "true" : "false",
                   (long long)entries[i].date, esc,
                   entries[i].complex ? "true" : "false");
        }
        printf("]\n");
    } else {
        printf("%-8s %-4s %-17s %s\n", "id", "out", "date", "text");
        for (int i = 0; i < count; i++) {
            char stext[HISTORY_TEXT_MAX];
            tty_sanitize(stext, sizeof(stext), entries[i].text);
            char dstr[20]; fmt_date(dstr, sizeof(dstr), entries[i].date);
            printf("%-8d %-4s %-17s %s\n",
                   entries[i].id, entries[i].out ? "yes" : "no", dstr, stext);
        }
        if (count == 0) printf("(no matches)\n");
    }
    free(entries);
    return 0;
}

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
        fprintf(stderr, "tg-cli contacts: failed (see logs)\n");
        free(entries); return 1;
    }
    if (args->json) {
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            printf("{\"user_id\":%lld,\"mutual\":%s"
                   ",\"name\":\"%s\",\"username\":\"%s\"}",
                   (long long)entries[i].user_id,
                   entries[i].mutual ? "true" : "false",
                   entries[i].name,
                   entries[i].username);
        }
        printf("]\n");
    } else {
        printf("%-12s %-30s %-20s %s\n",
               "user_id", "name", "username", "mutual");
        for (int i = 0; i < count; i++)
            printf("%-12lld %-30s %-20s %s\n",
                   (long long)entries[i].user_id,
                   entries[i].name,
                   entries[i].username,
                   entries[i].mutual ? "yes" : "no");
        if (count == 0) printf("(no contacts)\n");
    }
    free(entries);
    return 0;
}

static int cmd_user_info(const ArgResult *args) {
    if (!args->peer) { fprintf(stderr, "tg-cli user-info: <peer> required\n"); return 1; }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;
    ResolvedPeer r = {0};
    int rc = domain_resolve_username(&cfg, &s, &t, args->peer, &r);
    transport_close(&t);
    if (rc != 0) { fprintf(stderr, "tg-cli user-info: resolve failed\n"); return 1; }
    if (args->json) {
        printf("{\"type\":\"%s\",\"id\":%lld,\"name\":\"%s\","
               "\"username\":\"%s\",\"access_hash\":\"%s\"}\n",
               resolved_kind_name(r.kind), (long long)r.id,
               r.title, r.username, r.have_hash ? "present" : "none");
    } else {
        char su[64], st[128];
        tty_sanitize(su, sizeof(su), r.username);
        tty_sanitize(st, sizeof(st), r.title);
        printf("type:         %s\n", resolved_kind_name(r.kind));
        printf("id:           %lld\n", (long long)r.id);
        if (r.title[0])    printf("name:         %s\n", st);
        if (r.username[0]) printf("username:     @%s\n", su);
        printf("access_hash:  %s\n", r.have_hash ? "present" : "none");
    }
    return 0;
}

static int cmd_watch(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;
    signal(SIGINT, on_sigint);
    signal(SIGPIPE, SIG_IGN);
    int64_t peer_filter[WATCH_PEERS_MAX];
    int peer_filter_n = 0;
    if (args->watch_peers) {
        peer_filter_n = watch_peers_resolve(&cfg, &s, &t, args->watch_peers,
                                            peer_filter, WATCH_PEERS_MAX);
        if (peer_filter_n < 0) { transport_close(&t); return 1; }
        if (!args->quiet)
            fprintf(stderr, "watch: filtering to %d peer(s)\n", peer_filter_n);
    }
    UpdatesState state = {0};
    int loaded = updates_state_load(&state);
    if (loaded != 0) {
        if (!args->quiet) fprintf(stderr, "watch: no persisted state, fetching from server\n");
        if (domain_updates_state(&cfg, &s, &t, &state) != 0) {
            fprintf(stderr, "tg-cli watch: getState failed\n");
            transport_close(&t); return 1;
        }
        updates_state_save(&state);
    }
    int interval = args->watch_interval > 0 ? args->watch_interval : 30;
    if (!args->quiet)
        fprintf(stderr, "watch: seeded pts=%d qts=%d date=%lld, "
                        "polling every %ds (SIGINT to quit)\n",
                        state.pts, state.qts, (long long)state.date, interval);
    int backoff = 0;
    while (!g_stop) {
        UpdatesDifference diff = {0};
        if (domain_updates_difference(&cfg, &s, &t, &state, &diff) != 0) {
            if (backoff == 0) backoff = WATCH_BACKOFF_INIT_S;
            else if (backoff < WATCH_BACKOFF_CAP_S)
                backoff = (backoff * 2 < WATCH_BACKOFF_CAP_S) ? backoff * 2 : WATCH_BACKOFF_CAP_S;
            fprintf(stderr, "watch: getDifference failed, retrying in %ds\n", backoff);
            for (int i = 0; i < backoff && !g_stop; i++) sleep(1);
            continue;
        }
        backoff = 0;
        state = diff.next_state;
        updates_state_save(&state);
        if (args->json) {
            char esc[HISTORY_TEXT_MAX * 6 + 1];
            for (int i = 0; i < diff.new_messages_count; i++) {
                if (!watch_peer_allowed(peer_filter, peer_filter_n, diff.new_messages[i].peer_id)) continue;
                json_escape_str(esc, sizeof(esc), diff.new_messages[i].text);
                if (printf("{\"peer_id\":%lld,\"msg_id\":%d,\"date\":%lld,\"text\":\"%s\"}\n",
                           (long long)diff.new_messages[i].peer_id, diff.new_messages[i].id,
                           (long long)diff.new_messages[i].date, esc) < 0
                    || fflush(stdout) != 0) {
                    if (errno == EPIPE) { g_stop = 1; break; }
                }
            }
        } else {
            int printed = 0;
            for (int i = 0; i < diff.new_messages_count; i++) {
                if (!watch_peer_allowed(peer_filter, peer_filter_n, diff.new_messages[i].peer_id)) continue;
                char stext[HISTORY_TEXT_MAX];
                tty_sanitize(stext, sizeof(stext), diff.new_messages[i].text);
                if (printf("[%d] %lld %s\n", diff.new_messages[i].id,
                           (long long)diff.new_messages[i].date,
                           diff.new_messages[i].complex ? "(complex \xe2\x80\x94 text not parsed)" : stext) < 0) {
                    if (errno == EPIPE) { g_stop = 1; break; }
                }
                printed++;
            }
            if (!g_stop && printed == 0 && !args->quiet) {
                if (printf("(no new messages; pts=%d date=%lld)\n",
                           state.pts, (long long)state.date) < 0 && errno == EPIPE)
                    g_stop = 1;
            }
        }
        if (!g_stop && fflush(stdout) != 0 && errno == EPIPE) g_stop = 1;
        for (int i = 0; i < interval && !g_stop; i++) sleep(1);
    }
    transport_close(&t);
    return 0;
}

static int cmd_download(const ArgResult *args, const AppContext *ctx) {
    if (!args->peer || args->msg_id <= 0) {
        fprintf(stderr, "tg-cli download: <peer> and positive <msg_id> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;
    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t); return 1;
    }
    HistoryEntry entry = {0};
    int count = 0;
    int rc = domain_get_history(&cfg, &s, &t, &peer, args->msg_id + 1, 1, &entry, &count);
    if (rc != 0 || count == 0 || entry.id != args->msg_id) {
        fprintf(stderr, "tg-cli download: message %d not found\n", args->msg_id);
        transport_close(&t); return 1;
    }
    if (entry.media != MEDIA_PHOTO && entry.media != MEDIA_DOCUMENT) {
        fprintf(stderr, "tg-cli download: message %d has no downloadable media\n", args->msg_id);
        transport_close(&t); return 1;
    }
    if (entry.media_info.access_hash == 0 || entry.media_info.file_reference_len == 0) {
        fprintf(stderr, "tg-cli download: missing access_hash or file_reference\n");
        transport_close(&t); return 1;
    }
    char path_buf[2048];
    const char *out_path = args->out_path;
    if (!out_path) {
        const char *cache = ctx->cache_dir ? ctx->cache_dir : "/tmp";
        char dir_buf[1536];
        snprintf(dir_buf, sizeof(dir_buf), "%s/downloads", cache);
        fs_mkdir_p(dir_buf, 0700);
        if (entry.media == MEDIA_DOCUMENT) {
            const char *fn = entry.media_info.document_filename;
            if (fn[0]) snprintf(path_buf, sizeof(path_buf), "%s/%s", dir_buf, fn);
            else snprintf(path_buf, sizeof(path_buf), "%s/doc-%lld",
                          dir_buf, (long long)entry.media_info.document_id);
        } else {
            snprintf(path_buf, sizeof(path_buf), "%s/photo-%lld.jpg",
                     dir_buf, (long long)entry.media_info.photo_id);
        }
        out_path = path_buf;
    }
    rc = domain_download_media_cross_dc(&cfg, &s, &t, &entry.media_info, out_path);
    transport_close(&t);
    if (rc != 0) { fprintf(stderr, "tg-cli download: failed (see logs)\n"); return 1; }
    int64_t media_id = (entry.media == MEDIA_DOCUMENT)
                     ? entry.media_info.document_id : entry.media_info.photo_id;
    media_index_put(media_id, out_path);
    if (args->json)
        printf("{\"saved\":\"%s\",\"kind\":\"%s\",\"id\":%lld}\n",
               out_path, (entry.media == MEDIA_DOCUMENT) ? "document" : "photo",
               (long long)media_id);
    else if (!args->quiet)
        printf("saved: %s\n", out_path);
    return 0;
}

/* ---- Peer resolution (forward declaration above; definition here) ---- */
static int resolve_peer_arg(const ApiConfig *cfg, MtProtoSession *s,
                             Transport *t, const char *peer_arg,
                             HistoryPeer *out) {
    if (!peer_arg || strcmp(peer_arg, "self") == 0) {
        out->kind = HISTORY_PEER_SELF;
        return 0;
    }
    if (is_numeric_id(peer_arg)) {
        int64_t pid = (int64_t)strtoll(peer_arg, NULL, 10);
        return resolve_numeric_peer(cfg, s, t, pid, out);
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
        "Batch-mode Telegram CLI — read and write. Always non-interactive.\n"
        "For the interactive REPL/TUI use tg-tui(1).\n"
        "\n"
        "Read subcommands:\n"
        "  me (or self)                         Show own profile (US-05)\n"
        "  dialogs  [--limit N] [--archived]    List dialogs (US-04)\n"
        "  history  <peer> [--limit N] [--offset N] [--no-media]  Fetch history (US-06)\n"
        "  search   [<peer>] <query> [--limit N]  Search messages (US-10)\n"
        "  contacts                             List contacts (US-09)\n"
        "  user-info <peer>                     User/channel info (US-09)\n"
        "  watch    [--peers X,Y] [--interval N]  Watch updates (US-07)\n"
        "  download <peer> <msg_id> [--out PATH]  Download photo/document (US-08)\n"
        "\n"
        "Write subcommands:\n"
        "  send <peer> [--reply N] <message>    Send a text message (US-12)\n"
        "  send <peer> --stdin                  Read message body from stdin\n"
        "  read <peer> [--max-id N]             Mark peer's history as read (US-12)\n"
        "  edit <peer> <msg_id> <text>          Edit a message (US-13)\n"
        "  delete <peer> <msg_id> [--revoke]    Delete a message (US-13)\n"
        "  forward <from> <to> <msg_id>         Forward a message (US-13)\n"
        "  send-file|upload <peer> <path> [--caption T]  Upload a file (US-14)\n"
        "\n"
        "Session:\n"
        "  login [--api-id N --api-hash HEX] [--force]  First-run config wizard\n"
        "      Interactive when stdin is a TTY. From a script, pass both\n"
        "      flags; otherwise the command exits with 1 (never blocks on input).\n"
        "  --logout                             Clear persisted session and exit\n"
        "\n"
        "Global flags:\n"
        "  --config <path>     Use non-default config file\n"
        "  --json              Emit JSON output where supported\n"
        "  --quiet             Suppress informational output\n"
        "  --help, -h          Show this help and exit\n"
        "  --version, -v       Show version and exit\n"
        "\n"
        "Login flags (for session authentication):\n"
        "  --phone <number>    E.g. +15551234567\n"
        "  --code <digits>     SMS/app code\n"
        "  --password <pass>   2FA password\n"
        "\n"
        "Credentials:\n"
        "  TG_CLI_API_ID / TG_CLI_API_HASH env vars, or\n"
        "  api_id= / api_hash= in ~/.config/tg-cli/config.ini\n"
        "  (run 'tg-cli login' to set up config.ini interactively)\n"
        "\n"
        "Examples:\n"
        "  tg-cli login --api-id 12345 --api-hash deadbeef...  # batch setup\n"
        "  tg-cli me\n"
        "  tg-cli dialogs --limit 50\n"
        "  tg-cli history @friend --limit 20\n"
        "  tg-cli send @friend 'Hello!'\n"
        "  echo 'Hi' | tg-cli send @friend\n"
        "\n"
        "Note: date fields in output are Unix epoch seconds; use 'date -d @$ts' to format.\n"
        "See man tg-cli(1) for the full reference.\n"
    );
}

int main(int argc, char **argv) {
    platform_normalize_argv(&argc, &argv);
    g_stdout_is_tty = isatty(STDOUT_FILENO);
    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-cli") != 0) {
        fprintf(stderr, "tg-cli: bootstrap failed\n");
        return 1;
    }

    /* Drop the session-scoped resolver cache when the user logs out so a
     * subsequent login does not see stale @peer → id mappings. */
    auth_logout_set_cache_flush_cb(resolve_cache_flush);

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
        /* Read commands */
        case CMD_ME:        exit_code = cmd_me(&args); break;
        case CMD_DIALOGS:   exit_code = cmd_dialogs(&args); break;
        case CMD_HISTORY:   exit_code = cmd_history(&args); break;
        case CMD_SEARCH:    exit_code = cmd_search(&args); break;
        case CMD_CONTACTS:  exit_code = cmd_contacts(&args); break;
        case CMD_USER_INFO: exit_code = cmd_user_info(&args); break;
        case CMD_WATCH:     exit_code = cmd_watch(&args); break;
        case CMD_DOWNLOAD:  exit_code = cmd_download(&args, &ctx); break;
        /* Write commands */
        case CMD_SEND:      exit_code = cmd_send(&args); break;
        case CMD_READ:      exit_code = cmd_read(&args); break;
        case CMD_EDIT:      exit_code = cmd_edit(&args); break;
        case CMD_DELETE:    exit_code = cmd_delete(&args); break;
        case CMD_FORWARD:   exit_code = cmd_forward(&args); break;
        case CMD_SEND_FILE: exit_code = cmd_send_file(&args); break;
        /* Session */
        case CMD_LOGIN:
            if (args.api_id_str || args.api_hash_str) {
                exit_code = config_wizard_run_batch(args.api_id_str,
                                                     args.api_hash_str,
                                                     args.force) != 0 ? 1 : 0;
            } else {
                exit_code = config_wizard_run_interactive(args.force) != 0 ? 1 : 0;
            }
            /* Do NOT call session_bringup for login — config may not exist yet. */
            app_shutdown(&ctx);
            return exit_code;
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
