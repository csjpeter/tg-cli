/**
 * @file test_domain_self.c
 * @brief Unit tests for domain_get_self (US-05).
 *
 * Uses mock socket + mock crypto. Builds a fake Vector<User> response and
 * verifies field extraction and error handling.
 */

#include "test_helpers.h"
#include "domain/read/self.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

/* Re-used helper for building an "encrypted" response (passthrough crypto). */
static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    uint8_t zeros24[24] = {0};
    tl_write_raw(&w, zeros24, 24); /* auth_key_id + msg_key */
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4); /* data_len */
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);

    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0};
        tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    size_t units = w.len / 4;
    out[0] = (uint8_t)units;
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
    tl_writer_free(&w);
}

static void session_setup(MtProtoSession *s) {
    mtproto_session_init(s);
    uint8_t key[256] = {0};
    mtproto_session_set_auth_key(s, key);
    mtproto_session_set_salt(s, 0x1122334455667788ULL);
}

static void transport_setup(Transport *t) {
    transport_init(t);
    t->fd = 42;
    t->connected = 1;
    t->dc_id = 1;
}

static void cfg_setup(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeef";
}

/* Build a Vector<User> response with a single user#... entry. */
static size_t make_vector_of_one_user(uint8_t *buf, size_t max,
                                       int64_t id,
                                       const char *first, const char *last,
                                       const char *username,
                                       const char *phone) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);           /* count */
    tl_write_uint32(&w, TL_user);     /* user#... */
    uint32_t flags = 0;
    if (first)    flags |= (1u << 1);
    if (last)     flags |= (1u << 2);
    if (username) flags |= (1u << 3);
    if (phone)    flags |= (1u << 4);
    tl_write_uint32(&w, flags);
    tl_write_int64(&w, id);
    if (first)    tl_write_string(&w, first);
    if (last)     tl_write_string(&w, last);
    if (username) tl_write_string(&w, username);
    if (phone)    tl_write_string(&w, phone);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_self_success(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_vector_of_one_user(payload, sizeof(payload),
                                           987654321LL,
                                           "Alice", "Example",
                                           "alice", "+15551234567");

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    session_setup(&s); transport_setup(&t); cfg_setup(&cfg);

    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    ASSERT(rc == 0, "get_self: must succeed");
    ASSERT(me.id == 987654321LL, "id must be parsed");
    ASSERT(strcmp(me.first_name, "Alice") == 0, "first_name");
    ASSERT(strcmp(me.last_name,  "Example") == 0, "last_name");
    ASSERT(strcmp(me.username,   "alice") == 0, "username");
    ASSERT(strcmp(me.phone,      "+15551234567") == 0, "phone");
}

static void test_self_minimal_user(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    size_t plen = make_vector_of_one_user(payload, sizeof(payload),
                                           42LL, NULL, NULL, NULL, NULL);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    session_setup(&s); transport_setup(&t); cfg_setup(&cfg);

    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    ASSERT(rc == 0, "minimal user must succeed");
    ASSERT(me.id == 42LL, "id parsed");
    ASSERT(me.first_name[0] == '\0', "empty first_name");
    ASSERT(me.username[0]   == '\0', "empty username");
}

static void test_self_rpc_error(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 401);
    tl_write_string(&w, "AUTH_KEY_UNREGISTERED");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    session_setup(&s); transport_setup(&t); cfg_setup(&cfg);

    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    ASSERT(rc != 0, "RPC error must propagate");
}

static void test_self_empty_vector(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[32];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    session_setup(&s); transport_setup(&t); cfg_setup(&cfg);

    SelfInfo me = {0};
    int rc = domain_get_self(&cfg, &s, &t, &me);
    ASSERT(rc != 0, "empty vector must fail");
}

static void test_self_null_args(void) {
    SelfInfo me;
    ASSERT(domain_get_self(NULL, NULL, NULL, &me) == -1, "null cfg");
    ASSERT(domain_get_self(NULL, NULL, NULL, NULL) == -1, "null out");
}

void run_domain_self_tests(void) {
    RUN_TEST(test_self_success);
    RUN_TEST(test_self_minimal_user);
    RUN_TEST(test_self_rpc_error);
    RUN_TEST(test_self_empty_vector);
    RUN_TEST(test_self_null_args);
}
