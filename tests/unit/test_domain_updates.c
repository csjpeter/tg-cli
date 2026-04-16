/**
 * @file test_domain_updates.c
 * @brief Unit tests for domain_updates_state / domain_updates_difference.
 */

#include "test_helpers.h"
#include "domain/read/updates.h"
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
    mtproto_session_set_salt(s, 0xAABBCCDDEEFF1122ULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

static void test_updates_state_parse(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_state);
    tl_write_int32(&w, 100);  /* pts */
    tl_write_int32(&w, 0);    /* qts */
    tl_write_int32(&w, 1700000000); /* date */
    tl_write_int32(&w, 7);    /* seq */
    tl_write_int32(&w, 3);    /* unread_count */
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    UpdatesState st = {0};
    int rc = domain_updates_state(&cfg, &s, &t, &st);
    ASSERT(rc == 0, "updates.state parsed");
    ASSERT(st.pts == 100, "pts");
    ASSERT(st.seq == 7, "seq");
    ASSERT(st.unread_count == 3, "unread_count");
}

static void test_updates_difference_empty(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_differenceEmpty);
    tl_write_int32(&w, 1700000100); /* date */
    tl_write_int32(&w, 8);          /* seq */
    uint8_t payload[32]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[128]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    UpdatesState in = { .pts=100, .qts=0, .date=1700000000, .seq=7 };
    UpdatesDifference diff = {0};
    int rc = domain_updates_difference(&cfg, &s, &t, &in, &diff);
    ASSERT(rc == 0, "differenceEmpty parsed");
    ASSERT(diff.is_empty == 1, "is_empty flag");
    ASSERT(diff.next_state.date == 1700000100, "new date propagated");
}

static void test_updates_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 401);
    tl_write_string(&w, "AUTH_KEY_UNREGISTERED");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    UpdatesState st = {0};
    int rc = domain_updates_state(&cfg, &s, &t, &st);
    ASSERT(rc != 0, "RPC error propagates");
}

static void test_updates_null_args(void) {
    ASSERT(domain_updates_state(NULL, NULL, NULL, NULL) == -1, "null rejected");
    ASSERT(domain_updates_difference(NULL, NULL, NULL, NULL, NULL) == -1,
           "null diff rejected");
}

void run_domain_updates_tests(void) {
    RUN_TEST(test_updates_state_parse);
    RUN_TEST(test_updates_difference_empty);
    RUN_TEST(test_updates_rpc_error);
    RUN_TEST(test_updates_null_args);
}
