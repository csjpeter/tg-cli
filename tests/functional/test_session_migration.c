/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_session_migration.c
 * @brief TEST-83 / US-32 — functional coverage of session.bin schema
 *        migration (legacy v1 → current v2) and future-version rejection.
 *
 * Scenarios covered:
 *   1. test_v1_file_loads_into_v2_in_memory
 *      Hand-craft a valid legacy v1 session.bin (single DC, 284 bytes);
 *      session_store_load populates the in-memory MtProtoSession with the
 *      auth_key / salt / session_id and reports the v1-recorded dc_id.
 *   2. test_v1_file_rewritten_as_v2_on_save
 *      After the v1 load succeeds the next session_store_save atomically
 *      rewrites session.bin in v2 format — magic + version byte + multi-DC
 *      structure — with mode 0600 preserved.
 *   3. test_crash_between_v1_load_and_v2_save_keeps_v1
 *      Simulate an "atomic-rename fails" crash between load and save by
 *      making the save path unwritable (plant a directory where the
 *      session.bin.tmp is created). The save returns non-zero; the
 *      original v1 bytes remain on disk, so the next run retries.
 *   4. test_future_v3_file_rejected_without_clobber
 *      Plant a fake-future v3 file; session_store_load fails with the
 *      "unknown session version — upgrade client" diagnostic, and a
 *      byte-for-byte snapshot proves the load path leaves the file
 *      untouched (a newer client's state is never silently overwritten).
 *
 * Each test runs inside its own /tmp scratch $HOME.  XDG_CONFIG_HOME /
 * XDG_CACHE_HOME are unset because the CI runners (GitHub Actions) set
 * them, which would otherwise override the $HOME-based config root that
 * platform_config_dir() derives.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Schema constants (mirrored from session_store.c).                  */
/* ------------------------------------------------------------------ */

#define STORE_MAGIC_STR       "TGCS"
#define STORE_VERSION_V1      1
#define STORE_VERSION_V2      2
#define STORE_HEADER_SIZE     16
#define STORE_ENTRY_SIZE      276
/* v1 payload: magic(4) + ver(4) + dc_id(4) + server_salt(8) +
 *             session_id(8) + auth_key(256) = 284 bytes. */
#define STORE_V1_TOTAL_SIZE   284

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Build a scratch HOME path keyed by test tag + pid. */
static void scratch_dir_for(const char *tag, char *out, size_t cap) {
    snprintf(out, cap, "/tmp/tg-cli-ft-sessmigr-%s-%d", tag, (int)getpid());
}

/** rm -rf best-effort. */
static void rm_rf(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    int sysrc = system(cmd);
    (void)sysrc;
}

/** mkdir -p wrapper. */
static int mkdir_p(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    int sysrc = system(cmd);
    return sysrc == 0 ? 0 : -1;
}

/**
 * Redirect $HOME to a fresh scratch dir, unset XDG_* so the production
 * code's platform_config_dir() actually derives the config root from our
 * redirected $HOME, and point the logger at a per-test log file so the
 * assertions can pattern-match diagnostics.
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
    /* CI runners (GitHub Actions) set these; they must be cleared so
     * platform_config_dir() / platform_cache_dir() fall back to $HOME. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");

    snprintf(out_log, log_cap, "%s/session.log", cache_dir);
    (void)unlink(out_log);
    (void)logger_init(out_log, LOG_DEBUG);
}

/** Build the canonical session.bin path under @p home. */
static void session_path_from_home(const char *home, char *out, size_t cap) {
    snprintf(out, cap, "%s/.config/tg-cli/session.bin", home);
}

/** Read a file into a heap buffer (caller frees). NUL-terminated for
 *  safe strstr() over binary files. */
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

/** Overwrite @p path with @p buf. */
static int write_full(const char *path, const uint8_t *buf, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) return -1;
    ssize_t w = write(fd, buf, n);
    close(fd);
    return (w < 0 || (size_t)w != n) ? -1 : 0;
}

