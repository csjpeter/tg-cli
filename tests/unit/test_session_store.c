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

void run_session_store_tests(void) {
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_save_without_key_fails);
    RUN_TEST(test_load_wrong_magic);
    RUN_TEST(test_null_args);
    RUN_TEST(test_multi_dc_save_load);
    RUN_TEST(test_upsert_in_place);
    RUN_TEST(test_save_dc_on_empty_sets_home);
}
