/**
 * @file test_domain_search.c
 * @brief Unit tests for domain_search_peer / domain_search_global.
 */

#include "test_helpers.h"
#include "domain/read/search.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t zeros24[24] = {0}; tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);
    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0}; tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    out[0] = (uint8_t)(w.len / 4);
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
    tl_writer_free(&w);
}

static void fix_session(MtProtoSession *s) {
    mtproto_session_init(s);
    uint8_t k[256] = {0}; mtproto_session_set_auth_key(s, k);
    mtproto_session_set_salt(s, 0x1112131415161718ULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

static size_t make_messages_response(uint8_t *buf, size_t max, int32_t id) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, id);
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n); tl_writer_free(&w);
    return n;
}

static void test_search_global(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_messages_response(payload, sizeof(payload), 55);
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_search_global(&cfg, &s, &t, "hello", 5, e, &n);
    ASSERT(rc == 0, "global search parsed");
    ASSERT(n == 1, "one entry");
    ASSERT(e[0].id == 55, "id matches");
}

static void test_search_peer(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_messages_response(payload, sizeof(payload), 77);
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    HistoryEntry e[5] = {0}; int n = 0;
    int rc = domain_search_peer(&cfg, &s, &t, &peer, "needle", 5, e, &n);
    ASSERT(rc == 0, "per-peer search parsed");
    ASSERT(e[0].id == 77, "id matches");
}

static void test_search_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "QUERY_TOO_SHORT");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_search_global(&cfg, &s, &t, "q", 3, e, &n);
    ASSERT(rc != 0, "RPC error propagates");
}

static void test_search_null_args(void) {
    HistoryEntry e[1]; int n = 0;
    ASSERT(domain_search_global(NULL, NULL, NULL, "x", 3, e, &n) == -1, "global nulls");
    ASSERT(domain_search_peer(NULL, NULL, NULL, NULL, "x", 3, e, &n) == -1, "peer nulls");
}

void run_domain_search_tests(void) {
    RUN_TEST(test_search_global);
    RUN_TEST(test_search_peer);
    RUN_TEST(test_search_rpc_error);
    RUN_TEST(test_search_null_args);
}