/**
 * Craft a valid legacy v1 session.bin payload into @p buf (>=284 bytes).
 *
 * Layout:
 *   offset 0  — "TGCS"            (4 B)
 *   offset 4  — version = 1       (int32 LE)
 *   offset 8  — dc_id             (int32 LE)
 *   offset 12 — server_salt       (uint64 LE)
 *   offset 20 — session_id        (uint64 LE)
 *   offset 28 — auth_key          (256 B, filled with @p key_byte)
 */
static size_t craft_v1_file(uint8_t *buf, size_t cap,
                            int32_t dc_id, uint64_t salt, uint64_t sess,
                            uint8_t key_byte) {
    if (cap < STORE_V1_TOTAL_SIZE) return 0;
    memset(buf, 0, STORE_V1_TOTAL_SIZE);
    memcpy(buf, STORE_MAGIC_STR, 4);
    int32_t v = STORE_VERSION_V1;
    memcpy(buf + 4,  &v,     4);
    memcpy(buf + 8,  &dc_id, 4);
    memcpy(buf + 12, &salt,  8);
    memcpy(buf + 20, &sess,  8);
    memset(buf + 28, key_byte, 256);
    return STORE_V1_TOTAL_SIZE;
}

/** Craft a fake future v3 file (same outer shape as v2 but version=3). */
static size_t craft_v3_file(uint8_t *buf, size_t cap, uint8_t key_byte) {
    size_t need = STORE_HEADER_SIZE + STORE_ENTRY_SIZE;
    if (cap < need) return 0;
    memset(buf, 0, need);
    memcpy(buf, STORE_MAGIC_STR, 4);
    int32_t v = 3;
    memcpy(buf + 4, &v, 4);
    int32_t home_dc = 2;
    memcpy(buf + 8, &home_dc, 4);
    uint32_t count = 1;
    memcpy(buf + 12, &count, 4);
    int32_t  dc_id       = 2;
    uint64_t server_salt = 0xDEADBEEFCAFEBABEULL;
    uint64_t session_id  = 0x0011223344556677ULL;
    memcpy(buf + STORE_HEADER_SIZE + 0,  &dc_id,       4);
    memcpy(buf + STORE_HEADER_SIZE + 4,  &server_salt, 8);
    memcpy(buf + STORE_HEADER_SIZE + 12, &session_id,  8);
    memset(buf + STORE_HEADER_SIZE + 20, key_byte, 256);
    return need;
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * 1. A hand-crafted valid v1 file loads into the v2 in-memory struct.
 *    The loader must populate every relevant field of MtProtoSession
 *    (auth_key, salt, session_id) and surface the recorded dc_id as
 *    the home DC.
 */
static void test_v1_file_loads_into_v2_in_memory(void) {
    char home[512], log[1024];
    with_fresh_home("v1load", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t v1[STORE_V1_TOTAL_SIZE];
    uint64_t salt = 0x1122334455667788ULL;
    uint64_t sess = 0xAABBCCDDEEFF0011ULL;
    size_t n = craft_v1_file(v1, sizeof(v1),
                             /*dc_id=*/2, salt, sess, /*key_byte=*/0xA5);
    ASSERT(n == STORE_V1_TOTAL_SIZE, "crafted v1 file size correct");
    ASSERT(write_full(bin, v1, n) == 0, "v1 file written to scratch HOME");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc == 0, "session_store_load accepts legacy v1 payload");
    ASSERT(dc == 2, "home DC reported as the v1 entry's dc_id");
    ASSERT(s.server_salt == salt, "server_salt carried across migration");
    ASSERT(s.session_id  == sess, "session_id carried across migration");
    ASSERT(s.has_auth_key == 1, "auth_key flagged as present post-migration");
    /* Spot-check auth_key contents (every byte should be 0xA5). */
    int all_a5 = 1;
    for (size_t i = 0; i < MTPROTO_AUTH_KEY_SIZE; i++) {
        if (s.auth_key[i] != 0xA5) { all_a5 = 0; break; }
    }
    ASSERT(all_a5, "auth_key bytes intact after v1→v2 load");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "migrated v1 file") != NULL,
           "log announces the v1→v2 migration explicitly");
    free(buf);

    rm_rf(home);
}

