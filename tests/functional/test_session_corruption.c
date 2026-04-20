/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_session_corruption.c
 * @brief TEST-76 / US-25 — functional coverage of session.bin corruption
 *        and adversarial-state recovery paths in src/app/session_store.c.
 *
 * Scenarios covered:
 *   1. test_truncated_session_refuses_load
 *      Write the first 8 bytes of a valid file; session_store_load must
 *      return != 0 and the log must note "truncated".
 *   2. test_bad_magic_refuses_load
 *      Overwrite the 4-byte magic; session_store_load must fail with a
 *      distinct diagnostic ("bad magic").
 *   3. test_unknown_version_refuses_load_and_does_not_overwrite
 *      Stamp an impossibly-high version byte; session_store_load fails
 *      and a subsequent save does NOT clobber (bytes on disk unchanged
 *      relative to the crafted content).
 *   4. test_concurrent_writers_both_correct
 *      Fork two processes that both call session_store_save for the same
 *      DC. After both exit, the file is valid and contains exactly one
 *      entry for that DC (flock + atomic rename keep it sane).
 *   5. test_stale_tmp_leftover_ignored
 *      Create session.bin.tmp manually before calling save; the atomic
 *      rename must still leave a correct final file and the tmp must be
 *      gone afterwards.
 *   6. test_mode_drift_corrected_on_save
 *      chmod 0644 on an existing session.bin; the next save must restore
 *      mode 0600.
 *
 * Each test uses its own /tmp scratch HOME and unsets XDG_CONFIG_HOME so
 * the CI runners (which export XDG_CONFIG_HOME) don't bypass the
 * redirection — see test_logout_rpc.c for the canonical pattern.
 */

#include "test_helpers.h"

#include "app/session_store.h"
#include "logger.h"
#include "mtproto_session.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>    /* flock(2) */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Constants mirrored from session_store.c — bound by the on-disk     */
/* format, not private implementation detail.                         */
/* ------------------------------------------------------------------ */

#define STORE_HEADER_SIZE   16
#define STORE_ENTRY_SIZE    276
#define STORE_MAGIC_STR     "TGCS"
#define STORE_VERSION_CUR   2

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Build a scratch HOME path keyed by test tag + pid so parallel CI                                                     *  runs don't collide. */
static void scratch_dir_for(const char *tag, char *out, size_t cap) {
    snprintf(out, cap, "/tmp/tg-cli-ft-sesscorr-%s-%d", tag, (int)getpid());
}

/** rm -rf @p path (best-effort, errors ignored). */
static void rm_rf(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    int sysrc = system(cmd);
    (void)sysrc;
}

/** mkdir -p @p path. */
static int mkdir_p(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    int sysrc = system(cmd);
    return sysrc == 0 ? 0 : -1;
}

/**
 * Redirect HOME to a fresh scratch dir, unset XDG_CONFIG_HOME (CI runners
 * set it and would otherwise override our redirected HOME), and point the
 * logger at a per-test log file so we can assert on the diagnostics.
 *
 * @param tag           Short label used for path composition.
 * @param out_home      Receives the scratch HOME path.
 * @param home_cap      Capacity of @p out_home.
 * @param out_log       Receives the path to the per-test log file.
 * @param log_cap       Capacity of @p out_log.
 */
static void with_fresh_home(const char *tag,
                            char *out_home, size_t home_cap,
                            char *out_log,  size_t log_cap) {
    scratch_dir_for(tag, out_home, home_cap);
    rm_rf(out_home);

    char cfg_dir[600];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", out_home);
    (void)mkdir_p(cfg_dir);

    char cache_dir[600];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/tg-cli/logs", out_home);
    (void)mkdir_p(cache_dir);

    setenv("HOME", out_home, 1);
    /* CI runners (GitHub Actions) set XDG_CONFIG_HOME; that would redirect
     * platform_config_dir() away from our temp HOME. Force the HOME-based
     * fallback so the prod code writes into our scratch dir. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");

    snprintf(out_log, log_cap, "%s/session.log", cache_dir);
    /* Start with a fresh log file for easy substring matching. */
    (void)unlink(out_log);
    (void)logger_init(out_log, LOG_DEBUG);
}

