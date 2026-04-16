/**
 * @file test_domain_history.c
 * @brief Unit tests for domain_get_history_self (US-06 v1).
 */

#include "test_helpers.h"
#include "domain/read/history.h"
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
    mtproto_session_set_salt(s, 0xBADCAFEDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* Build messages.messages containing one messageEmpty entry — enough to
 * exercise the parse prefix without wrestling with flag-conditional
 * Message fields. */
static size_t make_one_empty_message(uint8_t *buf, size_t max, int32_t id) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_messages);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                /* vector count */
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);                /* flags = 0 */
    tl_write_int32 (&w, id);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_history_one_empty(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_one_empty_message(payload, sizeof(payload), 1234);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry entries[5] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 5, entries, &n);
    ASSERT(rc == 0, "history: must succeed");
    ASSERT(n == 1, "one entry parsed");
    ASSERT(entries[0].id == 1234, "id matches");
}

static void test_history_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();
    uint8_t payload[128];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "PEER_ID_INVALID");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    HistoryEntry e[3] = {0}; int n = 0;
    int rc = domain_get_history_self(&cfg, &s, &t, 0, 3, e, &n);
    ASSERT(rc != 0, "RPC error must propagate");
}

static void test_history_null_args(void) {
    HistoryEntry e[1]; int n = 0;
    ASSERT(domain_get_history_self(NULL, NULL, NULL, 0, 5, e, &n) == -1,
           "null args rejected");
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 0, e, &n) == -1,
           "limit=0 rejected");
}

void run_domain_history_tests(void) {
    RUN_TEST(test_history_one_empty);
    RUN_TEST(test_history_rpc_error);
    RUN_TEST(test_history_null_args);
}
