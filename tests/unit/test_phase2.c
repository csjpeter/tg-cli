/**
 * @file test_phase2.c
 * @brief Unit tests for Phase 2 modules (socket mock, transport, session).
 */

#include "test_helpers.h"
#include "platform/socket.h"
#include "mock_socket.h"
#include "transport.h"
#include "mtproto_session.h"
#include "mock_crypto.h"

#include <stdlib.h>
#include <string.h>

/* ---- Socket mock tests ---- */

void test_mock_socket_create(void) {
    mock_socket_reset();
    int fd = sys_socket_create();
    ASSERT(fd == 42, "mock socket_create should return 42");
    ASSERT(mock_socket_was_created() == 1, "create count should be 1");
}

void test_mock_socket_connect(void) {
    mock_socket_reset();
    sys_socket_create();
    int rc = sys_socket_connect(42, "149.154.175.50", 443);
    ASSERT(rc == 0, "mock connect should succeed");
    ASSERT(mock_socket_was_connected() == 1, "connect count should be 1");
}

void test_mock_socket_send_recv(void) {
    mock_socket_reset();
    sys_socket_create();
    sys_socket_connect(42, "localhost", 443);

    /* Send data */
    const char *msg = "hello";
    ssize_t sent = sys_socket_send(42, msg, 5);
    ASSERT(sent == 5, "send should return 5");

    /* Verify sent data */
    size_t sent_len = 0;
    const uint8_t *sent_data = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len == 5, "sent_len should be 5");
    ASSERT(memcmp(sent_data, "hello", 5) == 0, "sent data should match");

    /* Set response and recv */
    mock_socket_set_response((const uint8_t *)"world", 5);
    uint8_t buf[16];
    ssize_t recvd = sys_socket_recv(42, buf, 16);
    ASSERT(recvd == 5, "recv should return 5");
    ASSERT(memcmp(buf, "world", 5) == 0, "recv data should match");
}

void test_mock_socket_close(void) {
    mock_socket_reset();
    sys_socket_create();
    sys_socket_close(42);
    ASSERT(mock_socket_was_closed() == 1, "close count should be 1");
}

/* ---- Session tests ---- */

void test_session_init(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);
    ASSERT(s.session_id != 0, "session_id should be non-zero");
    ASSERT(s.seq_no == 0, "seq_no should start at 0");
    ASSERT(s.has_auth_key == 0, "should not have auth_key");
}

void test_session_msg_id_monotonic(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    uint64_t id1 = mtproto_session_next_msg_id(&s);
    uint64_t id2 = mtproto_session_next_msg_id(&s);
    ASSERT(id2 > id1, "msg_id should be monotonically increasing");
    ASSERT(id1 % 2 == 0, "msg_id should be even (client→server)");
    ASSERT(id2 % 2 == 0, "msg_id should be even");
}

void test_session_seq_no(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    /* Non-content-related (e.g., ping): seq_no = seq*2 */
    uint32_t s1 = mtproto_session_next_seq_no(&s, 0);
    ASSERT(s1 == 0, "first non-content seq should be 0");

    /* Content-related (RPC call): seq_no = seq*2 + 1, seq++ */
    uint32_t s2 = mtproto_session_next_seq_no(&s, 1);
    ASSERT(s2 == 1, "first content seq should be 1");

    /* Another content-related: seq_no = seq*2 + 1 */
    uint32_t s3 = mtproto_session_next_seq_no(&s, 1);
    ASSERT(s3 == 3, "second content seq should be 3");

    /* Non-content: seq_no = seq*2 */
    uint32_t s4 = mtproto_session_next_seq_no(&s, 0);
    ASSERT(s4 == 4, "non-content after 2 content should be 4");
}

void test_session_auth_key(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    uint8_t key[256];
    memset(key, 0xAB, 256);
    mtproto_session_set_auth_key(&s, key);

    ASSERT(s.has_auth_key == 1, "should have auth_key");
    ASSERT(memcmp(s.auth_key, key, 256) == 0, "auth_key should match");
}