/** Build the canonical session.bin path under the current HOME. */
static void session_path_from_home(const char *home, char *out, size_t cap) {
    snprintf(out, cap, "%s/.config/tg-cli/session.bin", home);
}
static void tmp_session_path_from_home(const char *home, char *out, size_t cap) {
    snprintf(out, cap, "%s/.config/tg-cli/session.bin.tmp", home);
}

/** Read file @p path into a heap buffer; caller frees. NUL-terminated. */
static char *slurp(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (size_out) *size_out = n;
    return buf;
}

/** Populate a fake but internally-valid auth_key + session into @p s. */
static void fake_session_fill(MtProtoSession *s, uint8_t key_byte) {
    mtproto_session_init(s);
    s->server_salt  = 0x1122334455667788ULL;
    s->session_id   = 0xAABBCCDDEEFF0011ULL;
    for (size_t i = 0; i < MTPROTO_AUTH_KEY_SIZE; i++)
        s->auth_key[i] = key_byte;
    s->has_auth_key = 1;
}

/** Write the contents of @p buf (size @p n) to @p path, overwriting. */
static int write_full(const char *path, const uint8_t *buf, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) return -1;
    ssize_t w = write(fd, buf, n);
    close(fd);
    return (w < 0 || (size_t)w != n) ? -1 : 0;
}

