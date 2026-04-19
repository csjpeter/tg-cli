/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

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
#include "app/config_wizard.h"
#include "app/credentials.h"
#include "app/dc_config.h"
#include "app/session_store.h"
#include "infrastructure/auth_logout.h"
#include "logger.h"
#include "arg_parse.h"
#include "platform/path.h"
#include "json_util.h"

#include "domain/read/self.h"
#include "domain/read/dialogs.h"
#include "domain/read/history.h"
#include "domain/read/updates.h"
#include "domain/read/user_info.h"
#include "domain/read/search.h"
#include "domain/read/contacts.h"
#include "domain/read/media.h"
#include "infrastructure/updates_state_store.h"
#include "infrastructure/media_index.h"
#include "fs_util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* SEC-01: set once at startup; 1 when stdout is a real terminal. */
static int g_stdout_is_tty = 0;

/**
 * @brief Sanitize @p src into @p dst for terminal display (SEC-01).
 *
 * When stdout is a tty, replaces control characters (< 0x20 except \\t and
 * \\n), DEL (0x7F), and the 8-bit CSI introducer (0x9B) with '.' to prevent
 * ANSI escape injection.  When stdout is a pipe/redirect the original bytes
 * are copied unchanged so scripts receive binary-safe output.
 *
 * @param dst  Output buffer (NUL-terminated on return).
 * @param cap  Capacity of @p dst in bytes (including NUL).
 * @param src  Input string (user-controlled).
 */
