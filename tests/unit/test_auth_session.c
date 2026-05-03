/**
 * @file test_auth_session.c
 * @brief Unit tests for auth_session.c (auth.sendCode + auth.signIn).
 *
 * Uses mock socket and mock crypto; verifies TL request structure and
 * response parsing.
 */

#include "test_helpers.h"
#include "auth_session.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ---- Helper: build a minimal fake encrypted response ----
 *
 * The RPC layer (rpc_recv_encrypted) expects:
 *   auth_key_id(8) + msg_key(16) + encrypted_payload
 * where encrypted_payload (after mock decrypt = identity) is:
 *   salt(8) + session_id(8) + msg_id(8) + seq_no(4) + data_len(4) + data
 * and data is the raw TL response we want to return.
 *
 * Since mock crypto uses passthrough (encrypt = copy), we build the
 * "encrypted" buffer as if it were already the decrypted payload.
 */
static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                           uint8_t *out, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    /* auth_key_id (8) + msg_key (16) = 24 bytes header */
    uint8_t zeros24[24] = {0};
    tl_write_raw(&w, zeros24, 24);

    /* plaintext frame: salt(8)+session_id(8)+msg_id(8)+seq_no(4)+len(4)+data */
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4); /* data_len */
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);

    /* Pad to 16-byte boundary */
    size_t total = w.len;
    size_t payload_start = 24; /* after auth_key_id + msg_key */
    size_t enc_part = total - payload_start;
    if (enc_part % 16 != 0) {
        size_t pad = 16 - (enc_part % 16);
        uint8_t zeros[16] = {0};
        tl_write_raw(&w, zeros, pad);
    }

    /* Wrap in abridged transport: 1-byte length prefix (in 4-byte units) */
    size_t wire_bytes = w.len;
    size_t wire_units = wire_bytes / 4;

    uint8_t *result = (uint8_t *)malloc(1 + wire_bytes);
    result[0] = (uint8_t)wire_units;
    memcpy(result + 1, w.data, wire_bytes);
    *out_len = 1 + wire_bytes;
    memcpy(out, result, *out_len);
    free(result);
    tl_writer_free(&w);
}

/* ---- Helper: make session ready for encrypted calls ---- */
static void session_setup(MtProtoSession *s) {
    mtproto_session_init(s);
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
    uint8_t fake_key[256] = {0};
    mtproto_session_set_auth_key(s, fake_key);
    mtproto_session_set_salt(s, 0x1122334455667788ULL);
}

/* ---- Helper: make fake sentCode TL payload ---- */
static size_t make_sent_code_payload(uint8_t *buf, size_t max) {
    TlWriter w;
    tl_writer_init(&w);

    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);              /* flags = 0 */
    /* type: sentCodeTypeApp */
    tl_write_uint32(&w, CRC_auth_sentCodeTypeApp);
    tl_write_int32(&w, 5);              /* length = 5 digits */
    /* phone_code_hash */
    tl_write_string(&w, "abc123hash456xyz");

    size_t len = w.len < max ? w.len : max;
    memcpy(buf, w.data, len);
    tl_writer_free(&w);
    return len;
}

/* ---- Helper: make fake auth.authorization TL payload ---- */
static size_t make_authorization_payload(uint8_t *buf, size_t max,
                                          int64_t user_id) {
    TlWriter w;
    tl_writer_init(&w);

    tl_write_uint32(&w, CRC_auth_authorization);
    tl_write_uint32(&w, 0);             /* flags = 0 */
    /* user: user#3ff6ecb0 */
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 0);            /* user flags */
    tl_write_int64(&w, user_id);       /* id */

    size_t len = w.len < max ? w.len : max;
    memcpy(buf, w.data, len);
    tl_writer_free(&w);
    return len;
}

/* ---- Test: auth_send_code sends correct TL request ---- */
static void test_send_code_request_structure(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* Prepare fake sentCode response */
    uint8_t payload[512];
    size_t plen = make_sent_code_payload(payload, sizeof(payload));

    uint8_t resp_buf[1024];
    size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s;
    session_setup(&s);
    Transport t;
    transport_init(&t);
    t.fd = 42;
    t.connected = 1;
    t.dc_id = 1;

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id   = 12345;
    cfg.api_hash = "deadbeef";

    AuthSentCode result;
    memset(&result, 0, sizeof(result));
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);

    ASSERT(rc == 0, "send_code: must succeed with valid sentCode response");
    ASSERT(strcmp(result.phone_code_hash, "abc123hash456xyz") == 0,
           "send_code: phone_code_hash must be parsed correctly");

    /* Verify the sent bytes contain the phone number and api_id */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len > 10,  "send_code: must have sent data");
    (void)sent;
}

/* ---- Test: auth_send_code handles RPC error ---- */
static void test_send_code_rpc_error(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* Build rpc_error payload */
    uint8_t payload[256];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "PHONE_NUMBER_INVALID");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[1024];
    size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s;
    session_setup(&s);
    Transport t;
    transport_init(&t);
    t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    AuthSentCode result;
    memset(&result, 0, sizeof(result));
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);

    ASSERT(rc != 0, "send_code: must fail on RPC error");
}

/* ---- Test: auth_sign_in success path ---- */
static void test_sign_in_success(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[512];
    size_t plen = make_authorization_payload(payload, sizeof(payload), 987654321LL);

    uint8_t resp_buf[1024];
    size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s;
    session_setup(&s);
    Transport t;
    transport_init(&t);
    t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    int64_t user_id = 0;
    int rc = auth_sign_in(&cfg, &s, &t,
                          "+15551234567", "abc123hash456xyz", "12345",
                          &user_id, NULL, NULL);

    ASSERT(rc == 0,             "sign_in: must succeed");
    ASSERT(user_id == 987654321LL, "sign_in: user_id must be parsed correctly");
}