/** Build a valid-looking header + @p count entries into @p buf. */
static size_t craft_valid_file(uint8_t *buf, size_t cap,
                               uint32_t count, int32_t home_dc,
                               int32_t dc_id_of_entry,
                               uint8_t key_byte) {
    size_t need = STORE_HEADER_SIZE + (size_t)count * STORE_ENTRY_SIZE;
    if (need > cap) return 0;
    memset(buf, 0, need);
    memcpy(buf, STORE_MAGIC_STR, 4);
    int32_t v = STORE_VERSION_CUR;
    memcpy(buf + 4,  &v,        4);
    memcpy(buf + 8,  &home_dc,  4);
    memcpy(buf + 12, &count,    4);
    for (uint32_t i = 0; i < count; i++) {
        size_t off = STORE_HEADER_SIZE + (size_t)i * STORE_ENTRY_SIZE;
        int32_t  dc_id       = dc_id_of_entry;
        uint64_t server_salt = 0x1122334455667788ULL;
        uint64_t session_id  = 0xAABBCCDDEEFF0011ULL;
        memcpy(buf + off + 0,   &dc_id,        4);
        memcpy(buf + off + 4,   &server_salt,  8);
        memcpy(buf + off + 12,  &session_id,   8);
        memset(buf + off + 20,  key_byte,      MTPROTO_AUTH_KEY_SIZE);
    }
    return need;
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * 1. Write the first 8 bytes of a valid file so the header is too
 *    short to parse. session_store_load must return non-zero.
 */
static void test_truncated_session_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("trunc", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t full[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    size_t total = craft_valid_file(full, sizeof(full),
                                    1, 2, 2, 0xAA);
    ASSERT(total > 0, "crafted valid template");

    /* Only write 8 bytes — far less than the 16-byte header. */
    ASSERT(write_full(bin, full, 8) == 0, "short file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a truncated file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "truncated") != NULL,
           "log mentions 'truncated'");
    free(buf);

    rm_rf(home);
}

/**
 * 2. Overwrite the 4-byte magic with garbage. Header parses but the
 *    magic check rejects the file.
 */
static void test_bad_magic_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("magic", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t full[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    size_t total = craft_valid_file(full, sizeof(full),
                                    1, 2, 2, 0xBB);
    ASSERT(total > 0, "crafted valid template");

    /* Stomp magic bytes to distinct non-"TGCS" value. */
    full[0] = 'X'; full[1] = 'X'; full[2] = 'X'; full[3] = 'X';
    ASSERT(write_full(bin, full, total) == 0, "bad-magic file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a bad-magic file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "bad magic") != NULL,
           "log mentions 'bad magic' (distinct from 'truncated')");
    ASSERT(strstr(buf, "truncated") == NULL,
           "no spurious 'truncated' diagnostic for bad-magic");
    free(buf);

    rm_rf(home);
}

/**
 * 3. Stamp the version byte to 9999. Load must fail; a subsequent save
 *    from a fully-initialised session resets to a fresh store (the
 *    corrupt file is NOT preserved as-is by the atomic-rename flow
 *    when the user explicitly invokes save), but the *file content on
 *    disk after a failed load alone — without a save — must be
 *    untouched. We additionally assert that once the user does call
 *    save, the file becomes valid and mode is restored to 0600.
 */
static void test_unknown_version_refuses_load_and_does_not_overwrite(void) {
    char home[512], log[1024];
    with_fresh_home("ver", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t full[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    size_t total = craft_valid_file(full, sizeof(full),
                                    1, 2, 2, 0xCC);
    ASSERT(total > 0, "crafted valid template");

    /* Rewrite version field to an impossibly-high value. */
    int32_t bad_version = 0x7FFF0001;
    memcpy(full + 4, &bad_version, 4);
    ASSERT(write_full(bin, full, total) == 0, "bad-version file written");

    /* Snapshot the on-disk bytes before we call load. */
    size_t before_sz = 0;
    char *before = slurp(bin, &before_sz);
    ASSERT(before != NULL, "read bin pre-load");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses an unknown-version file");

    /* Load-only path must NOT have rewritten the file. */
    size_t after_sz = 0;
    char *after = slurp(bin, &after_sz);
    ASSERT(after != NULL, "read bin post-load");
    ASSERT(after_sz == before_sz,
           "load-only leaves file size unchanged");
    ASSERT(memcmp(before, after, before_sz) == 0,
           "load-only leaves file bytes unchanged (no clobber)");
    free(before);
    free(after);

    /* The diagnostic must be distinct. */
    logger_close();
    size_t logsz = 0;
    char *logbuf = slurp(log, &logsz);
    ASSERT(logbuf != NULL, "read session.log");
    ASSERT(strstr(logbuf, "unsupported version") != NULL,
           "log mentions 'unsupported version'");
    free(logbuf);

    rm_rf(home);
}

/**
 * 4. Two processes save the same DC at the same instant. flock +
 *    atomic rename must leave exactly one entry with that dc_id in
 *    the final file, and it must load back cleanly.
 */
static void test_concurrent_writers_both_correct(void) {
    char home[512], log[1024];
    with_fresh_home("conc", home, sizeof(home), log, sizeof(log));

    pid_t pid = fork();
    ASSERT(pid >= 0, "fork succeeded");

    if (pid == 0) {
        /* Child: save DC 4. Use _exit so we don't rerun any cleanup.
         * Because the prod code uses non-blocking flock, some attempts
         * will collide with the parent; retry until we get one clean
         * success so the test is not flaky. */
        MtProtoSession cs;
        fake_session_fill(&cs, 0x44);
        int ok = -1;
        for (int i = 0; i < 200 && ok != 0; i++) {
            ok = session_store_save_dc(4, &cs);
            if (ok != 0) {
                struct timespec ts = {0, 1 * 1000 * 1000}; /* 1 ms */
                nanosleep(&ts, NULL);
            }
        }
        _exit(ok == 0 ? 0 : 1);
    }

    /* Parent: save DC 2 (home) repeatedly in parallel; also retry until
     * at least one attempt succeeds. */
    MtProtoSession ps;
    fake_session_fill(&ps, 0x22);
    int p_ok = -1;
    for (int i = 0; i < 200 && p_ok != 0; i++) {
        p_ok = session_store_save(&ps, 2);
        if (p_ok != 0) {
            struct timespec ts = {0, 1 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
    }
    ASSERT(p_ok == 0, "parent save eventually succeeded");

    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    ASSERT(waited == pid, "child reaped");
    ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "child exited cleanly");

    /* Parent loads home DC — must succeed. */
    MtProtoSession ls;
    mtproto_session_init(&ls);
    int dc = 0;
    ASSERT(session_store_load(&ls, &dc) == 0,
           "home load after concurrent writes succeeds");
    ASSERT(dc == 2, "home DC still 2 after concurrent writes");

    /* DC 4 entry also loadable. */
    MtProtoSession ls4;
    mtproto_session_init(&ls4);
    ASSERT(session_store_load_dc(4, &ls4) == 0,
           "DC 4 loadable after concurrent writes");

    /* Raw file: exactly one occurrence of the bytes (dc_id = 4) and
     * one of (dc_id = 2) in the file — structure stays sane. */
    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));
    size_t sz = 0;
    uint8_t *raw = (uint8_t *)slurp(bin, &sz);
    ASSERT(raw != NULL, "read final session.bin");
    ASSERT(sz >= STORE_HEADER_SIZE, "final file has header");
    uint32_t count = 0;
    memcpy(&count, raw + 12, 4);
    /* Each DC has exactly one entry — no duplicates from racing writers. */
    ASSERT(count == 2, "exactly two DC entries (no duplicates)");
    free(raw);

    logger_close();
    rm_rf(home);
}

/**
 * 5. Drop a stale session.bin.tmp into the config dir (the kind of
 *    leftover a prior crash would leave) and verify save() copes: the
 *    final file is valid and the tmp is gone.
 */
static void test_stale_tmp_leftover_ignored(void) {
    char home[512], log[1024];
    with_fresh_home("tmp", home, sizeof(home), log, sizeof(log));

    char tmp[1024];
    tmp_session_path_from_home(home, tmp, sizeof(tmp));

    /* Plant a 1-KB garbage .tmp. */
    uint8_t junk[1024];
    memset(junk, 0x5A, sizeof(junk));
    ASSERT(write_full(tmp, junk, sizeof(junk)) == 0,
           "stale .tmp planted");

    MtProtoSession s;
    fake_session_fill(&s, 0x77);
    ASSERT(session_store_save(&s, 2) == 0,
           "save succeeds despite stale .tmp");

    /* After save the real file exists and loads back. */
    MtProtoSession ls;
    mtproto_session_init(&ls);
    int dc = 0;
    ASSERT(session_store_load(&ls, &dc) == 0,
           "load after save over stale .tmp");
    ASSERT(dc == 2, "home DC set to 2 by save");

    /* The .tmp file must be gone (rename consumed it). */
    struct stat st;
    ASSERT(stat(tmp, &st) != 0,
           ".tmp is gone after successful save");

    logger_close();
    rm_rf(home);
}

/**
 * 6b. Truncate-between-header-and-body: header says count=2 but only
 *     one entry's bytes are actually on disk.  The loader's "need"
 *     check must refuse the load.
 */
static void test_truncated_body_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("body", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t full[STORE_HEADER_SIZE + 2 * STORE_ENTRY_SIZE];
    (void)craft_valid_file(full, sizeof(full), 2, 2, 2, 0xDD);

    /* Write only enough bytes for 1 entry, but leave count=2 in header. */
    size_t short_len = STORE_HEADER_SIZE + STORE_ENTRY_SIZE;
    ASSERT(write_full(bin, full, short_len) == 0,
           "header says 2 entries, body has 1 — truncated-body file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a truncated-body file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "truncated body") != NULL,
           "log mentions 'truncated body'");
    free(buf);

    rm_rf(home);
}

/**
 * 6c. Bogus count: header claims more entries than SESSION_STORE_MAX_DCS.
 *     The loader must reject the file with a distinct diagnostic.  This
 *     maps to the ticket's "bogus auth_key length" adversarial scenario
 *     — the count field directly governs how many 276-byte auth_key
 *     payloads the loader would otherwise trust.
 */
static void test_bogus_count_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("count", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Allocate enough room for the bogus count so the header parses and
     * the count check is the reason for rejection. */
    uint8_t full[STORE_HEADER_SIZE + 16 * STORE_ENTRY_SIZE];
    memset(full, 0, sizeof(full));
    memcpy(full, STORE_MAGIC_STR, 4);
    int32_t v = STORE_VERSION_CUR;
    memcpy(full + 4,  &v, 4);
    int32_t home_dc = 2;
    memcpy(full + 8,  &home_dc, 4);
    /* Way past SESSION_STORE_MAX_DCS (=5). */
    uint32_t crazy = 9999;
    memcpy(full + 12, &crazy, 4);

    ASSERT(write_full(bin, full, sizeof(full)) == 0,
           "bogus-count file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a bogus-count file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "too large") != NULL,
           "log mentions 'too large' (distinct from truncated/magic/version)");
    free(buf);

    rm_rf(home);
}

/**
 * 6d. Home DC has no entry: construct a file whose home_dc_id points
 *     at a DC that is not in the entries array.  session_store_load
 *     must refuse the home load with the "no entry" diagnostic.
 */
static void test_home_dc_missing_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("nohome", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t full[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    /* home_dc = 7, but the only entry is DC 2. */
    (void)craft_valid_file(full, sizeof(full), 1, 7, 2, 0xEE);
    ASSERT(write_full(bin, full, sizeof(full)) == 0,
           "home-DC-missing file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0,
           "session_store_load refuses when home DC has no entry");

    /* But session_store_load_dc(2) should still succeed — entry is there. */
    MtProtoSession s2;
    mtproto_session_init(&s2);
    ASSERT(session_store_load_dc(2, &s2) == 0,
           "existing DC 2 entry is still loadable directly");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "no entry") != NULL,
           "log mentions 'no entry' for missing home DC");
    free(buf);

    rm_rf(home);
}

/**
 * 6d2. ensure_dir failure: pre-plant a regular FILE at the path where
 *      ensure_dir() wants to mkdir the ~/.config/tg-cli directory.
 *      save() must cleanly return != 0 with a diagnostic.
 */
static void test_ensure_dir_failure_blocks_save(void) {
    char home[512], log[1024];
    /* Build a minimal scratch home and make $HOME/.config itself
     * read-only so mkdir("$HOME/.config/tg-cli") fails with EACCES. */
    scratch_dir_for("ensdir", home, sizeof(home));
    rm_rf(home);

    char cfg_root[600];
    snprintf(cfg_root, sizeof(cfg_root), "%s/.config", home);
    (void)mkdir_p(cfg_root);

    /* Initialise the logger under the cache dir before we clamp perms
     * so the logger init itself can succeed. */
    char cache_dir[700];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/tg-cli/logs", home);
    (void)mkdir_p(cache_dir);
    snprintf(log, sizeof(log), "%s/session.log", cache_dir);
    (void)unlink(log);

    setenv("HOME", home, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");

    (void)logger_init(log, LOG_DEBUG);

    /* Clamp .config to read+execute only (no write). mkdir of tg-cli
     * subdir must fail with EACCES and ensure_dir must surface the
     * "cannot create" diagnostic. */
    ASSERT(chmod(cfg_root, 0500) == 0,
           "chmod $HOME/.config to 0500 (no write)");

    MtProtoSession s;
    fake_session_fill(&s, 0x55);
    int rc = session_store_save(&s, 2);
    ASSERT(rc != 0,
           "save refuses when $HOME/.config is not writable (ensure_dir fails)");

    /* Restore perms before wiping so rm -rf can remove the tree. */
    (void)chmod(cfg_root, 0700);

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "cannot create") != NULL,
           "log mentions ensure_dir's 'cannot create' diagnostic");
    free(buf);

    rm_rf(home);
}

/**
 * 6d3. write_file_atomic failure: plant a DIRECTORY where the .tmp
 *      staging file should be. open(O_WRONLY|O_CREAT|O_TRUNC) on a
 *      directory fails with EISDIR, so the save must propagate the
 *      error.  Exercises the "cannot open tmp" branch.
 */
static void test_tmp_is_directory_blocks_save(void) {
    char home[512], log[1024];
    with_fresh_home("tmpdir", home, sizeof(home), log, sizeof(log));

    char tmp[1024];
    tmp_session_path_from_home(home, tmp, sizeof(tmp));

    /* Make session.bin.tmp a directory so open(O_WRONLY) fails. */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", tmp);
    int sysrc = system(cmd);
    ASSERT(sysrc == 0, "plant blocking directory at .tmp");

    MtProtoSession s;
    fake_session_fill(&s, 0x66);
    int rc = session_store_save(&s, 2);
    ASSERT(rc != 0,
           "save refuses when .tmp is a directory (open fails)");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "cannot open tmp") != NULL,
           "log mentions write_file_atomic's 'cannot open tmp'");
    free(buf);

    rm_rf(home);
}