static void tty_sanitize(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    if (!g_stdout_is_tty) {
        /* Pipe/redirect: preserve raw bytes for scripts. */
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
    char s1[128], s2[128], s3[128];
    tty_sanitize(s1, sizeof(s1), me->username);
    tty_sanitize(s2, sizeof(s2), me->first_name);
    tty_sanitize(s3, sizeof(s3), me->last_name);
    printf("id:       %lld\n", (long long)me->id);
    if (me->username[0])   printf("username: @%s\n", s1);
    if (me->first_name[0] || me->last_name[0])
        printf("name:     %s%s%s\n", s2, me->last_name[0] ? " " : "", s3);
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

/** Maximum sleep between poll retries on error (5 minutes). */
#define WATCH_BACKOFF_CAP_S  300
/** Initial sleep on first error. */
#define WATCH_BACKOFF_INIT_S   5

/** Maximum number of peers accepted by --peers. */
#define WATCH_PEERS_MAX 64

/**
 * @brief Parse and resolve the --peers comma-separated list.
 *
 * Iterates over comma-separated tokens from @p spec.  Each token may be
 * a numeric id (positive integer), "@username" / "username", or "self"
 * (treated as peer_id 0 which matches messages with peer_id == 0).
 *
 * Resolved peer ids are stored in @p ids (capacity @p cap).
 *
 * @return Number of ids stored in @p ids, or -1 on hard error.
 */
static int watch_peers_resolve(const ApiConfig *cfg, MtProtoSession *s,
                                Transport *t, const char *spec,
                                int64_t *ids, int cap) {
    if (!spec || !*spec) return 0;

    /* Work on a mutable copy so strtok can modify it. */
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
        /* Strip leading/trailing whitespace. */
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

        if (!*tok) { tok = strtok(NULL, ","); continue; }

        /* "self" → peer_id 0 (Saved Messages; peer_id is 0 for self messages
         * because getDifference does not expose peer for them). */
        if (strcmp(tok, "self") == 0) {
            ids[n++] = 0;
            tok = strtok(NULL, ",");
            continue;
        }

        /* Pure numeric id (optionally negative for legacy chats). */
        char *numend = NULL;
        long long numid = strtoll(tok, &numend, 10);
        if (numend && *numend == '\0') {
            ids[n++] = (int64_t)numid;
            tok = strtok(NULL, ",");
            continue;
        }

        /* @username or username — resolve via domain_resolve_username. */
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

/** Return 1 if @p peer_id matches any entry in @p ids[0..n-1], else 0.
 *  When @p n == 0 (no filter), always returns 1. */
static int watch_peer_allowed(const int64_t *ids, int n, int64_t peer_id) {
    if (n == 0) return 1;
    for (int i = 0; i < n; i++) {
        if (ids[i] == peer_id) return 1;
    }
    return 0;
}

static int cmd_watch(const ArgResult *args) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    signal(SIGINT,  on_sigint);
    /* Ignore SIGPIPE so write(2) returns EPIPE instead of killing the
     * process when the downstream pipe consumer exits early (e.g. head). */
    signal(SIGPIPE, SIG_IGN);

    /* Resolve --peers filter once up front. */
    int64_t peer_filter[WATCH_PEERS_MAX];
    int     peer_filter_n = 0;
    if (args->watch_peers) {
        peer_filter_n = watch_peers_resolve(&cfg, &s, &t,
                                            args->watch_peers,
                                            peer_filter, WATCH_PEERS_MAX);
        if (peer_filter_n < 0) {
            transport_close(&t);
            return 1;
        }
        if (!args->quiet)
            fprintf(stderr, "watch: filtering to %d peer(s)\n", peer_filter_n);
    }

    UpdatesState state = {0};

    /* Load persisted state; fall back to updates.getState if missing. */
    int loaded = updates_state_load(&state);
    if (loaded != 0) {
        if (!args->quiet)
            fprintf(stderr, "watch: no persisted state, fetching from server\n");
        if (domain_updates_state(&cfg, &s, &t, &state) != 0) {
            fprintf(stderr, "tg-cli-ro watch: getState failed\n");
            transport_close(&t);
            return 1;
        }
        /* Persist immediately so the next invocation can skip getState. */
        updates_state_save(&state);
    }

    int interval = args->watch_interval > 0 ? args->watch_interval : 30;
    if (!args->quiet)
        fprintf(stderr, "watch: seeded pts=%d qts=%d date=%lld, "
                        "polling every %ds (SIGINT to quit)\n",
                        state.pts, state.qts, (long long)state.date, interval);

    int backoff = 0; /* seconds of extra sleep on error; 0 means no error */

    while (!g_stop) {
        UpdatesDifference diff = {0};
        if (domain_updates_difference(&cfg, &s, &t, &state, &diff) != 0) {
            /* Exponential backoff: 5s → 10s → 20s … capped at 5 min. */
            if (backoff == 0)
                backoff = WATCH_BACKOFF_INIT_S;
            else if (backoff < WATCH_BACKOFF_CAP_S)
                backoff = (backoff * 2 < WATCH_BACKOFF_CAP_S)
                          ? backoff * 2 : WATCH_BACKOFF_CAP_S;
            fprintf(stderr,
                    "watch: getDifference failed, retrying in %ds (backoff)\n",
                    backoff);
            for (int i = 0; i < backoff && !g_stop; i++) sleep(1);
            continue;
        }

        /* Success — reset backoff. */
        backoff = 0;
        state = diff.next_state;

        /* Persist updated state after every successful poll. */
        updates_state_save(&state);

        if (args->json) {
            /* NDJSON: emit one JSON object per new message, one per line.
             * Each line is flushed immediately so pipes get live data.
             * Schema: {"peer_id":<int>,"msg_id":<int>,"date":<int>,"text":"<str>"} */
            char esc[HISTORY_TEXT_MAX * 6 + 1]; /* worst-case: every byte → \uXXXX */
            for (int i = 0; i < diff.new_messages_count; i++) {
                if (!watch_peer_allowed(peer_filter, peer_filter_n,
                                        diff.new_messages[i].peer_id))
                    continue;
                json_escape_str(esc, sizeof(esc), diff.new_messages[i].text);
                if (printf("{\"peer_id\":%lld,\"msg_id\":%d,\"date\":%lld,\"text\":\"%s\"}\n",
                           (long long)diff.new_messages[i].peer_id,
                           diff.new_messages[i].id,
                           (long long)diff.new_messages[i].date,
                           esc) < 0
                    || fflush(stdout) != 0) {
                    if (errno == EPIPE) {
                        /* Downstream consumer exited — clean termination. */
                        g_stop = 1;
                        break;
                    }
                }
            }
        } else {
            int printed = 0;
            for (int i = 0; i < diff.new_messages_count; i++) {
                if (!watch_peer_allowed(peer_filter, peer_filter_n,
                                        diff.new_messages[i].peer_id))
                    continue;
                char stext[HISTORY_TEXT_MAX];
                tty_sanitize(stext, sizeof(stext), diff.new_messages[i].text);
                if (printf("[%d] %lld %s\n",
                           diff.new_messages[i].id,
                           (long long)diff.new_messages[i].date,
                           diff.new_messages[i].complex
                               ? "(complex \xe2\x80\x94 text not parsed)"
                               : stext) < 0) {
                    if (errno == EPIPE) { g_stop = 1; break; }
                }
                printed++;
            }
            if (!g_stop && printed == 0 && !args->quiet) {
                if (printf("(no new messages; pts=%d date=%lld)\n",
                           state.pts, (long long)state.date) < 0 && errno == EPIPE) {
                    g_stop = 1;
                }
            }
        }
        if (!g_stop && fflush(stdout) != 0 && errno == EPIPE)
            g_stop = 1;

        /* Sleep in 1-second chunks so SIGINT is responsive. */
        for (int i = 0; i < interval && !g_stop; i++) sleep(1);
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
        char esc[HISTORY_TEXT_MAX * 6 + 1];
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i) printf(",");
            json_escape_str(esc, sizeof(esc), entries[i].text);
            printf("{\"id\":%d,\"out\":%s,\"date\":%lld,\"text\":\"%s\","
                   "\"complex\":%s}",
                   entries[i].id,
                   entries[i].out ? "true" : "false",
                   (long long)entries[i].date,
                   esc,
                   entries[i].complex ? "true" : "false");
        }
        printf("]\n");
    } else {
        printf("%-8s %-4s %-20s %s\n", "id", "out", "date", "text");
        for (int i = 0; i < count; i++) {
            char stext[HISTORY_TEXT_MAX];
            tty_sanitize(stext, sizeof(stext), entries[i].text);
            printf("%-8d %-4s %-20lld %s\n",
                   entries[i].id,
                   entries[i].out ? "yes" : "no",
                   (long long)entries[i].date,
                   entries[i].complex ? "(complex \xe2\x80\x94 text not parsed)"
                                      : stext);
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
        char su[64];
        tty_sanitize(su, sizeof(su), r.username);
        printf("type:         %s\n", resolved_kind_name(r.kind));
        printf("id:           %lld\n", (long long)r.id);
        if (r.username[0]) printf("username:     @%s\n", su);
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
            /* --no-media: skip pure-media entries (media present, no text). */
            if (args->no_media && entries[i].media != MEDIA_NONE
                    && entries[i].media != MEDIA_EMPTY
                    && entries[i].text[0] == '\0') {
                continue;
            }
            if (!first) printf(",");
            first = 0;
            /* When --no-media is set, suppress the media label in JSON too. */
            const char *ml = (args->no_media) ? "" : media_label[entries[i].media];
            long long mid = (args->no_media) ? 0LL : (long long)entries[i].media_id;
            /* Look up cached path for photo/document. */
            char cached_path[2048] = {0};
            int has_cache = 0;
            if (!args->no_media && entries[i].media_id != 0
                && (entries[i].media == MEDIA_PHOTO
                    || entries[i].media == MEDIA_DOCUMENT)) {
                has_cache = (media_index_get(entries[i].media_id,
                                             cached_path,
                                             sizeof(cached_path)) == 1);
            }
            printf("{\"id\":%d,\"out\":%s,\"date\":%lld,\"text\":\"%s\","
                   "\"complex\":%s,\"media\":\"%s\",\"media_id\":%lld"
                   ",\"media_path\":\"%s\"}",
                   entries[i].id,
                   entries[i].out ? "true" : "false",
                   (long long)entries[i].date, entries[i].text,
                   entries[i].complex ? "true" : "false",
                   ml, mid,
                   has_cache ? cached_path : "");
        }
        printf("]\n");
    } else {
        int printed = 0;
        for (int i = 0; i < count; i++) {
            const char *ml = media_label[entries[i].media];
            /* Look up cached local path for photo/document. */
            char cached_path[2048] = {0};
            int has_cache = 0;
            if (entries[i].media_id != 0
                && (entries[i].media == MEDIA_PHOTO
                    || entries[i].media == MEDIA_DOCUMENT)) {
                has_cache = (media_index_get(entries[i].media_id,
                                             cached_path,
                                             sizeof(cached_path)) == 1);
            }
            char stext[HISTORY_TEXT_MAX];
            tty_sanitize(stext, sizeof(stext), entries[i].text);
            if (entries[i].complex) {
                printf("[%d] %s %lld (complex \xe2\x80\x94 text not parsed)\n",
                       entries[i].id, entries[i].out ? ">" : "<",
                       (long long)entries[i].date);
                printed++;
            } else if (ml[0] && args->no_media) {
                /* --no-media: pure-media (no caption) → skip entirely;
                 * mixed (caption present) → print caption only, no label. */
                if (entries[i].text[0] == '\0') continue;
                printf("[%d] %s %lld %s\n",
                       entries[i].id, entries[i].out ? ">" : "<",
                       (long long)entries[i].date, stext);
                printed++;
            } else if (ml[0]) {
                /* Display inline cached path if available, else just label. */
                if (has_cache) {
                    printf("[%d] %s %lld [%s: %s] %s\n",
                           entries[i].id, entries[i].out ? ">" : "<",
                           (long long)entries[i].date, ml,
                           cached_path, stext);
                } else {
                    printf("[%d] %s %lld [%s] %s\n",
                           entries[i].id, entries[i].out ? ">" : "<",
                           (long long)entries[i].date, ml, stext);
                }
                printed++;
            } else {
                printf("[%d] %s %lld %s\n",
                       entries[i].id, entries[i].out ? ">" : "<",
                       (long long)entries[i].date, stext);
                printed++;
            }
        }
        if (printed == 0) printf("(no messages)\n");
    }
    free(entries);
    return 0;
}

