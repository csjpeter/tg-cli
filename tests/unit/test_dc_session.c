/**
 * @file test_dc_session.c
 * @brief Unit tests for app/dc_session.
 *
 * Fast-path coverage only: when session_store has a cached auth_key for
 * the target DC, dc_session_open() must skip the DH handshake and just
 * open the TCP transport. The slow path (real DH exchange) is covered
 * by the integration path in auth_flow tests.
 */

#include "test_helpers.h"
#include "app/dc_session.h"
#include "app/session_store.h"
#include "mock_socket.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void with_tmp_home(const char *subdir) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-dc-session-test-%s", subdir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(path);
    setenv("HOME", tmp, 1);
}

static void seed_cached_session(int dc_id, uint64_t salt,
                                const uint8_t key[256]) {
    MtProtoSession s;
    mtproto_session_init(&s);
    mtproto_session_set_auth_key(&s, key);
    mtproto_session_set_salt(&s, salt);
    s.session_id = 0xCCCCCCCCCCCCCCCCULL;
    (void)session_store_save_dc(dc_id, &s);
}

static void test_fast_path_reuses_cached_key(void) {
    with_tmp_home("fastpath");
    mock_socket_reset();

    uint8_t key[256];
    for (int i = 0; i < 256; i++) key[i] = (uint8_t)(i * 13 + 7);
    seed_cached_session(2, 0xF00DULL, key);

    DcSession out;
    ASSERT(dc_session_open(2, &out) == 0, "fast-path open succeeds");
    ASSERT(out.dc_id == 2, "dc_id recorded");
    ASSERT(out.transport.dc_id == 2, "transport tagged with dc_id");
    ASSERT(out.session.has_auth_key == 1, "session has auth_key");
    ASSERT(memcmp(out.session.auth_key, key, 256) == 0,
           "reused auth_key matches persisted copy");
    ASSERT(out.session.server_salt == 0xF00DULL, "salt restored");
    ASSERT(out.authorized == 1,
           "cached key is treated as already authorized");
    ASSERT(mock_socket_was_connected() == 1, "transport connected");

    size_t sent_len = 0;
    (void)mock_socket_get_sent(&sent_len);
    ASSERT(sent_len == 1, "fast path sends only the 0xef abridged prefix");

    dc_session_close(&out);
    ASSERT(mock_socket_was_closed() == 1, "close tears down transport");

    session_store_clear();
}

static void test_unknown_dc_fails(void) {
    with_tmp_home("unknown-dc");
    mock_socket_reset();

    DcSession out;
    ASSERT(dc_session_open(99, &out) == -1, "unknown DC rejected");
}

static void test_null_out_rejected(void) {
    ASSERT(dc_session_open(2, NULL) == -1, "null out param rejected");
    dc_session_close(NULL);                     /* must not crash */
}

static void test_cached_but_connect_fails_bails_out(void) {
    with_tmp_home("connect-fail");
    mock_socket_reset();

    uint8_t key[256] = {0};
    seed_cached_session(2, 0xBEEFULL, key);

    mock_socket_fail_connect();

    DcSession out;
    /* Fast path falls back to full handshake; handshake also fails because
     * the mock has no queued server response. So we expect -1 here. */
    ASSERT(dc_session_open(2, &out) == -1,
           "cached DC + connect failure bails (no stray success)");

    session_store_clear();
}

void run_dc_session_tests(void) {
    RUN_TEST(test_fast_path_reuses_cached_key);
    RUN_TEST(test_unknown_dc_fails);
    RUN_TEST(test_null_out_rejected);
    RUN_TEST(test_cached_but_connect_fails_bails_out);
}