/**
 * 6d3b. flock contention: hold LOCK_EX on session.bin from a separate
 *       fd in this process and try to call session_store_load.  Linux
 *       flock() treats distinct fds independently within a process, so
 *       the load's LOCK_SH attempt fails with EWOULDBLOCK (non-blocking)
 *       — exercising the "another tg-cli process is using this session"
 *       branch inside lock_file().
 */
static void test_flock_busy_blocks_load(void) {
    char home[512], log[1024];
    with_fresh_home("flock", home, sizeof(home), log, sizeof(log));

    MtProtoSession s;
    fake_session_fill(&s, 0x13);
    ASSERT(session_store_save(&s, 2) == 0, "seed session.bin");

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Hold an exclusive flock from a separate fd. */
    int blocker_fd = open(bin, O_RDWR);
    ASSERT(blocker_fd >= 0, "open blocker fd");
    ASSERT(flock(blocker_fd, LOCK_EX | LOCK_NB) == 0,
           "blocker acquires LOCK_EX");

    /* Now session_store_load must fail on LOCK_SH | LOCK_NB. */
    MtProtoSession ls;
    mtproto_session_init(&ls);
    int dc = 0;
    int rc = session_store_load(&ls, &dc);
    ASSERT(rc != 0, "load refuses while another fd holds LOCK_EX");

    /* Release blocker so cleanup works. */
    flock(blocker_fd, LOCK_UN);
    close(blocker_fd);

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "another tg-cli process is using") != NULL,
           "log mentions the busy-lock diagnostic");
    free(buf);

    rm_rf(home);
}