/**
 * 2. After the v1 load, the next save must rewrite session.bin in v2
 *    format: magic preserved, version byte stamped as 2, home_dc / count
 *    header filled in, and mode 0600 intact.
 */
static void test_v1_file_rewritten_as_v2_on_save(void) {
    char home[512], log[1024];
    with_fresh_home("v1rewrite", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Plant a v1 file. */
    uint8_t v1[STORE_V1_TOTAL_SIZE];
    uint64_t salt = 0x5555666677778888ULL;
    uint64_t sess = 0x9999AAAABBBBCCCCULL;
    (void)craft_v1_file(v1, sizeof(v1), /*dc_id=*/3, salt, sess, 0x7E);
    ASSERT(write_full(bin, v1, sizeof(v1)) == 0, "v1 seed written");

    /* Load (migrates into v2 in memory). */
    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "v1 load succeeds");
    ASSERT(dc == 3, "home DC recorded from v1");

    /* Save: should rewrite on-disk file in v2 format. */
    ASSERT(session_store_save(&s, dc) == 0, "save rewrites in v2 format");

    /* Inspect the resulting file. */
    size_t sz = 0;
    uint8_t *after = (uint8_t *)slurp(bin, &sz);
    ASSERT(after != NULL, "read post-save session.bin");
    ASSERT(sz == (size_t)(STORE_HEADER_SIZE + STORE_ENTRY_SIZE),
           "post-save file has exactly one v2 entry body (276 B + 16 B header)");
    ASSERT(memcmp(after, STORE_MAGIC_STR, 4) == 0, "magic preserved");

    int32_t ver_on_disk = 0;
    memcpy(&ver_on_disk, after + 4, 4);
    ASSERT(ver_on_disk == STORE_VERSION_V2,
           "version byte bumped to 2 on save");

    int32_t home_on_disk = 0;
    memcpy(&home_on_disk, after + 8, 4);
    ASSERT(home_on_disk == 3, "home_dc_id preserved through migration");

    uint32_t count_on_disk = 0;
    memcpy(&count_on_disk, after + 12, 4);
    ASSERT(count_on_disk == 1, "entry count == 1 for single migrated DC");

    int32_t entry_dc = 0;
    memcpy(&entry_dc, after + STORE_HEADER_SIZE + 0, 4);
    ASSERT(entry_dc == 3, "entry[0].dc_id preserved");

    uint64_t entry_salt = 0;
    memcpy(&entry_salt, after + STORE_HEADER_SIZE + 4, 8);
    ASSERT(entry_salt == salt, "entry[0].server_salt preserved");

    uint64_t entry_sess = 0;
    memcpy(&entry_sess, after + STORE_HEADER_SIZE + 12, 8);
    ASSERT(entry_sess == sess, "entry[0].session_id preserved");

    free(after);

    struct stat st;
    ASSERT(stat(bin, &st) == 0, "stat post-save");
    ASSERT((st.st_mode & 0777) == 0600,
           "mode 0600 applied to migrated file");

    /* And the re-load from v2 must come back clean. */
    MtProtoSession s2;
    mtproto_session_init(&s2);
    int dc2 = 0;
    ASSERT(session_store_load(&s2, &dc2) == 0, "re-load from v2 succeeds");
    ASSERT(dc2 == 3, "home DC round-trips");
    ASSERT(s2.server_salt == salt, "salt round-trips");
    ASSERT(s2.session_id  == sess, "session id round-trips");

    logger_close();
    rm_rf(home);
}

/**
 * 3. Simulate a crash mid-save by making the config directory hostile
 *    to the atomic-rename path (plant a *directory* at session.bin.tmp
 *    so open(O_WRONLY|O_CREAT|O_TRUNC) fails with EISDIR).  The save
 *    must return non-zero *and* the original v1 file must still be
 *    byte-identical on disk so the next run can retry.
 */
