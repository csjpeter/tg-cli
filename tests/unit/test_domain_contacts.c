/**
 * @file test_domain_contacts.c
 */

#include "test_helpers.h"
#include "domain/read/contacts.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

#define CRC_contact 0x145ade0bu

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t z24[24] = {0}; tl_write_raw(&w, z24, 24);
    uint8_t hdr[32] = {0};
    uint32_t pl32 = (uint32_t)plen;
    memcpy(hdr + 28, &pl32, 4);
    tl_write_raw(&w, hdr, 32);
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
    mtproto_session_set_salt(s, 0xCAFE11223344DEAD);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

static void test_contacts_simple(void) {
    mock_socket_reset(); mock_crypto_reset();

    /* contacts.contacts + 3 contacts + saved_count + empty users vector. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_contacts_contacts);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 3);
    tl_write_uint32(&w, CRC_contact); tl_write_int64(&w, 100LL); tl_write_bool(&w, 1);
    tl_write_uint32(&w, CRC_contact); tl_write_int64(&w, 200LL); tl_write_bool(&w, 0);
    tl_write_uint32(&w, CRC_contact); tl_write_int64(&w, 300LL); tl_write_bool(&w, 1);
    tl_write_int32 (&w, 3);          /* saved_count */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    uint8_t payload[512]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    ContactEntry entries[10] = {0};
    int count = 0;
    int rc = domain_get_contacts(&cfg, &s, &t, entries, 10, &count);
    ASSERT(rc == 0, "contacts parsed");
    ASSERT(count == 3, "all 3 contacts");
    ASSERT(entries[0].user_id == 100 && entries[0].mutual == 1, "c0");
    ASSERT(entries[1].user_id == 200 && entries[1].mutual == 0, "c1");
    ASSERT(entries[2].user_id == 300 && entries[2].mutual == 1, "c2");
}

static void test_contacts_rpc_error(void) {
    mock_socket_reset(); mock_crypto_reset();
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 401);
    tl_write_string(&w, "AUTH_KEY_INVALID");
    uint8_t payload[64]; memcpy(payload, w.data, w.len);
    size_t plen = w.len; tl_writer_free(&w);

    uint8_t resp[256]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    ContactEntry e[5] = {0}; int n = 0;
    int rc = domain_get_contacts(&cfg, &s, &t, e, 5, &n);
    ASSERT(rc != 0, "RPC error propagates");
}

static void test_contacts_null_args(void) {
    ContactEntry e[1]; int n = 0;
    ASSERT(domain_get_contacts(NULL, NULL, NULL, e, 5, &n) == -1, "nulls");
    ApiConfig cfg; fix_cfg(&cfg);
    MtProtoSession s; fix_session(&s);
    Transport t; fix_transport(&t);
    ASSERT(domain_get_contacts(&cfg, &s, &t, e, 0, &n) == -1, "zero max");
}

void run_domain_contacts_tests(void) {
    RUN_TEST(test_contacts_simple);
    RUN_TEST(test_contacts_rpc_error);
    RUN_TEST(test_contacts_null_args);
}