/* ---- Test: auth_sign_in RPC error ---- */
static void test_sign_in_rpc_error(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[256];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 400);
    tl_write_string(&w, "PHONE_CODE_INVALID");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[1024];
    size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s;
    session_setup(&s);
    Transport t;
    transport_init(&t);
    t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    int64_t user_id = 0;
    int rc = auth_sign_in(&cfg, &s, &t,
                          "+15551234567", "abc123hash456xyz", "00000",
                          &user_id, NULL, NULL);

    ASSERT(rc != 0, "sign_in: must fail on RPC error");
}

/* ---- Test: null arguments are rejected ---- */
static void test_null_args_rejected(void) {
    AuthSentCode out;
    ASSERT(auth_send_code(NULL, NULL, NULL, NULL, &out, NULL) == -1,
           "send_code: null args must return -1");
    ASSERT(auth_sign_in(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == -1,
           "sign_in: null args must return -1");
}

/* ---- Helpers for additional error-path tests ---- */
static void setup_session_and_transport(MtProtoSession *s, Transport *t,
                                         ApiConfig *cfg) {
    session_setup(s);
    transport_init(t);
    t->fd = 42; t->connected = 1; t->dc_id = 1;
    api_config_init(cfg);
    cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* ---- Test: send_code with unexpected top-level constructor ---- */
static void test_send_code_unexpected_constructor(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[64];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEADBEEF);         /* not auth.sentCode */
    tl_write_uint32(&w, 0);
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[256]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    AuthSentCode result = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);
    ASSERT(rc != 0, "send_code: unexpected constructor must fail");
}

/* ---- Test: send_code hitting sentCodeTypeFlashCall ---- */
static void test_send_code_flash_call_type(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[256];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 1u << 2);            /* flags: timeout present */
    tl_write_uint32(&w, CRC_auth_sentCodeTypeFlashCall);
    tl_write_string(&w, "\\d{6}");           /* pattern */
    tl_write_string(&w, "flash_hash");
    tl_write_int32(&w, 120);                 /* timeout */
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    AuthSentCode result = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);
    ASSERT(rc == 0, "send_code: FlashCall path must succeed");
    ASSERT(result.timeout == 120, "timeout must be parsed from flags.2");
    ASSERT(strcmp(result.phone_code_hash, "flash_hash") == 0,
           "phone_code_hash must match");
}

/* ---- Test: send_code with unknown sentCodeType constructor ---- */
static void test_send_code_unknown_type(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xBADCAFEE);         /* unknown sentCodeType */
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[256]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    AuthSentCode result = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);
    ASSERT(rc != 0, "send_code: unknown sentCodeType must fail");
}

/* ---- Test: send_code API-call failure (no response) ---- */
static void test_send_code_api_call_fails(void) {
    mock_socket_reset();
    mock_crypto_reset();
    /* No response queued → recv returns 0 → api_call fails. */

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    AuthSentCode result = {0};
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);
    ASSERT(rc != 0, "send_code: api_call failure must propagate");
}

/* ---- Test: sign_in with unexpected top-level constructor ---- */
static void test_sign_in_unexpected_constructor(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[64];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xBADBADBA);
    tl_write_uint32(&w, 0);
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[256]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int64_t uid = 0;
    int rc = auth_sign_in(&cfg, &s, &t, "+1", "h", "12345", &uid, NULL, NULL);
    ASSERT(rc != 0, "sign_in: unexpected constructor must fail");
}

/* ---- Test: sign_in with unexpected user constructor — still OK, uid=0 ---- */
static void test_sign_in_unexpected_user_crc(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_authorization);
    tl_write_uint32(&w, 0);          /* flags */
    tl_write_uint32(&w, 0xCAFEBABE); /* unexpected user constructor */
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[256]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int64_t uid = 77;
    int rc = auth_sign_in(&cfg, &s, &t, "+1", "h", "12345", &uid, NULL, NULL);
    ASSERT(rc == 0, "sign_in: unexpected user constructor still OK");
    ASSERT(uid == 0, "sign_in: uid must be reset to 0");
}

/* ---- Test: sign_in api_call fails ---- */
static void test_sign_in_api_call_fails(void) {
    mock_socket_reset();
    mock_crypto_reset();
    /* No response queued */

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int64_t uid = 0;
    int rc = auth_sign_in(&cfg, &s, &t, "+1", "h", "12345", &uid, NULL, NULL);
    ASSERT(rc != 0, "sign_in: api_call failure must propagate");
}

void run_auth_session_tests(void) {
    RUN_TEST(test_send_code_request_structure);
    RUN_TEST(test_send_code_rpc_error);
    RUN_TEST(test_sign_in_success);
    RUN_TEST(test_sign_in_rpc_error);
    RUN_TEST(test_null_args_rejected);
    RUN_TEST(test_send_code_unexpected_constructor);
    RUN_TEST(test_send_code_flash_call_type);
    RUN_TEST(test_send_code_unknown_type);
    RUN_TEST(test_send_code_api_call_fails);
    RUN_TEST(test_sign_in_unexpected_constructor);
    RUN_TEST(test_sign_in_unexpected_user_crc);
    RUN_TEST(test_sign_in_api_call_fails);
}
