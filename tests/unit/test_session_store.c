/**
 * @file test_session_store.c
 * @brief Unit tests for session persistence.
 *
 * Overrides HOME so the test doesn't touch the developer's real
 * ~/.config/tg-cli/session.bin.
 */

#include "test_helpers.h"
#include "app/session_store.h"
#include "mtproto_session.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void with_tmp_home(const char *subdir) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-session-test-%s", subdir);
    /* Best-effort cleanup of stale leftovers */
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(path);
    setenv("HOME", tmp, 1);
}

static void test_save_load_roundtrip(void) {
    with_tmp_home("roundtrip");

    MtProtoSession s;
    mtproto_session_init(&s);
    uint8_t key[256];
    for (int i = 0; i < 256; i++) key[i] = (uint8_t)(i * 7 + 3);
    mtproto_session_set_auth_key(&s, key);
    mtproto_session_set_salt(&s, 0xDEADBEEFCAFEBABEULL);
    s.session_id = 0x1122334455667788ULL;

    ASSERT(session_store_save(&s, 4) == 0, "save succeeds");

    MtProtoSession r;
    mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "load succeeds");
    ASSERT(dc == 4, "dc_id restored");
    ASSERT(r.has_auth_key == 1, "has_auth_key set");
    ASSERT(r.server_salt == 0xDEADBEEFCAFEBABEULL, "salt restored");
    ASSERT(r.session_id == 0x1122334455667788ULL, "session_id restored");
    ASSERT(memcmp(r.auth_key, key, 256) == 0, "auth_key matches");

    session_store_clear();
}

static void test_load_missing_file(void) {
    with_tmp_home("missing");
    /* Ensure no file */
    session_store_clear();

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == -1, "load returns -1 if no file");
}

static void test_save_without_key_fails(void) {
    with_tmp_home("no-key");

    MtProtoSession s;
    mtproto_session_init(&s); /* has_auth_key=0 */
    ASSERT(session_store_save(&s, 2) == -1, "save without key fails");
}

static void test_load_wrong_magic(void) {
    with_tmp_home("bad-magic");

    char path[256];
    snprintf(path, sizeof(path),
             "%s/.config/tg-cli/session.bin", getenv("HOME"));
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/tg-cli", getenv("HOME"));
    mkdir("/tmp/tg-cli-session-test-bad-magic", 0700);
    char base[256];
    snprintf(base, sizeof(base), "%s/.config",
             "/tmp/tg-cli-session-test-bad-magic");
    mkdir(base, 0700);
    mkdir(dir, 0700);

    FILE *f = fopen(path, "wb");
    if (f) {
        char junk[284] = "XXXX";
        fwrite(junk, 1, sizeof(junk), f);
        fclose(f);
    }
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == -1, "bad magic rejected");

    session_store_clear();
}

/*
 * Test: test_load_wrong_version
 *
 * Writes a session file with the correct "TGCS" magic but version = 3
 * (STORE_VERSION + 1). session_store_load must return -1 and must not
 * modify the file.
 */
static void test_load_wrong_version(void) {
    with_tmp_home("wrong-version");

    /* Build the directory tree manually (mirrors test_load_wrong_magic). */
    mkdir("/tmp/tg-cli-session-test-wrong-version", 0700);
    char base[256];
    snprintf(base, sizeof(base),
             "%s/.config", "/tmp/tg-cli-session-test-wrong-version");
    mkdir(base, 0700);
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/tg-cli",
             "/tmp/tg-cli-session-test-wrong-version");
    mkdir(dir, 0700);

    char path[256];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin",
             getenv("HOME"));

    /* Build a minimal header: "TGCS" + version=3 + home_dc_id=0 + count=0. */
    uint8_t header[16];
    memset(header, 0, sizeof(header));
    memcpy(header, "TGCS", 4);
    int32_t bad_ver = 3;   /* STORE_VERSION (2) + 1 */
    memcpy(header + 4, &bad_ver, 4);

    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL, "created session file for wrong-version test");
    if (f) {
        fwrite(header, 1, sizeof(header), f);
        fclose(f);
    }

    /* Record mtime/size before calling load so we can verify no modification. */
    struct stat st_before;
    ASSERT(stat(path, &st_before) == 0, "stat before load");

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == -1, "wrong version rejected");

    struct stat st_after;
    ASSERT(stat(path, &st_after) == 0, "stat after load");
    ASSERT(st_before.st_size  == st_after.st_size,  "file size unchanged");
    ASSERT(st_before.st_mtime == st_after.st_mtime, "file mtime unchanged");

    (void)unlink(path);
}

