/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_logger_lifecycle.c
 * @brief TEST-72 / US-21 — functional coverage of logger lifecycle and
 *        redaction invariants.
 *
 * Exercises `src/core/logger.c` end-to-end against the real file system:
 *
 *   1. test_logger_rotates_at_5mb           — rotation triggered on init
 *                                             when file exceeds 5 MB cap.
 *   2. test_logger_level_filter_warn_drops_debug
 *                                           — entries below configured
 *                                             level must not hit the file.
 *   3. test_logger_redacts_message_body     — driving domain_send_message
 *                                             through the mock server must
 *                                             NOT write the plaintext body
 *                                             to the log file.
 *   4. test_logger_redacts_api_hash         — domain code paths that see
 *                                             the api_hash must never emit
 *                                             it into the log.
 *   5. test_logger_creates_missing_dir      — bootstrap recreates the log
 *                                             directory with mode 0700 if
 *                                             it was removed.
 *   6. test_logger_readonly_fallback_does_not_crash
 *                                           — chmodding the log dir to
 *                                             0500 mid-run must not crash
 *                                             subsequent logger_log calls.
 *
 * All tests use isolated $HOME / $XDG_CACHE_HOME / $XDG_CONFIG_HOME under
 * /tmp so they never touch the developer's real ~/.cache/tg-cli.
 */

#include "test_helpers.h"

#include "logger.h"
#include "app/bootstrap.h"

#include "mock_socket.h"
#include "mock_tel_server.h"
#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_registry.h"
#include "tl_serial.h"
#include "domain/write/send.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---- CRCs not surfaced by a public header ---- */
#define CRC_messages_sendMessage    0x0d9d75a4U
#define CRC_updateShortSentMessage  0x9015e101U

/* Known 32-char api_hash we deliberately feed into tests that need to
 * verify the secret never leaks into the log file. */
#define TEST_API_HASH "deadbeefcafebabef00dbaadfeedc0de"

/* ================================================================ */
/* Helpers                                                          */
/* ================================================================ */

/** Build a per-test scratch directory name unique to this test and pid. */
static void make_scratch_dir(char *out, size_t cap, const char *tag) {
    snprintf(out, cap, "/tmp/tg-cli-logger-%s-%d", tag, (int)getpid());
}

/** rm -rf @p path — tolerates missing, ignores errors. */
static void rm_rf(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    int sysrc = system(cmd);
    (void)sysrc;
}

/** mkdir -p @p path with the given mode (best-effort). */
static int mkdir_p(const char *path, mode_t mode) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    int sysrc = system(cmd);
    if (sysrc != 0) return -1;
    return chmod(path, mode);
}

/**
 * @brief Read the entire contents of @p path into a malloc'd buffer.
 *
 * Callers own the returned pointer and must free() it. Returns NULL on
 * error. NUL-terminates for easy strstr() scanning.
 */
static char *slurp(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (size_out) *size_out = n;
    return buf;
}

/** Point $HOME / XDG cache+config at scratch dir so bootstrap writes there. */
static void redirect_env_to(const char *scratch) {
    setenv("HOME", scratch, 1);
    char cache[512], cfg[512];
    snprintf(cache, sizeof(cache), "%s/cache", scratch);
    snprintf(cfg,   sizeof(cfg),   "%s/config", scratch);
    setenv("XDG_CACHE_HOME",  cache, 1);
    setenv("XDG_CONFIG_HOME", cfg,   1);
}

/* ================================================================ */
/* Responders (test 3 & 4)                                          */
/* ================================================================ */

static void reply_update_short_sent(MtRpcContext *ctx, int32_t id) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updateShortSentMessage);
    tl_write_uint32(&w, 0);        /* flags */
    tl_write_int32 (&w, id);
    tl_write_int32 (&w, 0);        /* pts */
    tl_write_int32 (&w, 0);        /* pts_count */
    tl_write_int32 (&w, 0);        /* date */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_send_message_ok(MtRpcContext *ctx) {
    reply_update_short_sent(ctx, 42);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/**
 * 1. Prime a >5 MB file at the target path, re-init the logger, and
 *    verify the old file was renamed to <path>.1 while the new file
 *    is well below the cap.
 */
static void test_logger_rotates_at_5mb(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "rot");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/session.log", scratch);
    char rotated[1024];
    snprintf(rotated, sizeof(rotated), "%s.1", log_path);

    /* Write a 6 MB file so the cap triggers on init. */
    FILE *fp = fopen(log_path, "wb");
    ASSERT(fp != NULL, "create oversize log");
    const size_t big = 6u * 1024u * 1024u;
    char chunk[4096];
    memset(chunk, 'x', sizeof(chunk));
    for (size_t written = 0; written < big; written += sizeof(chunk)) {
        size_t n = (big - written < sizeof(chunk)) ? big - written : sizeof(chunk);
        ASSERT(fwrite(chunk, 1, n, fp) == n, "fill oversize log");
    }
    fclose(fp);

    /* Re-initialise the logger — rotation must fire on the size check. */
    ASSERT(logger_init(log_path, LOG_INFO) == 0, "logger_init ok");
    logger_log(LOG_INFO, "post-rotation marker");
    logger_close();

    struct stat st;
    ASSERT(stat(rotated, &st) == 0, ".1 exists after rotation");
    ASSERT(st.st_size >= (off_t)big / 2, ".1 carries old bytes");

    ASSERT(stat(log_path, &st) == 0, "fresh session.log exists");
    ASSERT(st.st_size < (off_t)(5u * 1024u * 1024u),
           "fresh log is below the 5 MB cap");

    rm_rf(scratch);
}