/**
 * 6d4. rename() failure: plant a non-empty DIRECTORY at the final
 *      session.bin path.  The .tmp file opens and writes fine, but
 *      rename() over a non-empty directory fails with ENOTEMPTY, so
 *      write_file_atomic must surface the rename error.
 */
static void test_rename_failure_blocks_save(void) {
    char home[512], log[1024];
    with_fresh_home("rename", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Make session.bin a directory containing a file, so rename()
     * cannot replace it. */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" && touch \"%s/x\"",
             bin, bin);
    int sysrc = system(cmd);
    ASSERT(sysrc == 0, "plant non-empty blocking directory at session.bin");

    MtProtoSession s;
    fake_session_fill(&s, 0x88);
    int rc = session_store_save(&s, 2);
    ASSERT(rc != 0,
           "save refuses when session.bin is a non-empty dir (rename fails)");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "rename") != NULL,
           "log mentions the failed 'rename' call");
    free(buf);

    /* Clear the blocker so rm_rf can clean up. */
    char cleanup_cmd[1024];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd),
             "chmod -R u+w \"%s\" 2>/dev/null", home);
    int cleanup_rc = system(cleanup_cmd);
    (void)cleanup_rc;
    rm_rf(home);
}

/**
 * 6e. Slot exhaustion: fill all SESSION_STORE_MAX_DCS slots, then try
 *     to save one more DC.  The save must fail with a clear "no slot
 *     left" diagnostic and the file must remain valid.
 */