/*
 * Test: test_load_truncated_file
 *
 * Writes a session file whose content is shorter than the required 16-byte
 * header. session_store_load must return -1.
 */
static void test_load_truncated_file(void) {
    with_tmp_home("truncated");

    mkdir("/tmp/tg-cli-session-test-truncated", 0700);
    char base[256];
    snprintf(base, sizeof(base),
             "%s/.config", "/tmp/tg-cli-session-test-truncated");
    mkdir(base, 0700);
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/tg-cli",
             "/tmp/tg-cli-session-test-truncated");
    mkdir(dir, 0700);

    char path[256];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin",
             getenv("HOME"));

    /* Only 8 bytes — less than the 16-byte STORE_HEADER. */
    uint8_t short_buf[8];
    memcpy(short_buf, "TGCS", 4);
    memset(short_buf + 4, 0, 4);

    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL, "created truncated session file");
    if (f) {
        fwrite(short_buf, 1, sizeof(short_buf), f);
        fclose(f);
    }

    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == -1, "truncated file rejected");

    (void)unlink(path);
}

/* Multi-DC: save two DCs, each round-trips cleanly. */
static void test_multi_dc_save_load(void) {
    with_tmp_home("multi-dc");

    MtProtoSession home; mtproto_session_init(&home);
    uint8_t k_home[256]; for (int i = 0; i < 256; i++) k_home[i] = (uint8_t)i;
    mtproto_session_set_auth_key(&home, k_home);
    mtproto_session_set_salt(&home, 0xA1A1A1A1A1A1A1A1ULL);
    home.session_id = 0x0101010101010101ULL;

    MtProtoSession media; mtproto_session_init(&media);
    uint8_t k_media[256]; for (int i = 0; i < 256; i++) k_media[i] = (uint8_t)(255 - i);
    mtproto_session_set_auth_key(&media, k_media);
    mtproto_session_set_salt(&media, 0xB2B2B2B2B2B2B2B2ULL);
    media.session_id = 0x0202020202020202ULL;

    ASSERT(session_store_save(&home, 2) == 0, "home save DC2");
    ASSERT(session_store_save_dc(4, &media) == 0, "secondary save DC4");

    /* load home → DC2 */
    MtProtoSession r; mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "load home ok");
    ASSERT(dc == 2, "home stayed DC2 after secondary save");
    ASSERT(r.server_salt == 0xA1A1A1A1A1A1A1A1ULL, "home salt");
    ASSERT(memcmp(r.auth_key, k_home, 256) == 0, "home auth_key");

    /* load secondary DC4 */
    MtProtoSession r2; mtproto_session_init(&r2);
    ASSERT(session_store_load_dc(4, &r2) == 0, "load DC4 ok");
    ASSERT(r2.server_salt == 0xB2B2B2B2B2B2B2B2ULL, "DC4 salt");
    ASSERT(memcmp(r2.auth_key, k_media, 256) == 0, "DC4 auth_key");

    /* load a DC we never saved */
    MtProtoSession r3; mtproto_session_init(&r3);
    ASSERT(session_store_load_dc(5, &r3) == -1, "unknown DC rejected");

    session_store_clear();
}

