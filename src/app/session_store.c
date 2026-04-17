/**
 * @file app/session_store.c
 * @brief Multi-DC session persistence (v2).
 */

#include "app/session_store.h"

#include "fs_util.h"
#include "logger.h"
#include "platform/path.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_MAGIC      "TGCS"
#define STORE_VERSION    2
#define STORE_HEADER     16                  /* magic+ver+home_dc+count */
#define STORE_ENTRY_SIZE 276                 /* 4 + 8 + 8 + 256 */
#define STORE_MAX_SIZE   (STORE_HEADER + SESSION_STORE_MAX_DCS * STORE_ENTRY_SIZE)

typedef struct {
    int32_t  dc_id;
    uint64_t server_salt;
    uint64_t session_id;
    uint8_t  auth_key[MTPROTO_AUTH_KEY_SIZE];
} StoreEntry;

typedef struct {
    int32_t    home_dc_id;
    uint32_t   count;
    StoreEntry entries[SESSION_STORE_MAX_DCS];
} StoreFile;

static char *store_path(void) {
    const char *cfg = platform_config_dir();
    if (!cfg) return NULL;
    char *p = NULL;
    if (asprintf(&p, "%s/tg-cli/session.bin", cfg) == -1) return NULL;
    return p;
}

static int ensure_dir(void) {
    const char *cfg_dir = platform_config_dir();
    if (!cfg_dir) return -1;
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/tg-cli", cfg_dir);
    if (fs_mkdir_p(dir_path, 0700) != 0) {
        logger_log(LOG_ERROR, "session_store: cannot create %s", dir_path);
        return -1;
    }
    return 0;
}

/* Read the file into `out` if present. Returns:
 *   0  on success (file existed and parsed cleanly)
 *  +1  on "file absent" (caller will treat as empty store)
 *  -1  on corrupt / unsupported
 */
static int read_file(StoreFile *out) {
    memset(out, 0, sizeof(*out));

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    RAII_FILE FILE *f = fopen(path, "rb");
    if (!f) return +1;

    uint8_t buf[STORE_MAX_SIZE];
    size_t n = fread(buf, 1, sizeof(buf), f);
    if (n < STORE_HEADER) {
        logger_log(LOG_WARN, "session_store: truncated header");
        return -1;
    }
    if (memcmp(buf, STORE_MAGIC, 4) != 0) {
        logger_log(LOG_WARN, "session_store: bad magic");
        return -1;
    }
    int32_t version;
    memcpy(&version, buf + 4, 4);
    if (version != STORE_VERSION) {
        logger_log(LOG_WARN, "session_store: unsupported version %d", version);
        return -1;
    }
    memcpy(&out->home_dc_id, buf + 8,  4);
    memcpy(&out->count,      buf + 12, 4);
    if (out->count > SESSION_STORE_MAX_DCS) {
        logger_log(LOG_WARN, "session_store: count %u too large", out->count);
        return -1;
    }
    size_t need = STORE_HEADER + (size_t)out->count * STORE_ENTRY_SIZE;
    if (n < need) {
        logger_log(LOG_WARN, "session_store: truncated body");
        return -1;
    }
    for (uint32_t i = 0; i < out->count; i++) {
        size_t off = STORE_HEADER + (size_t)i * STORE_ENTRY_SIZE;
        memcpy(&out->entries[i].dc_id,       buf + off + 0,   4);
        memcpy(&out->entries[i].server_salt, buf + off + 4,   8);
        memcpy(&out->entries[i].session_id,  buf + off + 12,  8);
        memcpy( out->entries[i].auth_key,    buf + off + 20,  256);
    }
    return 0;
}

