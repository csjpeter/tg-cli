/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/session_store.c
 * @brief Multi-DC session persistence (v2).
 *
 * Write safety:
 *   - An exclusive advisory lock (flock LOCK_EX | LOCK_NB) is acquired on
 *     the session file before every read-modify-write cycle.  A non-blocking
 *     attempt is used; if the lock is busy we return -1 with a log message
 *     so the caller can surface "another tg-cli process is using this session".
 *   - The new content is written to `session.bin.tmp`, fsync'd, then renamed
 *     atomically over `session.bin`.  This prevents a truncated file on crash
 *     or disk-full.
 *   - Reads also take a shared lock (LOCK_SH | LOCK_NB) so they never observe
 *     a partially-written file.
 *   - On Windows the lock calls are compiled out (advisory locks are not
 *     available via flock on MinGW); the atomic-rename pattern still applies.
 */

#include "app/session_store.h"

#include "fs_util.h"
#include "logger.h"
#include "platform/path.h"
#include "raii.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(_WIN32)
#  include <sys/file.h>   /* flock(2) */
#endif

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

/* -------------------------------------------------------------------------
 * Path helpers
 * ---------------------------------------------------------------------- */

static char *store_path(void) {
    const char *cfg = platform_config_dir();
    if (!cfg) return NULL;
    char *p = NULL;
    if (asprintf(&p, "%s/tg-cli/session.bin", cfg) == -1) return NULL;
    return p;
}

static char *store_tmp_path(void) {
    const char *cfg = platform_config_dir();
    if (!cfg) return NULL;
    char *p = NULL;
    if (asprintf(&p, "%s/tg-cli/session.bin.tmp", cfg) == -1) return NULL;
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

/* -------------------------------------------------------------------------
 * Advisory locking (POSIX only)
 *
 * Returns an open fd that holds the lock, or -1 on error / busy.
 * The caller must close() the fd to release the lock.
 * On Windows these stubs always succeed (no-op).
 * ---------------------------------------------------------------------- */

#if !defined(_WIN32)

/**
 * @brief Open @p path and acquire an advisory flock.
 *
 * @param path   Path to lock (created if absent).
 * @param how    LOCK_EX for exclusive, LOCK_SH for shared.
 * @return open fd with lock held, or -1 on failure.
 */
static int lock_file(const char *path, int how) {
    /* O_CREAT so the lock file can exist even before first write. */
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        logger_log(LOG_ERROR, "session_store: open(%s) failed: %s",
                   path, strerror(errno));
        return -1;
    }
    if (flock(fd, how | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            logger_log(LOG_ERROR,
                       "session_store: another tg-cli process is using "
                       "this session; please close it first");
        } else {
            logger_log(LOG_ERROR, "session_store: flock failed: %s",
                       strerror(errno));
        }
        close(fd);
        return -1;
    }
    return fd;
}

static void unlock_file(int fd) {
    if (fd >= 0) close(fd);
}

#else /* _WIN32 — no advisory locks; just return a dummy fd */

static int lock_file(const char *path, int how) {
    (void)path; (void)how;
    return 0;   /* non-negative = success */
}

static void unlock_file(int fd) {
    (void)fd;
}

#endif /* _WIN32 */

/* -------------------------------------------------------------------------
 * Serialise / deserialise
 * ---------------------------------------------------------------------- */

/* Read the file into `out` if present.  The caller is responsible for holding
 * a shared lock before calling this function.
 *
 * Returns:
 *   0  on success (file existed and parsed cleanly)
 *  +1  on "file absent" (caller treats as empty store)
 *  -1  on corrupt / unsupported
 */