/* Upserting the same DC overwrites in place and does not grow count. */
static void test_upsert_in_place(void) {
    with_tmp_home("upsert");

    MtProtoSession s1; mtproto_session_init(&s1);
    uint8_t k1[256]; for (int i = 0; i < 256; i++) k1[i] = (uint8_t)(i ^ 0x55);
    mtproto_session_set_auth_key(&s1, k1);
    mtproto_session_set_salt(&s1, 0x1111111111111111ULL);
    s1.session_id = 0xAAAAAAAAAAAAAAAAULL;
    ASSERT(session_store_save(&s1, 2) == 0, "first save");

    MtProtoSession s2; mtproto_session_init(&s2);
    uint8_t k2[256]; for (int i = 0; i < 256; i++) k2[i] = (uint8_t)(i ^ 0xAA);
    mtproto_session_set_auth_key(&s2, k2);
    mtproto_session_set_salt(&s2, 0x2222222222222222ULL);
    s2.session_id = 0xBBBBBBBBBBBBBBBBULL;
    ASSERT(session_store_save(&s2, 2) == 0, "overwrite save");

    MtProtoSession r; mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "load after upsert");
    ASSERT(dc == 2, "home still DC2");
    ASSERT(r.server_salt == 0x2222222222222222ULL, "salt overwritten");
    ASSERT(memcmp(r.auth_key, k2, 256) == 0, "auth_key overwritten");

    session_store_clear();
}

/* save_dc on an empty store still sets home (bootstrap convenience). */
static void test_save_dc_on_empty_sets_home(void) {
    with_tmp_home("save-dc-empty");
    session_store_clear();

    MtProtoSession s; mtproto_session_init(&s);
    uint8_t k[256] = {0};
    mtproto_session_set_auth_key(&s, k);
    mtproto_session_set_salt(&s, 42);
    ASSERT(session_store_save_dc(4, &s) == 0, "save_dc on empty");

    MtProtoSession r; mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "load picks up the only DC");
    ASSERT(dc == 4, "home promoted to DC4");

    session_store_clear();
}

static void test_null_args(void) {
    ASSERT(session_store_save(NULL, 1) == -1, "null session rejected");
    int dc = 0;
    ASSERT(session_store_load(NULL, &dc) == -1, "null session (load)");
}

/* Helper: build a valid session with a recognisable key pattern. */
static void make_session(MtProtoSession *s, uint8_t fill) {
    mtproto_session_init(s);
    uint8_t key[256];
    for (int i = 0; i < 256; i++) key[i] = (uint8_t)(fill + i);
    mtproto_session_set_auth_key(s, key);
    mtproto_session_set_salt(s, (uint64_t)fill * 0x0101010101010101ULL);
    s->session_id = (uint64_t)fill;
}

/*
 * Test: write_under_lock
 *
 * Verifies that session_store_save() succeeds and the resulting file can be
 * loaded back correctly — i.e. the lock-acquire + atomic rename path works
 * end-to-end in the normal (non-contended) case.
 */
static void test_write_under_lock(void) {
    with_tmp_home("write-under-lock");
    session_store_clear();

    MtProtoSession s;
    make_session(&s, 0xAB);

    ASSERT(session_store_save(&s, 3) == 0, "save under lock succeeds");

    MtProtoSession r;
    mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "load after locked write ok");
    ASSERT(dc == 3, "dc_id correct");
    ASSERT(r.has_auth_key == 1, "auth key present");
    ASSERT(r.server_salt == s.server_salt, "salt matches");

    session_store_clear();
}

/*
 * Test: concurrent_write_conflict
 *
 * Forks a child that holds the exclusive lock on session.bin while the parent
 * tries to save.  The parent's save must fail with -1 (lock busy), not
 * corrupt the file or hang.
 *
 * Mechanism:
 *   1. Parent creates the session file.
 *   2. Parent opens a write-end pipe and forks.
 *   3. Child: open + flock(LOCK_EX) the file, close write-end of pipe
 *      (signals "lock held"), then blocks reading from a second pipe until
 *      signalled to exit.
 *   4. Parent: reads from the pipe (waits until child holds lock), then
 *      tries session_store_save() — should return -1.
 *   5. Parent closes "go" pipe so child exits, waits for child, then verifies
 *      the file is still intact.
 *
 * Skip gracefully on non-POSIX (Windows) where flock is unavailable; on those
 * platforms concurrent writes are not guarded and the test would be vacuous.
 */