static void test_crash_between_v1_load_and_v2_save_keeps_v1(void) {
    char home[512], log[1024];
    with_fresh_home("v1crash", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));
    char tmp[1024];
    snprintf(tmp, sizeof(tmp),
             "%s/.config/tg-cli/session.bin.tmp", home);

    /* Plant the v1 file. */
    uint8_t v1[STORE_V1_TOTAL_SIZE];
    uint64_t salt = 0xAAAA1111BBBB2222ULL;
    uint64_t sess = 0x3333CCCC4444DDDDULL;
    (void)craft_v1_file(v1, sizeof(v1), /*dc_id=*/4, salt, sess, 0x33);
    ASSERT(write_full(bin, v1, sizeof(v1)) == 0, "v1 seed written");

    /* Snapshot bytes on disk before attempting migration. */
    size_t before_sz = 0;
    char *before = slurp(bin, &before_sz);
    ASSERT(before != NULL, "snapshot v1 bytes pre-save");
    ASSERT(before_sz == STORE_V1_TOTAL_SIZE, "snapshot size matches v1");

    /* Perform the v1 load. */
    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "v1 load still succeeds");
    ASSERT(dc == 4, "home DC from v1");

    /* Now sabotage the atomic-rename path: turn session.bin.tmp into a
     * directory so the next open(..., O_WRONLY|O_TRUNC) fails. */
    char mkcmd[2048];
    snprintf(mkcmd, sizeof(mkcmd), "mkdir -p \"%s\"", tmp);
    int sysrc = system(mkcmd);
    ASSERT(sysrc == 0, "plant blocking directory at .tmp");

    /* Save must fail (cannot create its staging file). */
    int rc = session_store_save(&s, dc);
    ASSERT(rc != 0, "save fails when staging .tmp cannot be opened");

    /* The critical invariant: on-disk v1 bytes still intact. */
    size_t after_sz = 0;
    char *after = slurp(bin, &after_sz);
    ASSERT(after != NULL, "snapshot bytes post-failed-save");
    ASSERT(after_sz == before_sz,
           "failed-save leaves v1 file size unchanged");
    ASSERT(memcmp(before, after, before_sz) == 0,
           "failed-save leaves v1 file bytes unchanged (retry safe)");
    free(before);
    free(after);

    /* Re-run the migration (simulating a restart) — it must still work
     * once the blocker is cleared. */
    char rmcmd[2048];
    snprintf(rmcmd, sizeof(rmcmd), "rmdir \"%s\"", tmp);
    int rmrc = system(rmcmd);
    ASSERT(rmrc == 0, "remove blocking directory");

    MtProtoSession s2;
    mtproto_session_init(&s2);
    int dc2 = 0;
    ASSERT(session_store_load(&s2, &dc2) == 0,
           "v1 load retried successfully");
    ASSERT(session_store_save(&s2, dc2) == 0,
           "save succeeds on retry");

    /* Confirm file is now v2. */
    size_t final_sz = 0;
    uint8_t *final = (uint8_t *)slurp(bin, &final_sz);
    ASSERT(final != NULL, "read final session.bin");
    int32_t final_ver = 0;
    memcpy(&final_ver, final + 4, 4);
    ASSERT(final_ver == STORE_VERSION_V2,
           "file finally rewritten as v2 on retry");
    free(final);

    logger_close();

    /* Clean up any residual chmod drift under the scratch tree. */
    char cleanup_cmd[1024];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd),
             "chmod -R u+w \"%s\" 2>/dev/null", home);
    int cleanup_rc = system(cleanup_cmd);
    (void)cleanup_rc;
    rm_rf(home);
}

/**
 * 4. A fake future v3 file must be refused without clobbering.
 *    session_store_load returns non-zero; the on-disk bytes are
 *    unchanged; the log carries the "unknown session version — upgrade
 *    client" diagnostic so the operator knows to upgrade rather than
 *    re-authenticate.
 */
