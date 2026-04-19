/**
 * @file test_domain_user_info.c
 * @brief Unit tests for domain_resolve_username (US-09 v1).
 */

#include "test_helpers.h"
#include "domain/read/user_info.h"
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
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
    uint8_t k[256] = {0}; mtproto_session_set_auth_key(s, k);
    mtproto_session_set_salt(s, 0xFFEEDDCCBBAA9988ULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* Build a minimal contacts.resolvedPeer response where peer is a channel,
 * chats contains a single channel with access_hash, and users is empty. */
static size_t make_channel_response(uint8_t *buf, size_t max,
                                     int64_t channel_id, int64_t hash) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_resolvedPeer);
    tl_write_uint32(&w, TL_peerChannel);
    tl_write_int64 (&w, channel_id);

    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_channel);
    tl_write_uint32(&w, 1u << 13);       /* flags: access_hash present */
    tl_write_uint32(&w, 0);              /* flags2 */
    tl_write_int64 (&w, channel_id);
    tl_write_int64 (&w, hash);

    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_resolve_channel(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[256];
    size_t plen = make_channel_response(payload, sizeof(payload),
                                          1001234567890LL, 0xABCDEF1234567890LL);
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    ResolvedPeer r = {0};
    int rc = domain_resolve_username(&cfg, &s, &t, "@channel_example", &r);
    ASSERT(rc == 0, "resolve: must succeed");
    ASSERT(r.kind == RESOLVED_KIND_CHANNEL, "kind=channel");
    ASSERT(r.id == 1001234567890LL, "id matches");
    ASSERT(r.have_hash == 1, "access_hash present");
    ASSERT(r.access_hash == (int64_t)0xABCDEF1234567890LL, "access_hash value");
    ASSERT(strcmp(r.username, "channel_example") == 0, "username echoed");
}

static void test_resolve_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "USERNAME_INVALID");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    ResolvedPeer r = {0};
    int rc = domain_resolve_username(&cfg, &s, &t, "@bogus", &r);
    ASSERT(rc != 0, "RPC error must propagate");
}

static void test_resolve_null_args(void) {
    ResolvedPeer r;
    ASSERT(domain_resolve_username(NULL, NULL, NULL, NULL, &r) == -1, "nulls");
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_resolve_username(&cfg, &s, &t, NULL, &r) == -1, "null name");
}

void run_domain_user_info_tests(void) {
    RUN_TEST(test_resolve_channel);
    RUN_TEST(test_resolve_rpc_error);
    RUN_TEST(test_resolve_null_args);
}