static int cmd_download(const ArgResult *args, const AppContext *ctx) {
    if (!args->peer || args->msg_id <= 0) {
        fprintf(stderr, "tg-cli-ro download: <peer> and positive <msg_id> required\n");
        return 1;
    }
    ApiConfig cfg; MtProtoSession s; Transport t;
    int brc = session_bringup(args, &cfg, &s, &t);
    if (brc != 0) return brc;

    HistoryPeer peer = {0};
    if (resolve_peer_arg(&cfg, &s, &t, args->peer, &peer) != 0) {
        transport_close(&t);
        return 1;
    }

    HistoryEntry entry = {0};
    int count = 0;
    int rc = domain_get_history(&cfg, &s, &t, &peer, args->msg_id + 1, 1,
                                 &entry, &count);
    if (rc != 0 || count == 0 || entry.id != args->msg_id) {
        fprintf(stderr,
                "tg-cli-ro download: message %d not found in this peer\n",
                args->msg_id);
        transport_close(&t);
        return 1;
    }
    if (entry.media != MEDIA_PHOTO && entry.media != MEDIA_DOCUMENT) {
        fprintf(stderr,
                "tg-cli-ro download: message %d has no downloadable "
                "photo/document (media kind=%d)\n",
                args->msg_id, (int)entry.media);
        transport_close(&t);
        return 1;
    }
    if (entry.media_info.access_hash == 0
        || entry.media_info.file_reference_len == 0) {
        fprintf(stderr,
                "tg-cli-ro download: missing access_hash or file_reference\n");
        transport_close(&t);
        return 1;
    }

    /* Compose output path: --out if given, else
     *   <cache>/downloads/photo-<id>.jpg  (photo)
     *   <cache>/downloads/<filename or doc-<id>>  (document) */
    char path_buf[2048];
    const char *out_path = args->out_path;
    if (!out_path) {
        const char *cache = ctx->cache_dir ? ctx->cache_dir : "/tmp";
        char dir_buf[1536];
        snprintf(dir_buf, sizeof(dir_buf), "%s/downloads", cache);
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

    rc = domain_download_media_cross_dc(&cfg, &s, &t,
                                          &entry.media_info, out_path);
    transport_close(&t);
    if (rc != 0) {
        fprintf(stderr, "tg-cli-ro download: failed (see logs)\n");
        return 1;
    }

    /* Record in media index so `history` can show inline paths. */
    int64_t media_id = (entry.media == MEDIA_DOCUMENT)
                     ? entry.media_info.document_id
                     : entry.media_info.photo_id;
    if (media_index_put(media_id, out_path) != 0) {
        fprintf(stderr, "tg-cli-ro download: warning: failed to update media index\n");
    }

    if (args->json) {
        printf("{\"saved\":\"%s\",\"kind\":\"%s\",\"id\":%lld}\n",
               out_path,
               (entry.media == MEDIA_DOCUMENT) ? "document" : "photo",
               (long long)media_id);
    } else if (!args->quiet) {
        printf("saved: %s\n", out_path);
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
        fprintf(stderr, "tg-cli-ro dialogs: failed (see logs)\n");
        free(entries);
        return 1;
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
                   entries[i].top_message_id,
                   entries[i].unread_count);
        }
        printf("]\n");
    } else {
        printf("%-8s %6s %-32s %s\n",
               "type", "unread", "title", "@username / id");
        for (int i = 0; i < count; i++) {
            char stitle[128], susername[64];
            tty_sanitize(stitle, sizeof(stitle), entries[i].title);
            tty_sanitize(susername, sizeof(susername), entries[i].username);
            const char *title = entries[i].title[0] ? stitle : "(no title)";
            if (entries[i].username[0]) {
                printf("%-8s %6d %-32s @%s\n",
                       peer_kind_name(entries[i].kind),
                       entries[i].unread_count, title,
                       susername);
            } else {
                printf("%-8s %6d %-32s %lld\n",
                       peer_kind_name(entries[i].kind),
                       entries[i].unread_count, title,
                       (long long)entries[i].peer_id);
            }
        }
    }
    free(entries);
    return 0;
}