static void test_future_v3_file_rejected_without_clobber(void) {
    char home[512], log[1024];
    with_fresh_home("v3", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t v3[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    size_t n = craft_v3_file(v3, sizeof(v3), 0xF3);
    ASSERT(n > 0, "crafted v3 blob");
    ASSERT(write_full(bin, v3, n) == 0, "v3 file written");

    /* Snapshot before load. */
    size_t before_sz = 0;
    char *before = slurp(bin, &before_sz);
    ASSERT(before != NULL, "snapshot v3 bytes pre-load");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses an unknown-future v3 file");
    /* has_auth_key should remain cleared — the loader must not have
     * populated the struct from a version it cannot parse. */
    ASSERT(s.has_auth_key == 0,
           "in-memory session untouched by failed v3 load");

    /* On-disk bytes unchanged. */
    size_t after_sz = 0;
    char *after = slurp(bin, &after_sz);
    ASSERT(after != NULL, "snapshot post-failed-load");
    ASSERT(after_sz == before_sz,
           "load-only leaves v3 file size unchanged");
    ASSERT(memcmp(before, after, before_sz) == 0,
           "load-only leaves v3 file bytes unchanged (no clobber)");
    free(before);
    free(after);

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "unknown session version") != NULL,
           "log mentions 'unknown session version'");
    ASSERT(strstr(buf, "upgrade client") != NULL,
           "log mentions 'upgrade client' remediation hint");
    ASSERT(strstr(buf, "migrated v1") == NULL,
           "no spurious v1-migration log for a v3 file");
    free(buf);

    rm_rf(home);
}

/**
 * 5. A v1 header with a truncated body (version byte says 1 but fewer
 *    than 284 bytes are on disk) is rejected with a distinct diagnostic.
 *    This guards the v1-migration code against reading off the end of
 *    the buffer and ensures a partially-written legacy file is not
 *    silently treated as valid.
 */
static void test_truncated_v1_file_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("v1trunc", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    /* Build a complete v1 payload then keep only the first 100 bytes —
     * past the 16-byte header so the magic + version checks both pass,
     * but well short of the 284-byte full payload. */
    uint8_t v1[STORE_V1_TOTAL_SIZE];
    (void)craft_v1_file(v1, sizeof(v1), /*dc_id=*/2,
                        0x1111ULL, 0x2222ULL, 0x5C);
    ASSERT(write_full(bin, v1, 100) == 0, "short v1 file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a truncated-v1 file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "truncated v1 payload") != NULL,
           "log mentions 'truncated v1 payload'");
    free(buf);

    rm_rf(home);
}

/**
 * 6. A version-zero file (neither v1 nor v2 nor future) must take the
 *    plain "unsupported version" branch — distinct from the "upgrade
 *    client" diagnostic reserved for forward-only incompatibility.
 *    This keeps the legacy corruption-recovery contract (US-25) intact.
 */
static void test_unsupported_low_version_refuses_load(void) {
    char home[512], log[1024];
    with_fresh_home("v0", home, sizeof(home), log, sizeof(log));

    char bin[1024];
    session_path_from_home(home, bin, sizeof(bin));

    uint8_t pkt[STORE_HEADER_SIZE + STORE_ENTRY_SIZE];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, STORE_MAGIC_STR, 4);
    int32_t v = 0;                       /* explicitly non-v1, non-v2, non-future */
    memcpy(pkt + 4, &v, 4);
    int32_t home_dc = 2;
    memcpy(pkt + 8, &home_dc, 4);
    uint32_t count = 0;
    memcpy(pkt + 12, &count, 4);
    ASSERT(write_full(bin, pkt, sizeof(pkt)) == 0,
           "v0 file written");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    int rc = session_store_load(&s, &dc);
    ASSERT(rc != 0, "load refuses a v0 (below-current) file");

    logger_close();
    size_t sz = 0;
    char *buf = slurp(log, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "unsupported version 0") != NULL,
           "log mentions 'unsupported version 0'");
    /* The low-version branch must NOT emit the upgrade hint — that hint
     * is specifically for forward-incompatible (future) versions. */
    ASSERT(strstr(buf, "upgrade client") == NULL,
           "no 'upgrade client' hint for sub-current versions");
    free(buf);

    rm_rf(home);
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_session_migration_tests(void) {
    RUN_TEST(test_v1_file_loads_into_v2_in_memory);
    RUN_TEST(test_v1_file_rewritten_as_v2_on_save);
    RUN_TEST(test_crash_between_v1_load_and_v2_save_keeps_v1);
    RUN_TEST(test_future_v3_file_rejected_without_clobber);
    RUN_TEST(test_truncated_v1_file_refuses_load);
    RUN_TEST(test_unsupported_low_version_refuses_load);
}