static void test_concurrent_write_conflict(void) {
#if defined(_WIN32)
    /* Advisory locking not implemented on Windows; skip. */
    g_tests_run++;
    return;
#else
    with_tmp_home("concurrent-write");
    session_store_clear();

    /* Create an initial valid store that the child can lock. */
    MtProtoSession s0;
    make_session(&s0, 0x10);
    ASSERT(session_store_save(&s0, 1) == 0, "initial write ok");

    /* pipe[0]=read, pipe[1]=write — child closes [1] to signal lock held. */
    int lock_ready[2];   /* child → parent: "I have the lock" */
    int release_lock[2]; /* parent → child: "you may release now" */
    ASSERT(pipe(lock_ready)   == 0, "pipe lock_ready created");
    ASSERT(pipe(release_lock) == 0, "pipe release_lock created");

    const char *home = getenv("HOME");
    ASSERT(home != NULL, "HOME is set");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin", home);

    pid_t child = fork();
    ASSERT(child >= 0, "fork succeeded");

    if (child == 0) {
        /* ---- child ---- */
        /* Close parent-side ends. */
        close(lock_ready[0]);
        close(release_lock[1]);

        /* Acquire exclusive lock. */
        int fd = open(path, O_RDWR, 0600);
        if (fd == -1) { close(lock_ready[1]); close(release_lock[0]); _exit(1); }
        if (flock(fd, LOCK_EX) != 0) {
            close(fd); close(lock_ready[1]); close(release_lock[0]); _exit(2);
        }

        /* Signal parent: lock acquired. */
        close(lock_ready[1]);   /* write-end close → parent read returns EOF */

        /* Block until parent says "done". */
        char rbuf[1];
        ssize_t nr = read(release_lock[0], rbuf, 1);
        (void)nr;

        flock(fd, LOCK_UN);
        close(fd);
        close(release_lock[0]);
        _exit(0);
    }

    /* ---- parent ---- */
    close(lock_ready[1]);
    close(release_lock[0]);

    /* Wait for child to hold the lock. */
    char buf[1];
    ssize_t nr = read(lock_ready[0], buf, 1);
    (void)nr;
    close(lock_ready[0]);

    /* Attempt a write while child holds the lock — must fail. */
    MtProtoSession s1;
    make_session(&s1, 0x20);
    int rc = session_store_save(&s1, 2);
    ASSERT(rc == -1, "save returns -1 when lock is held by another process");

    /* Release child and reap. */
    close(release_lock[1]);
    int status = 0;
    waitpid(child, &status, 0);
    ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child exited cleanly");

    /* The original file must still be readable and intact. */
    MtProtoSession r;
    mtproto_session_init(&r);
    int dc = 0;
    ASSERT(session_store_load(&r, &dc) == 0, "file intact after failed concurrent write");
    ASSERT(dc == 1, "original home DC preserved");
    ASSERT(r.server_salt == s0.server_salt, "original salt intact");

    session_store_clear();
#endif /* _WIN32 */
}

void run_session_store_tests(void) {
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_save_without_key_fails);
    RUN_TEST(test_load_wrong_magic);
    RUN_TEST(test_load_wrong_version);
    RUN_TEST(test_load_truncated_file);
    RUN_TEST(test_null_args);
    RUN_TEST(test_multi_dc_save_load);
    RUN_TEST(test_upsert_in_place);
    RUN_TEST(test_save_dc_on_empty_sets_home);
    RUN_TEST(test_write_under_lock);
    RUN_TEST(test_concurrent_write_conflict);
}