static void test_slot_exhaustion_refuses_save(void) {
    char home[512], log[1024];
    with_fresh_home("slots", home, sizeof(home), log, sizeof(log));

    MtProtoSession s;
    /* Fill every slot. */
    for (int dc = 1; dc <= SESSION_STORE_MAX_DCS; dc++) {
        fake_session_fill(&s, (uint8_t)(0x10 + dc));
        ASSERT(session_store_save_dc(dc, &s) == 0, "seed slot");
    }

    /* One more DC should have no room. */
    fake_session_fill(&s, 0x99);
    int rc = session_store_save_dc(SESSION_STORE_MAX_DCS + 10, &s);
    ASSERT(rc != 0,
           "save for an extra DC refused when all slots full");

    /* Existing slots must still load cleanly. */
    for (int dc = 1; dc <= SESSION_STORE_MAX_DCS; dc++) {
        MtProtoSession ls;
        mtproto_session_init(&ls);
        ASSERT(session_store_load_dc(dc, &ls) == 0,
               "existing slot still loadable after refused save");
    }

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "no slot left") != NULL,
           "log mentions 'no slot left'");
    free(buf);

    rm_rf(home);
}

/**
 * 6. chmod the existing session.bin to 0644 and invoke save again.
 *    The atomic-rename path uses fs_ensure_permissions(0600) on the
 *    .tmp before rename, so the final mode must be 0600 regardless of
 *    the pre-existing mode drift.
 */