static void print_usage(void) {
    puts(
        "Usage: tg-cli-ro [GLOBAL FLAGS] <subcommand> [ARGS]\n"
        "\n"
        "Read-only batch Telegram CLI. Cannot mutate server state.\n"
        "For read+write batch use tg-cli(1). For the REPL/TUI use tg-tui(1).\n"
        "\n"
        "Read subcommands:\n"
        "  me (or self)                         Show own profile (US-05)\n"
        "  dialogs  [--limit N] [--archived]    List dialogs (US-04)\n"
        "  history  <peer> [--limit N] [--offset N] [--no-media]  Fetch history (US-06)\n"
        "           (dates are Unix epoch seconds; use 'date -d @$ts' to format)\n"
        "  search   [<peer>] <query> [--limit N]  Search messages (US-10)\n"
        "           (dates are Unix epoch seconds; use 'date -d @$ts' to format)\n"
        "  contacts                             List contacts (US-09)\n"
        "  user-info <peer>                     User/channel info (US-09)\n"
        "  watch    [--peers X,Y] [--interval N]  Watch updates (US-07)\n"
        "           With --json: emits one NDJSON line per new message.\n"
        "           (dates are Unix epoch seconds; use 'date -d @$ts' to format)\n"
        "  download <peer> <msg_id> [--out PATH]  Download photo or document (US-08)\n"
        "\n"
        "Session:\n"
        "  login [--api-id N --api-hash HEX] [--force]  First-run config wizard\n"
        "  --logout                             Clear persisted session\n"
        "\n"
        "Global flags:\n"
        "  --config <path>     Custom config file path\n"
        "  --json              Machine-readable JSON output (where supported)\n"
        "  --quiet             Suppress informational output\n"
        "  --help, -h          Show this help and exit\n"
        "  --version, -v       Show version and exit\n"
        "\n"
        "Login flags (for session authentication):\n"
        "  --phone <number>    E.g. +15551234567\n"
        "  --code <digits>     The SMS/app code received on the phone\n"
        "  --password <pass>   2FA password (when the account has one set)\n"
        "\n"
        "Credentials:\n"
        "  TG_CLI_API_ID / TG_CLI_API_HASH env vars, or\n"
        "  api_id= / api_hash= in ~/.config/tg-cli/config.ini\n"
        "  (run 'tg-cli-ro login' to set up config.ini interactively)\n"
        "\n"
        "Examples:\n"
        "  tg-cli-ro login --api-id 12345 --api-hash deadbeef...  # batch setup\n"
        "  tg-cli-ro me\n"
        "  tg-cli-ro dialogs --limit 50\n"
        "  tg-cli-ro history @friend --limit 20\n"
        "\n"
        "See man tg-cli-ro(1) for the full reference.\n"
    );
}

