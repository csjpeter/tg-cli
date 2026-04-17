/**
 * @file test_auth_transfer.c
 * @brief Unit tests for P10-04 cross-DC authorization transfer.
 */

#include "test_helpers.h"
#include "infrastructure/auth_transfer.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdlib.h>
#include <string.h>

#define CRC_auth_exportedAuthorization_t 0xb434e2b8u
#define CRC_auth_authorization_t         0x2ea2c0d4u

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
    size_t dwords = w.len / 4;
    size_t off = 0;
    if (dwords < 0x7F) { out[0] = (uint8_t)dwords; off = 1; }
    else {
        out[0] = 0x7F;
        out[1] = (uint8_t)(dwords);
        out[2] = (uint8_t)(dwords >> 8);
        out[3] = (uint8_t)(dwords >> 16);
        off = 4;
    }
    memcpy(out + off, w.data, w.len);
    *out_len = off + w.len;
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

static size_t make_exported_auth(uint8_t *buf, size_t max,
                                  int64_t id,
                                  const uint8_t *bytes, size_t blen) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_exportedAuthorization_t);
    tl_write_int64 (&w, id);
    tl_write_bytes (&w, bytes, blen);
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static size_t make_authorization(uint8_t *buf, size_t max) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_authorization_t);
    /* Build a minimal auth.authorization — flags=0, user:userEmpty. */
    tl_write_uint32(&w, 0);                                  /* flags */
    tl_write_uint32(&w, 0xd3bc4b7au);                        /* userEmpty crc */
    tl_write_int64 (&w, 0);                                  /* id */
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static void test_export_parses_id_and_bytes(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t token[32];
    for (size_t i = 0; i < sizeof(token); i++) token[i] = (uint8_t)(i * 7 + 1);

    uint8_t payload[512];
    size_t plen = make_exported_auth(payload, sizeof(payload),
                                      0xFEEDBEEFCAFEDEADLL,
                                      token, sizeof(token));
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    AuthExported out = {0};
    int rc = auth_transfer_export(&cfg, &s, &t, 4, &out, NULL);
    ASSERT(rc == 0, "export succeeds");
    ASSERT(out.id == (int64_t)0xFEEDBEEFCAFEDEADLL, "id parsed");
    ASSERT(out.bytes_len == sizeof(token), "bytes_len parsed");
    ASSERT(memcmp(out.bytes, token, sizeof(token)) == 0, "bytes round-trip");
}

static void test_export_rpc_error_surfaces(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 400);
    tl_write_string(&w, "DC_ID_INVALID");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    AuthExported out = {0};
    RpcError err = {0};
    int rc = auth_transfer_export(&cfg, &s, &t, 99, &out, &err);
    ASSERT(rc == -1, "rpc error propagates");
    ASSERT(err.error_code == 400, "error_code preserved");
    ASSERT(strcmp(err.error_msg, "DC_ID_INVALID") == 0, "error_msg preserved");
}

static void test_import_accepts_authorization(void) {
    mock_socket_reset(); mock_crypto_reset();

    uint8_t payload[512];
    size_t plen = make_authorization(payload, sizeof(payload));
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    AuthExported tok = {0};
    tok.id = 42;
    tok.bytes_len = 4;
    tok.bytes[0] = 1; tok.bytes[1] = 2; tok.bytes[2] = 3; tok.bytes[3] = 4;

    int rc = auth_transfer_import(&cfg, &s, &t, &tok, NULL);
    ASSERT(rc == 0, "import succeeds on auth.authorization");
}

static void test_import_rejects_empty_token(void) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    AuthExported tok = {0};                  /* bytes_len = 0 */
    ASSERT(auth_transfer_import(&cfg, &s, &t, &tok, NULL) == -1,
           "zero-length token rejected");
}

static void test_null_args(void) {
    ASSERT(auth_transfer_export(NULL, NULL, NULL, 2, NULL, NULL) == -1,
           "export rejects nulls");
    ASSERT(auth_transfer_import(NULL, NULL, NULL, NULL, NULL) == -1,
           "import rejects nulls");
}

void run_auth_transfer_tests(void) {
    RUN_TEST(test_export_parses_id_and_bytes);
    RUN_TEST(test_export_rpc_error_surfaces);
    RUN_TEST(test_import_accepts_authorization);
    RUN_TEST(test_import_rejects_empty_token);
    RUN_TEST(test_null_args);
}