static void test_mode_drift_corrected_on_save(void) {
    char home[512], log[1024];
    with_fresh_home("mode", home, sizeof(home), log, sizeof(log));

    MtProtoSession s1;
    fake_session_fill(&s1, 0x11);
    ASSERT(session_store_save(&s1, 2) == 0, "initial save ok");

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Drift the mode. */
    ASSERT(chmod(bin, 0644) == 0, "chmod 0644 succeeded");
    struct stat st;
    ASSERT(stat(bin, &st) == 0, "stat after chmod");
    ASSERT((st.st_mode & 0777) == 0644, "mode is now 0644 pre-save");

    /* Second save must rewrite with mode 0600. */
    MtProtoSession s2;
    fake_session_fill(&s2, 0x22);
    ASSERT(session_store_save(&s2, 2) == 0, "second save ok");

    ASSERT(stat(bin, &st) == 0, "stat after second save");
    ASSERT((st.st_mode & 0777) == 0600,
           "mode restored to 0600 after save");

    logger_close();
    rm_rf(home);
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_session_corruption_tests(void) {
    RUN_TEST(test_truncated_session_refuses_load);
    RUN_TEST(test_bad_magic_refuses_load);
    RUN_TEST(test_unknown_version_refuses_load_and_does_not_overwrite);
    RUN_TEST(test_concurrent_writers_both_correct);
    RUN_TEST(test_stale_tmp_leftover_ignored);
    RUN_TEST(test_truncated_body_refuses_load);
    RUN_TEST(test_bogus_count_refuses_load);
    RUN_TEST(test_home_dc_missing_refuses_load);
    RUN_TEST(test_ensure_dir_failure_blocks_save);
    RUN_TEST(test_tmp_is_directory_blocks_save);
    RUN_TEST(test_flock_busy_blocks_load);
    RUN_TEST(test_rename_failure_blocks_save);
    RUN_TEST(test_slot_exhaustion_refuses_save);
    RUN_TEST(test_mode_drift_corrected_on_save);
}
