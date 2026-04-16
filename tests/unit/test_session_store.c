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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static void test_null_args(void) {
    ASSERT(session_store_save(NULL, 1) == -1, "null session rejected");
    int dc = 0;
    ASSERT(session_store_load(NULL, &dc) == -1, "null session (load)");
}

void run_session_store_tests(void) {
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_save_without_key_fails);
    RUN_TEST(test_load_wrong_magic);
    RUN_TEST(test_null_args);
}