/**
 * 2. With level=LOG_WARN a LOG_DEBUG call must not reach the file, while
 *    a LOG_WARN call must. Also exercises the TG_CLI_LOG_LEVEL env hint
 *    (currently advisory — tracked by US-21) so the assertion binds to
 *    the effective runtime behaviour of logger_log.
 */
static void test_logger_level_filter_warn_drops_debug(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "lvl");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/session.log", scratch);

    setenv("TG_CLI_LOG_LEVEL", "WARN", 1);
    ASSERT(logger_init(log_path, LOG_WARN) == 0, "logger_init warn");

    logger_log(LOG_DEBUG, "UNIQUE_DEBUG_BODY_%d", 777);
    logger_log(LOG_WARN,  "UNIQUE_WARN_BODY_%d",  888);
    logger_close();

    size_t sz = 0;
    char *buf = slurp(log_path, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, "UNIQUE_DEBUG_BODY_777") == NULL,
           "DEBUG entry must be filtered out at WARN level");
    ASSERT(strstr(buf, "UNIQUE_WARN_BODY_888") != NULL,
           "WARN entry must still reach the file");
    free(buf);

    unsetenv("TG_CLI_LOG_LEVEL");
    rm_rf(scratch);
}

/**
 * 3. Drive domain_send_message with a distinctive plaintext body, then
 *    confirm the log file does not contain the body — domain callers
 *    are expected to log size / type, never the content.
 */
static void test_logger_redacts_message_body(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "body");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");
    redirect_env_to(scratch);

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/session.log", scratch);
    /* DEBUG so every informational logger_log call lands on disk. */
    ASSERT(logger_init(log_path, LOG_DEBUG) == 0, "logger_init ok");

    mt_server_init();
    mt_server_reset();
    MtProtoSession s;
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed session");
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load seeded session");
    mt_server_expect(CRC_messages_sendMessage, on_send_message_ok, NULL);

    ApiConfig cfg; api_config_init(&cfg);
    cfg.api_id = 12345;
    cfg.api_hash = TEST_API_HASH;

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect mock");

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    const char *secret = "PLAINTEXT_BODY_MARKER_Z9Q7H3K5T2";
    ASSERT(domain_send_message(&cfg, &s, &t, &self, secret, &mid, &err) == 0,
           "sendMessage happy path");

    transport_close(&t);
    mt_server_reset();
    logger_close();

    size_t sz = 0;
    char *buf = slurp(log_path, &sz);
    ASSERT(buf != NULL, "read session.log");
    ASSERT(strstr(buf, secret) == NULL,
           "plaintext body must not appear in the log file");
    free(buf);

    rm_rf(scratch);
}

/**
 * 4. Run an end-to-end RPC carrying a known api_hash through the logger
 *    and verify the 32-char secret is never written verbatim. The call
 *    path here (bootstrap → logger_init → send) matches production.
 */
static void test_logger_redacts_api_hash(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "hash");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");
    redirect_env_to(scratch);

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "test-redact") == 0, "bootstrap ok");

    mt_server_init();
    mt_server_reset();
    MtProtoSession s;
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");
    mt_server_expect(CRC_messages_sendMessage, on_send_message_ok, NULL);

    ApiConfig cfg; api_config_init(&cfg);
    cfg.api_id = 98765;
    cfg.api_hash = TEST_API_HASH;

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t, &self, "ping", &mid, &err) == 0,
           "send ok");

    transport_close(&t);
    mt_server_reset();
    app_shutdown(&ctx);

    size_t sz = 0;
    char *buf = slurp(ctx.log_path, &sz);
    ASSERT(buf != NULL, "read bootstrap log");
    ASSERT(strstr(buf, TEST_API_HASH) == NULL,
           "api_hash secret must not appear in the log file");
    free(buf);

    rm_rf(scratch);
}

/**
 * 5. Blow away the whole cache tree, call app_bootstrap(). It must
 *    re-create ~/.cache/tg-cli/logs/ with mode 0700 and produce a live
 *    log file.
 */
