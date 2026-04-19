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

static void test_updates_difference_messages(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* updates.difference with 2 simple new messages, then empty
     * Vectors for encrypted/other/chats/users, then a state tail. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_difference);

    /* new_messages */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    /* msg 1 */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0); tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 501);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 10LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "alpha");
    /* msg 2 */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0); tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 502);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 10LL);
    tl_write_int32 (&w, 1700000001);
    tl_write_string(&w, "beta");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    UpdatesState in = { .pts=10, .qts=0, .date=1700000000, .seq=1 };
    UpdatesDifference diff = {0};
    int rc = domain_updates_difference(&cfg, &s, &t, &in, &diff);
    ASSERT(rc == 0, "difference with messages parsed");
    ASSERT(diff.new_messages_count == 2, "both messages captured");
    ASSERT(diff.new_messages[0].id == 501, "msg0 id");
    ASSERT(strcmp(diff.new_messages[0].text, "alpha") == 0, "msg0 text");
    ASSERT(diff.new_messages[1].id == 502, "msg1 id");
    ASSERT(strcmp(diff.new_messages[1].text, "beta") == 0, "msg1 text");
}

static void test_updates_null_args(void) {
    ASSERT(domain_updates_state(NULL, NULL, NULL, NULL) == -1, "null rejected");
    ASSERT(domain_updates_difference(NULL, NULL, NULL, NULL, NULL) == -1,
           "null diff rejected");
}

/* ---- FEAT-14: peer_id is extracted from Message.peer_id ---- */
static void test_updates_difference_peer_id(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* Two messages: one from peer 111, one from peer 222. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates_difference);

    /* new_messages */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    /* msg 1: peer_id = peerUser 111 */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0); tl_write_uint32(&w, 0); /* flags, flags2 */
    tl_write_int32 (&w, 601);                         /* id */
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 111LL); /* peer_id */
    tl_write_int32 (&w, 1700000010);                  /* date */
    tl_write_string(&w, "from 111");

    /* msg 2: peer_id = peerUser 222 */
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, 0); tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 602);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 222LL);
    tl_write_int32 (&w, 1700000011);
    tl_write_string(&w, "from 222");

    uint8_t payload[1024]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[2048]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    UpdatesState in = { .pts=10, .qts=0, .date=1700000000, .seq=1 };
    UpdatesDifference diff = {0};
    int rc = domain_updates_difference(&cfg, &s, &t, &in, &diff);
    ASSERT(rc == 0, "peer_id: difference parsed ok");
    ASSERT(diff.new_messages_count == 2, "peer_id: two messages");
    ASSERT(diff.new_messages[0].peer_id == 111LL, "peer_id: msg0 peer_id==111");
    ASSERT(diff.new_messages[1].peer_id == 222LL, "peer_id: msg1 peer_id==222");

    /* Simulate the filter: only allow peer 111. */
    int matched = 0;
    for (int i = 0; i < diff.new_messages_count; i++) {
        if (diff.new_messages[i].peer_id == 111LL) {
            ASSERT(strcmp(diff.new_messages[i].text, "from 111") == 0,
                   "peer_id: msg from 111 has correct text");
            matched++;
        }
    }
    ASSERT(matched == 1, "peer_id: exactly one message from peer 111");
}

void run_domain_updates_tests(void) {
    RUN_TEST(test_updates_state_parse);
    RUN_TEST(test_updates_difference_empty);
    RUN_TEST(test_updates_rpc_error);
    RUN_TEST(test_updates_difference_messages);
    RUN_TEST(test_updates_null_args);
    RUN_TEST(test_updates_difference_peer_id);
}