static int write_file(const StoreFile *st) {
    if (ensure_dir() != 0) return -1;

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    RAII_FILE FILE *f = fopen(path, "wb");
    if (!f) {
        logger_log(LOG_ERROR, "session_store: cannot open %s for writing", path);
        return -1;
    }

    uint8_t buf[STORE_MAX_SIZE] = {0};
    memcpy(buf, STORE_MAGIC, 4);
    int32_t version = STORE_VERSION;
    memcpy(buf + 4,  &version,        4);
    memcpy(buf + 8,  &st->home_dc_id, 4);
    memcpy(buf + 12, &st->count,      4);
    for (uint32_t i = 0; i < st->count; i++) {
        size_t off = STORE_HEADER + (size_t)i * STORE_ENTRY_SIZE;
        memcpy(buf + off + 0,   &st->entries[i].dc_id,       4);
        memcpy(buf + off + 4,   &st->entries[i].server_salt, 8);
        memcpy(buf + off + 12,  &st->entries[i].session_id,  8);
        memcpy(buf + off + 20,   st->entries[i].auth_key,    256);
    }
    size_t total = STORE_HEADER + (size_t)st->count * STORE_ENTRY_SIZE;
    size_t n = fwrite(buf, 1, total, f);
    if (n != total) {
        logger_log(LOG_ERROR, "session_store: short write to %s", path);
        return -1;
    }
    fflush(f);
    if (fs_ensure_permissions(path, 0600) != 0) {
        logger_log(LOG_WARN, "session_store: cannot set 0600 on %s", path);
    }
    return 0;
}

/* Find the index of @p dc_id in the store, or -1 if absent. */
static int find_entry(const StoreFile *st, int dc_id) {
    for (uint32_t i = 0; i < st->count; i++) {
        if (st->entries[i].dc_id == dc_id) return (int)i;
    }
    return -1;
}

static void populate_entry(StoreEntry *e, int dc_id, const MtProtoSession *s) {
    e->dc_id       = dc_id;
    e->server_salt = s->server_salt;
    e->session_id  = s->session_id;
    memcpy(e->auth_key, s->auth_key, MTPROTO_AUTH_KEY_SIZE);
}

static int upsert(int dc_id, const MtProtoSession *s, int set_home) {
    if (!s || !s->has_auth_key) return -1;

    StoreFile st;
    int rc = read_file(&st);
    if (rc < 0) {
        /* Corrupt: start fresh. The user is re-authenticating anyway. */
        memset(&st, 0, sizeof(st));
    }

    int idx = find_entry(&st, dc_id);
    if (idx < 0) {
        if (st.count >= SESSION_STORE_MAX_DCS) {
            logger_log(LOG_ERROR,
                       "session_store: no slot left for DC%d", dc_id);
            return -1;
        }
        idx = (int)st.count++;
    }
    populate_entry(&st.entries[idx], dc_id, s);

    if (set_home || st.home_dc_id == 0) {
        st.home_dc_id = dc_id;
    }

    if (write_file(&st) != 0) return -1;
    logger_log(LOG_INFO,
               "session_store: persisted DC%d (home=%d, count=%u)",
               dc_id, st.home_dc_id, st.count);
    return 0;
}

static void apply_entry(MtProtoSession *s, const StoreEntry *e) {
    s->server_salt  = e->server_salt;
    s->session_id   = e->session_id;
    memcpy(s->auth_key, e->auth_key, MTPROTO_AUTH_KEY_SIZE);
    s->has_auth_key = 1;
    s->seq_no       = 0;
    s->last_msg_id  = 0;
}

int session_store_save(const MtProtoSession *s, int dc_id) {
    return upsert(dc_id, s, /*set_home=*/1);
}

int session_store_save_dc(int dc_id, const MtProtoSession *s) {
    return upsert(dc_id, s, /*set_home=*/0);
}

int session_store_load(MtProtoSession *s, int *dc_id) {
    if (!s || !dc_id) return -1;

    StoreFile st;
    if (read_file(&st) != 0) return -1;
    if (st.count == 0 || st.home_dc_id == 0) return -1;

    int idx = find_entry(&st, st.home_dc_id);
    if (idx < 0) {
        logger_log(LOG_WARN,
                   "session_store: home DC%d has no entry", st.home_dc_id);
        return -1;
    }
    apply_entry(s, &st.entries[idx]);
    *dc_id = st.home_dc_id;
    logger_log(LOG_INFO, "session_store: loaded home DC%d", *dc_id);
    return 0;
}

int session_store_load_dc(int dc_id, MtProtoSession *s) {
    if (!s) return -1;

    StoreFile st;
    if (read_file(&st) != 0) return -1;

    int idx = find_entry(&st, dc_id);
    if (idx < 0) return -1;

    apply_entry(s, &st.entries[idx]);
    logger_log(LOG_INFO, "session_store: loaded DC%d", dc_id);
    return 0;
}

void session_store_clear(void) {
    RAII_STRING char *path = store_path();
    if (!path) return;
    if (remove(path) == 0)
        logger_log(LOG_INFO, "session_store: cleared");
}