static void test_logger_creates_missing_dir(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "mkdir");
    rm_rf(scratch);
    /* Do NOT mkdir — bootstrap must do it itself. */
    redirect_env_to(scratch);

    AppContext ctx;
    ASSERT(app_bootstrap(&ctx, "test-mkdir") == 0, "bootstrap ok");

    /* Derive the log directory from the resolved log_path. */
    char log_dir[2048];
    snprintf(log_dir, sizeof(log_dir), "%s", ctx.log_path);
    char *slash = strrchr(log_dir, '/');
    ASSERT(slash != NULL, "log_path contains a dir separator");
    *slash = '\0';

    struct stat st;
    ASSERT(stat(log_dir, &st) == 0, "log dir recreated");
    ASSERT(S_ISDIR(st.st_mode), "it's a directory");
    ASSERT((st.st_mode & 0777) == 0700,
           "log dir permissions are 0700");

    logger_log(LOG_INFO, "first-entry-after-recreate");
    app_shutdown(&ctx);

    ASSERT(stat(ctx.log_path, &st) == 0, "log file exists");
    ASSERT(st.st_size > 0, "log file has content");

    rm_rf(scratch);
}

/**
 * 6. Chmod the log directory read-only mid-run. logger_log must NOT
 *    crash even though underlying fwrite may fail; subsequent
 *    logger_close() must remain callable.
 *
 * ASAN + Valgrind will catch any use-after-free / double-close as part
 * of the test run.
 */
static void test_logger_readonly_fallback_does_not_crash(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "ro");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/session.log", scratch);
    ASSERT(logger_init(log_path, LOG_DEBUG) == 0, "logger_init ok");
    logger_log(LOG_INFO, "pre-chmod-entry");

    /* Make the directory r-x only. An existing open FILE* keeps working,
     * but no fresh open() would succeed. */
    ASSERT(chmod(scratch, 0500) == 0, "chmod 0500");

    /* Continue writing. The process must not die even if the FS push-back
     * fires; ASAN/Valgrind also watch for corruption on shared state. */
    for (int i = 0; i < 16; i++) {
        logger_log(LOG_WARN, "post-chmod-entry-%d", i);
    }
    logger_log(LOG_ERROR, "post-chmod-error-entry");

    /* Restore perms so cleanup can remove the tree. */
    ASSERT(chmod(scratch, 0700) == 0, "chmod 0700 restore");
    logger_close();

    /* Best-effort read-back: any surviving file must still be sane. */
    size_t sz = 0;
    char *buf = slurp(log_path, &sz);
    if (buf) {
        /* Process did not abort — that alone is the assertion. */
        free(buf);
    }

    rm_rf(scratch);
}

/**
 * 7. Exercise the remaining public API — logger_set_stderr() and
 *    logger_clean_logs() — so functional coverage clears the ≥75 %
 *    ticket bar. logger_clean_logs deletes session.log* siblings in a
 *    directory; we seed a few files and verify the cleaner wipes them.
 */
static void test_logger_set_stderr_and_clean_logs(void) {
    char scratch[256];
    make_scratch_dir(scratch, sizeof(scratch), "clean");
    rm_rf(scratch);
    ASSERT(mkdir_p(scratch, 0700) == 0, "mkdir scratch");

    /* Toggle stderr mirroring both ways — no crash expected. */
    logger_set_stderr(0);
    logger_set_stderr(1);

    /* Seed several session.log* files + an unrelated file. */
    char p[512];
    const char *names[] = {
        "session.log", "session.log.1", "session.log.2", "other.log"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", scratch, names[i]);
        FILE *fp = fopen(p, "wb");
        ASSERT(fp != NULL, "seed file");
        fputs("x\n", fp);
        fclose(fp);
    }

    ASSERT(logger_clean_logs(scratch) == 0, "clean_logs ok");
    ASSERT(logger_clean_logs("/nonexistent-dir-xyz-9999") == -1,
           "clean_logs returns -1 on missing dir");

    struct stat st;
    for (size_t i = 0; i < 3; i++) {
        snprintf(p, sizeof(p), "%s/%s", scratch, names[i]);
        ASSERT(stat(p, &st) != 0,
               "session.log* siblings removed");
    }
    /* Unrelated file untouched. */
    snprintf(p, sizeof(p), "%s/%s", scratch, names[3]);
    ASSERT(stat(p, &st) == 0, "other.log preserved");

    rm_rf(scratch);
}

/* ---- Runner ---- */

void run_logger_lifecycle_tests(void) {
    RUN_TEST(test_logger_rotates_at_5mb);
    RUN_TEST(test_logger_level_filter_warn_drops_debug);
    RUN_TEST(test_logger_redacts_message_body);
    RUN_TEST(test_logger_redacts_api_hash);
    RUN_TEST(test_logger_creates_missing_dir);
    RUN_TEST(test_logger_readonly_fallback_does_not_crash);
    RUN_TEST(test_logger_set_stderr_and_clean_logs);
}