int main(int argc, char **argv) {
    platform_normalize_argv(&argc, &argv);
    /* SEC-01: detect once whether stdout is a real terminal so tty_sanitize()
     * can skip sanitization when output is piped (preserves binary safety). */
    g_stdout_is_tty = isatty(STDOUT_FILENO);
    AppContext ctx;
    if (app_bootstrap(&ctx, "tg-cli-ro") != 0) {
        fprintf(stderr, "tg-cli-ro: bootstrap failed\n");
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
                            "tg-cli-ro: logout: cannot connect to DC%d, clearing local session",
                            loaded_dc);
                        session_store_clear();
                    }
                } else {
                    /* No persisted session — nothing to invalidate. */
                    session_store_clear();
                }
            } else {
                /* No credentials — just wipe locally. */
                session_store_clear();
            }
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
        case CMD_DOWNLOAD:
            exit_code = cmd_download(&args, &ctx); break;
        case CMD_LOGIN:
            if (args.api_id_str || args.api_hash_str) {
                exit_code = config_wizard_run_batch(args.api_id_str,
                                                     args.api_hash_str,
                                                     args.force) != 0 ? 1 : 0;
            } else {
                exit_code = config_wizard_run_interactive() != 0 ? 1 : 0;
            }
            app_shutdown(&ctx);
            return exit_code;
        case CMD_SEND:
        case CMD_READ:
        case CMD_EDIT:
        case CMD_DELETE:
        case CMD_FORWARD:
        case CMD_SEND_FILE:
            fprintf(stderr, "tg-cli-ro: write commands are not available in read-only mode\n");
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