void test_session_salt(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    mtproto_session_set_salt(&s, 0xDEADBEEFCAFEBABEULL);
    ASSERT(s.server_salt == 0xDEADBEEFCAFEBABEULL, "salt should match");
}

void test_session_save_load(void) {
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    uint8_t key[256];
    memset(key, 0x42, 256);
    mtproto_session_set_auth_key(&s, key);

    char tmppath[64];
    snprintf(tmppath, sizeof(tmppath), "/tmp/tg-cli-test-auth-%d", getpid());

    int rc = mtproto_session_save_auth_key(&s, tmppath);
    ASSERT(rc == 0, "save should succeed");

    MtProtoSession s2;
    memset(&s2, 0, sizeof(s2));
    rc = mtproto_session_load_auth_key(&s2, tmppath);
    ASSERT(rc == 0, "load should succeed");
    ASSERT(s2.has_auth_key == 1, "loaded session should have auth_key");
    ASSERT(memcmp(s2.auth_key, key, 256) == 0, "loaded key should match");

    remove(tmppath);
}

void test_session_load_nonexistent(void) {
    MtProtoSession s;
    memset(&s, 0, sizeof(s));
    int rc = mtproto_session_load_auth_key(&s, "/tmp/tg-cli-nonexistent-file-12345");
    ASSERT(rc == -1, "load from nonexistent file should fail");
    ASSERT(s.has_auth_key == 0, "should not have auth_key after failed load");
}

void test_session_load_truncated(void) {
    /* Write only 100 bytes (less than 256) */
    char tmppath[64];
    snprintf(tmppath, sizeof(tmppath), "/tmp/tg-cli-test-trunc-%d", getpid());
    FILE *f = fopen(tmppath, "wb");
    ASSERT(f != NULL, "should create temp file");
    uint8_t short_data[100];
    memset(short_data, 0x42, 100);
    fwrite(short_data, 1, 100, f);
    fclose(f);

    MtProtoSession s;
    memset(&s, 0, sizeof(s));
    int rc = mtproto_session_load_auth_key(&s, tmppath);
    ASSERT(rc == -1, "load from truncated file should fail");
    ASSERT(s.has_auth_key == 0, "should not have auth_key after truncated load");

    remove(tmppath);
}

void test_session_save_invalid_path(void) {
    MtProtoSession s;
    memset(&s, 0, sizeof(s));
    uint8_t key[256];
    memset(key, 0x42, 256);
    mtproto_session_set_auth_key(&s, key);

    int rc = mtproto_session_save_auth_key(&s, "/nonexistent/dir/auth.key");
    ASSERT(rc == -1, "save to invalid path should fail");
}

void test_session_save_null_args(void) {
    MtProtoSession s;
    memset(&s, 0, sizeof(s));

    ASSERT(mtproto_session_save_auth_key(NULL, "/tmp/x") == -1, "NULL session");
    ASSERT(mtproto_session_save_auth_key(&s, NULL) == -1, "NULL path");
    /* No auth_key set */
    ASSERT(mtproto_session_save_auth_key(&s, "/tmp/x") == -1, "no auth_key");

    ASSERT(mtproto_session_load_auth_key(NULL, "/tmp/x") == -1, "load NULL session");
    ASSERT(mtproto_session_load_auth_key(&s, NULL) == -1, "load NULL path");
}

/* ---- Test suite entry point ---- */

void test_phase2(void) {
    RUN_TEST(test_mock_socket_create);
    RUN_TEST(test_mock_socket_connect);
    RUN_TEST(test_mock_socket_send_recv);
    RUN_TEST(test_mock_socket_close);
    RUN_TEST(test_session_init);
    RUN_TEST(test_session_msg_id_monotonic);
    RUN_TEST(test_session_seq_no);
    RUN_TEST(test_session_auth_key);
    RUN_TEST(test_session_salt);
    RUN_TEST(test_session_save_load);
    RUN_TEST(test_session_load_nonexistent);
    RUN_TEST(test_session_load_truncated);
    RUN_TEST(test_session_save_invalid_path);
    RUN_TEST(test_session_save_null_args);
}
