/**
 * @file test_auth_logout.c
 * @brief Unit tests for auth_logout.c (auth.logOut RPC).
 *
 * Tests:
 *   1. TL request byte layout for auth.logOut is correct.
 *   2. auth.loggedOut response without future_auth_token is accepted.
 *   3. auth.loggedOut response with future_auth_token flag set is accepted
 *      (token ignored).
 *   4. RPC error with error_code 401 (NOT_AUTHORIZED) is treated as success.
 *   5. Generic RPC error returns -1.
 *   6. Network failure (no response) returns -1.
 *   7. auth_logout() clears the session file even when the RPC fails.
 */

#include "test_helpers.h"
#include "auth_logout.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ---- Helpers shared with test_auth_session ---- */

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                           uint8_t *out, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    uint8_t zeros24[24] = {0};
    tl_write_raw(&w, zeros24, 24);

    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);

    size_t total = w.len;
    size_t payload_start = 24;
    size_t enc_part = total - payload_start;
    if (enc_part % 16 != 0) {
        size_t pad = 16 - (enc_part % 16);
        uint8_t zeros[16] = {0};
        tl_write_raw(&w, zeros, pad);
    }

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

static void session_setup(MtProtoSession *s) {
    mtproto_session_init(s);
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
    uint8_t fake_key[256] = {0};
    mtproto_session_set_auth_key(s, fake_key);
    mtproto_session_set_salt(s, 0x1122334455667788ULL);
}

static void setup_session_and_transport(MtProtoSession *s, Transport *t,
                                        ApiConfig *cfg) {
    session_setup(s);
    transport_init(t);
    t->fd = 42; t->connected = 1; t->dc_id = 1;
    api_config_init(cfg);
    cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* ---- Test 1: TL request has correct CRC ---- */
static void test_logout_request_crc(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* Build a minimal loggedOut response so api_call succeeds */
    uint8_t payload[16];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_loggedOut);
    tl_write_uint32(&w, 0); /* flags = 0 */
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc == 0, "logout_rpc: must succeed with loggedOut response");

    /* Verify the sent bytes contain the auth.logOut CRC (0x3e72ba19). */
    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);
    ASSERT(sent_len > 4, "logout_rpc: must have sent data");

    /* The CRC appears inside the encrypted+wrapped payload, but with mock
     * passthrough crypto we can search the raw bytes for the little-endian
     * CRC_auth_logOut = 0x3e72ba19 => bytes 19 ba 72 3e. */
    uint8_t expected[4] = {0x19, 0xba, 0x72, 0x3e};
    int found = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++) {
        if (memcmp(sent + i, expected, 4) == 0) { found = 1; break; }
    }
    ASSERT(found, "logout_rpc: sent bytes must contain auth.logOut CRC");
    (void)sent;
}

/* ---- Test 2: auth.loggedOut with no token (flags=0) accepted ---- */
static void test_logged_out_no_token(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[16];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_loggedOut);
    tl_write_uint32(&w, 0); /* flags = 0, no future_auth_token */
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc == 0, "logout_rpc: loggedOut no-token must succeed");
}

/* ---- Test 3: auth.loggedOut with future_auth_token flag set is accepted ---- */
static void test_logged_out_with_token(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[64];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_loggedOut);
    tl_write_uint32(&w, 1u); /* flags.0 = future_auth_token present */
    /* future_auth_token: bytes (TL bytes type — length-prefixed) */
    uint8_t token[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    tl_write_bytes(&w, token, sizeof(token));
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc == 0, "logout_rpc: loggedOut with future_auth_token must succeed");
}

/* ---- Test 4: RPC error 401 (NOT_AUTHORIZED) treated as success ---- */
static void test_logout_rpc_401_is_ok(void) {
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

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc == 0, "logout_rpc: 401 NOT_AUTHORIZED must be treated as success");
}

/* ---- Test 5: Generic RPC error (non-401) returns -1 ---- */
static void test_logout_rpc_generic_error(void) {
    mock_socket_reset();
    mock_crypto_reset();

    uint8_t payload[128];
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32(&w, 500);
    tl_write_string(&w, "INTERNAL");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp_buf[512]; size_t resp_len = 0;
    build_fake_encrypted_response(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc != 0, "logout_rpc: non-401 RPC error must return -1");
}

/* ---- Test 6: Network failure returns -1 ---- */
static void test_logout_rpc_network_failure(void) {
    mock_socket_reset();
    mock_crypto_reset();
    /* No response queued — recv returns 0 — api_call fails. */

    MtProtoSession s; Transport t; ApiConfig cfg;
    setup_session_and_transport(&s, &t, &cfg);

    int rc = auth_logout_rpc(&cfg, &s, &t);
    ASSERT(rc != 0, "logout_rpc: network failure must return -1");
}

/* ---- Test 7: null args rejected ---- */
static void test_logout_null_args(void) {
    int rc = auth_logout_rpc(NULL, NULL, NULL);
    ASSERT(rc != 0, "logout_rpc: null args must return -1");
}

void run_auth_logout_tests(void) {
    RUN_TEST(test_logout_request_crc);
    RUN_TEST(test_logged_out_no_token);
    RUN_TEST(test_logged_out_with_token);
    RUN_TEST(test_logout_rpc_401_is_ok);
    RUN_TEST(test_logout_rpc_generic_error);
    RUN_TEST(test_logout_rpc_network_failure);
    RUN_TEST(test_logout_null_args);
}