static int read_file_locked(StoreFile *out) {
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

/**
 * @brief Atomically write @p st to the session file.
 *
 * Writes to a sibling .tmp file, fsync's it, then renames it over the real
 * path.  The rename is atomic on POSIX.  The caller must hold an exclusive
 * lock before calling this function.
 */
static int write_file_atomic(const StoreFile *st) {
    RAII_STRING char *path     = store_path();
    RAII_STRING char *tmp_path = store_tmp_path();
    if (!path || !tmp_path) return -1;

    /* Build the serialised buffer. */
    uint8_t buf[STORE_MAX_SIZE];
    memset(buf, 0, sizeof(buf));
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

    /* Write to tmp. */
    int tfd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (tfd == -1) {
        logger_log(LOG_ERROR, "session_store: cannot open tmp %s: %s",
                   tmp_path, strerror(errno));
        return -1;
    }

    ssize_t n = write(tfd, buf, total);
    if (n < 0 || (size_t)n != total) {
        logger_log(LOG_ERROR, "session_store: short write to %s", tmp_path);
        close(tfd);
        unlink(tmp_path);
        return -1;
    }

#if !defined(_WIN32)
    if (fsync(tfd) != 0) {
        logger_log(LOG_WARN, "session_store: fsync(%s) failed: %s",
                   tmp_path, strerror(errno));
        /* Non-fatal — proceed with rename. */
    }
#endif
    close(tfd);

    /* Set permissions on the tmp file before rename. */
    if (fs_ensure_permissions(tmp_path, 0600) != 0) {
        logger_log(LOG_WARN, "session_store: cannot set 0600 on %s", tmp_path);
    }

    /* Atomic rename. */
    if (rename(tmp_path, path) != 0) {
        logger_log(LOG_ERROR, "session_store: rename(%s, %s) failed: %s",
                   tmp_path, path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Internal entry helpers
 * ---------------------------------------------------------------------- */

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

static void apply_entry(MtProtoSession *s, const StoreEntry *e) {
    s->server_salt  = e->server_salt;
    s->session_id   = e->session_id;
    memcpy(s->auth_key, e->auth_key, MTPROTO_AUTH_KEY_SIZE);
    s->has_auth_key = 1;
    s->seq_no       = 0;
    s->last_msg_id  = 0;
}

/* -------------------------------------------------------------------------
 * Upsert (read-modify-write under exclusive lock)
 * ---------------------------------------------------------------------- */

static int upsert(int dc_id, const MtProtoSession *s, int set_home) {
    if (!s || !s->has_auth_key) return -1;

    if (ensure_dir() != 0) return -1;

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    /* Acquire exclusive lock. */
    int lock_fd = lock_file(path, LOCK_EX);
    if (lock_fd == -1) return -1;

    StoreFile st;
    int rc = read_file_locked(&st);
    if (rc < 0) {
        /* Corrupt: start fresh. The user is re-authenticating anyway. */
        memset(&st, 0, sizeof(st));
    }

    int idx = find_entry(&st, dc_id);
    if (idx < 0) {
        if (st.count >= SESSION_STORE_MAX_DCS) {
            logger_log(LOG_ERROR,
                       "session_store: no slot left for DC%d", dc_id);
            unlock_file(lock_fd);
            return -1;
        }
        idx = (int)st.count++;
    }
    populate_entry(&st.entries[idx], dc_id, s);

    if (set_home || st.home_dc_id == 0) {
        st.home_dc_id = dc_id;
    }

    int write_rc = write_file_atomic(&st);

    unlock_file(lock_fd);

    if (write_rc != 0) return -1;

    logger_log(LOG_INFO,
               "session_store: persisted DC%d (home=%d, count=%u)",
               dc_id, st.home_dc_id, st.count);
    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int session_store_save(const MtProtoSession *s, int dc_id) {
    return upsert(dc_id, s, /*set_home=*/1);
}

int session_store_save_dc(int dc_id, const MtProtoSession *s) {
    return upsert(dc_id, s, /*set_home=*/0);
}

int session_store_load(MtProtoSession *s, int *dc_id) {
    if (!s || !dc_id) return -1;

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    /* Shared lock — wait for any in-progress write to finish. */
    int lock_fd = lock_file(path, LOCK_SH);
    if (lock_fd == -1) return -1;

    StoreFile st;
    int rc = read_file_locked(&st);

    unlock_file(lock_fd);

    if (rc != 0) return -1;
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

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    int lock_fd = lock_file(path, LOCK_SH);
    if (lock_fd == -1) return -1;

    StoreFile st;
    int rc = read_file_locked(&st);

    unlock_file(lock_fd);

    if (rc != 0) return -1;

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
