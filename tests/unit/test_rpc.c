/**
 * @file test_rpc.c
 * @brief Unit tests for MTProto RPC framework.
 *
 * Uses mock socket + mock crypto to verify message framing.
 */

#include "test_helpers.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "tl_serial.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "transport.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void test_rpc_send_unencrypted_framing(void) {
    mock_socket_reset();
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    Transport t;
    transport_init(&t);
    transport_connect(&t, "localhost", 443);

    /* Send a 4-byte payload */
    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    int rc = rpc_send_unencrypted(&s, &t, payload, 4);
    ASSERT(rc == 0, "send_unencrypted should succeed");

    /* Verify sent data */
    size_t sent_len = 0;
    (void)mock_socket_get_sent(&sent_len);
    /* sent_len includes the 0xEF abridged marker (1) + abridged prefix + payload */
    /* The abridged encoding wraps the RPC frame */
    ASSERT(sent_len > 20, "should have sent more than 20 bytes");

    transport_close(&t);
}

void test_rpc_recv_unencrypted(void) {
    mock_socket_reset();
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    Transport t;
    transport_init(&t);
    transport_connect(&t, "localhost", 443);

    /* Build a response: auth_key_id(8)=0 + msg_id(8)=99 + len(4)=4 + data(4) */
    uint8_t response[24];
    memset(response, 0, sizeof(response));
    /* auth_key_id = 0 (bytes 0-7) */
    /* msg_id = 99 at byte 8 */
    uint64_t msg_id = 99;
    memcpy(response + 8, &msg_id, 8);
    /* len = 4 at byte 16 */
    uint32_t data_len = 4;
    memcpy(response + 16, &data_len, 4);
    /* data at byte 20 */
    response[20] = 0xAA;
    response[21] = 0xBB;
    response[22] = 0xCC;
    response[23] = 0xDD;

    /* Abridged encode: length in 4-byte units = 24/4 = 6, fits in 1 byte */
    uint8_t wire[25];
    wire[0] = 6; /* abridged length prefix */
    memcpy(wire + 1, response, 24);
    mock_socket_set_response(wire, 25);

    /* Clear sent data (abridged marker) */
    mock_socket_clear_sent();

    /* Now receive */
    uint8_t out[64];
    size_t out_len = 0;
    int rc = rpc_recv_unencrypted(&s, &t, out, sizeof(out), &out_len);
    ASSERT(rc == 0, "recv_unencrypted should succeed");
    ASSERT(out_len == 4, "payload length should be 4");
    ASSERT(out[0] == 0xAA, "payload byte 0");
    ASSERT(out[1] == 0xBB, "payload byte 1");

    transport_close(&t);
}

void test_rpc_send_unencrypted_null_checks(void) {
    MtProtoSession s;
    mtproto_session_init(&s);
    uint8_t data[4] = {0};

    ASSERT(rpc_send_unencrypted(NULL, NULL, data, 4) == -1,
           "NULL session should fail");
    ASSERT(rpc_send_unencrypted(&s, NULL, data, 4) == -1,
           "NULL transport should fail");
    ASSERT(rpc_send_unencrypted(&s, (Transport*)(intptr_t)1, NULL, 4) == -1,
           "NULL data should fail");
}

void test_rpc_recv_unencrypted_short_packet(void) {
    mock_socket_reset();
    mock_crypto_reset();
    MtProtoSession s;
    mtproto_session_init(&s);

    Transport t;
    transport_init(&t);
    transport_connect(&t, "localhost", 443);
    mock_socket_clear_sent();

    /* Too-short packet (10 bytes) */
    uint8_t wire[11];
    wire[0] = 3; /* abridged: 3*4=12 bytes, but we only provide 10 */
    uint8_t short_data[10] = {0};
    memcpy(wire + 1, short_data, 10);
    mock_socket_set_response(wire, 11);

    uint8_t out[64];
    size_t out_len = 0;
    int rc = rpc_recv_unencrypted(&s, &t, out, sizeof(out), &out_len);
    ASSERT(rc == -1, "short packet should fail");

    transport_close(&t);
}

/* ---- msg_container tests ---- */

void test_container_not_container(void) {
    /* Non-container data → single message */
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    RpcContainerMsg msgs[4];
    size_t count = 0;

    int rc = rpc_parse_container(data, sizeof(data), msgs, 4, &count);
    ASSERT(rc == 0, "non-container should succeed");
    ASSERT(count == 1, "count should be 1");
    ASSERT(msgs[0].body_len == sizeof(data), "body_len should match");
    ASSERT(msgs[0].body == data, "body should point to original data");
}

void test_container_single_msg(void) {
    /* Container with 1 message */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x73f1f8dc); /* msg_container */
    tl_write_uint32(&w, 1);          /* count = 1 */
    /* msg: msg_id(8) + seqno(4) + body_len(4) + body */
    tl_write_uint64(&w, 12345);      /* msg_id */
    tl_write_uint32(&w, 1);          /* seqno */
    tl_write_uint32(&w, 4);          /* body_len */
    uint8_t body[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    tl_write_raw(&w, body, 4);

    RpcContainerMsg msgs[4];
    size_t count = 0;
    int rc = rpc_parse_container(w.data, w.len, msgs, 4, &count);
    ASSERT(rc == 0, "single-msg container should succeed");
    ASSERT(count == 1, "count should be 1");
    ASSERT(msgs[0].msg_id == 12345, "msg_id should be 12345");
    ASSERT(msgs[0].seqno == 1, "seqno should be 1");
    ASSERT(msgs[0].body_len == 4, "body_len should be 4");
    ASSERT(memcmp(msgs[0].body, body, 4) == 0, "body should match");

    tl_writer_free(&w);
}

void test_container_multiple_msgs(void) {
    /* Container with 3 messages */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x73f1f8dc);
    tl_write_uint32(&w, 3);

    for (int i = 0; i < 3; i++) {
        tl_write_uint64(&w, (uint64_t)(100 + i)); /* msg_id */
        tl_write_uint32(&w, (uint32_t)(i * 2));    /* seqno */
        tl_write_uint32(&w, 4);                    /* body_len */
        uint8_t body[4] = { (uint8_t)i, 0, 0, 0 };
        tl_write_raw(&w, body, 4);
    }

    RpcContainerMsg msgs[8];
    size_t count = 0;
    int rc = rpc_parse_container(w.data, w.len, msgs, 8, &count);
    ASSERT(rc == 0, "multi-msg container should succeed");
    ASSERT(count == 3, "count should be 3");
    ASSERT(msgs[0].msg_id == 100, "msg 0 id");
    ASSERT(msgs[1].msg_id == 101, "msg 1 id");
    ASSERT(msgs[2].msg_id == 102, "msg 2 id");
    ASSERT(msgs[0].body[0] == 0, "msg 0 body");
    ASSERT(msgs[1].body[0] == 1, "msg 1 body");
    ASSERT(msgs[2].body[0] == 2, "msg 2 body");

    tl_writer_free(&w);
}

void test_container_too_many_msgs(void) {
    /* Container with more messages than buffer can hold */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x73f1f8dc);
    tl_write_uint32(&w, 5); /* 5 messages */
    for (int i = 0; i < 5; i++) {
        tl_write_uint64(&w, (uint64_t)i);
        tl_write_uint32(&w, 0);
        tl_write_uint32(&w, 4);
        uint8_t body[4] = {0};
        tl_write_raw(&w, body, 4);
    }

    RpcContainerMsg msgs[2]; /* only room for 2 */
    size_t count = 0;
    int rc = rpc_parse_container(w.data, w.len, msgs, 2, &count);
    ASSERT(rc == -1, "should fail when too many messages for buffer");
    tl_writer_free(&w);
}

void test_container_null_args(void) {
    uint8_t data[8] = {0};
    RpcContainerMsg msgs[2];
    size_t count = 0;
    ASSERT(rpc_parse_container(NULL, 8, msgs, 2, &count) == -1, "NULL data");
    ASSERT(rpc_parse_container(data, 8, NULL, 2, &count) == -1, "NULL msgs");
    ASSERT(rpc_parse_container(data, 8, msgs, 2, NULL) == -1, "NULL count");
}

void test_container_unaligned_body_len(void) {
    /* Regression: QA-20 — a container with body_len that is not a multiple
     * of 4 must be rejected to prevent silent misalignment of subsequent
     * message reads. */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x73f1f8dc); /* msg_container */
    tl_write_uint32(&w, 1);          /* count = 1 */
    tl_write_uint64(&w, 12345);      /* msg_id */
    tl_write_uint32(&w, 1);          /* seqno */
    tl_write_uint32(&w, 3);          /* body_len = 3 (odd, not 4-aligned) */
    uint8_t body[4] = { 0xAA, 0xBB, 0xCC, 0x00 };
    tl_write_raw(&w, body, 4);

    RpcContainerMsg msgs[4];
    size_t count = 0;
    int rc = rpc_parse_container(w.data, w.len, msgs, 4, &count);
    ASSERT(rc == -1, "container with unaligned body_len must be rejected");

    tl_writer_free(&w);
}

/* ---- rpc_result / rpc_error tests ---- */

void test_rpc_unwrap_result(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xf35c6d01); /* rpc_result */
    tl_write_uint64(&w, 99887766ULL); /* req_msg_id */
    tl_write_uint32(&w, 0xDEADBEEF); /* inner data (some constructor) */
    tl_write_int32(&w, 42);

    uint64_t req_id = 0;
    const uint8_t *inner = NULL;
    size_t inner_len = 0;
    int rc = rpc_unwrap_result(w.data, w.len, &req_id, &inner, &inner_len);
    ASSERT(rc == 0, "unwrap rpc_result should succeed");
    ASSERT(req_id == 99887766ULL, "req_msg_id should match");
    ASSERT(inner_len == 8, "inner should be 8 bytes (constructor + int32)");

    uint32_t inner_crc;
    memcpy(&inner_crc, inner, 4);
    ASSERT(inner_crc == 0xDEADBEEF, "inner constructor should match");

    tl_writer_free(&w);
}

void test_rpc_unwrap_result_not_result(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x12345678); /* not rpc_result */
    tl_write_int32(&w, 42);

    uint64_t req_id;
    const uint8_t *inner;
    size_t inner_len;
    int rc = rpc_unwrap_result(w.data, w.len, &req_id, &inner, &inner_len);
    ASSERT(rc == -1, "non-rpc_result should return -1");
    tl_writer_free(&w);
}

void test_rpc_parse_error_flood_wait(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2144ca19); /* rpc_error */
    tl_write_int32(&w, 420);         /* error_code */
    tl_write_string(&w, "FLOOD_WAIT_30");

    RpcError err;
    int rc = rpc_parse_error(w.data, w.len, &err);
    ASSERT(rc == 0, "parse flood_wait should succeed");
    ASSERT(err.error_code == 420, "error_code should be 420");
    ASSERT(strcmp(err.error_msg, "FLOOD_WAIT_30") == 0, "error_msg should match");
    ASSERT(err.flood_wait_secs == 30, "flood_wait should be 30 seconds");
    ASSERT(err.migrate_dc == -1, "no migration");

    tl_writer_free(&w);
}

void test_rpc_parse_error_phone_migrate(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2144ca19);
    tl_write_int32(&w, 303);
    tl_write_string(&w, "PHONE_MIGRATE_4");

    RpcError err;
    int rc = rpc_parse_error(w.data, w.len, &err);
    ASSERT(rc == 0, "parse phone_migrate should succeed");
    ASSERT(err.error_code == 303, "error_code should be 303");
    ASSERT(err.migrate_dc == 4, "should migrate to DC 4");
    ASSERT(err.flood_wait_secs == 0, "no flood wait");

    tl_writer_free(&w);
}

void test_rpc_parse_error_file_migrate(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2144ca19);
    tl_write_int32(&w, 303);
    tl_write_string(&w, "FILE_MIGRATE_2");

    RpcError err;
    int rc = rpc_parse_error(w.data, w.len, &err);
    ASSERT(rc == 0, "parse file_migrate should succeed");
    ASSERT(err.migrate_dc == 2, "should migrate to DC 2");

    tl_writer_free(&w);
}

void test_rpc_parse_error_session_password(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2144ca19);
    tl_write_int32(&w, 401);
    tl_write_string(&w, "SESSION_PASSWORD_NEEDED");

    RpcError err;
    int rc = rpc_parse_error(w.data, w.len, &err);
    ASSERT(rc == 0, "parse session_password should succeed");
    ASSERT(err.error_code == 401, "error_code should be 401");
    ASSERT(strcmp(err.error_msg, "SESSION_PASSWORD_NEEDED") == 0, "msg");
    ASSERT(err.migrate_dc == -1, "no migration");
    ASSERT(err.flood_wait_secs == 0, "no flood wait");

    tl_writer_free(&w);
}

void test_rpc_parse_error_not_error(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x12345678); /* not rpc_error */
    tl_write_int32(&w, 200);

    RpcError err;
    int rc = rpc_parse_error(w.data, w.len, &err);
    ASSERT(rc == -1, "non-rpc_error should return -1");

    tl_writer_free(&w);
}

void test_rpc_parse_error_null_args(void) {
    uint8_t data[16] = {0};
    RpcError err;
    ASSERT(rpc_parse_error(NULL, 16, &err) == -1, "NULL data");
    ASSERT(rpc_parse_error(data, 16, NULL) == -1, "NULL err");
}

/* ---- rpc_recv_encrypted validation tests ---- */

/**
 * Build a minimal valid encrypted wire frame and put it in the mock socket.
 *
 * With mock crypto:
 *  - SHA256 always returns 32 zero bytes, so auth_key_id = 0 and msg_key = 0.
 *  - AES decrypt is an identity transform, so decrypted == ciphertext.
 *  - msg_key verification inside mtproto_decrypt passes when msg_key == 0.
 *
 * The plaintext layout:
 *   salt(8) | session_id(8) | msg_id(8) | seq_no(4) | data_len(4) | data(4) | pad(16)
 *   = 52 bytes total → round to 64 (multiple of 16 for AES).
 *
 * @param session_id_override  Value to write into the session_id field of the frame.
 * @param auth_key_id_override Value to write into the outer auth_key_id field.
 * @param local_session_id     Session's actual session_id.
 */
static void build_encrypted_frame(uint64_t auth_key_id_override,
                                  uint64_t session_id_in_frame,
                                  Transport *t) {
    /* Plaintext: must be multiple of 16. We use 64 bytes. */
    uint8_t plain[64];
    memset(plain, 0, sizeof(plain));

    uint64_t salt = 0;
    memcpy(plain + 0,  &salt,                 8); /* salt */
    memcpy(plain + 8,  &session_id_in_frame,  8); /* session_id */
    /* msg_id, seq_no, data_len, data, padding stay zero */

    /* With mock crypto, AES-IGE decrypt is identity: cipher == plain. */
    uint8_t cipher[64];
    memcpy(cipher, plain, 64);

    /* Wire frame: auth_key_id(8) + msg_key(16, zeros) + cipher(64) = 88 bytes */
    uint8_t frame[88];
    memcpy(frame + 0,  &auth_key_id_override, 8);  /* auth_key_id */
    memset(frame + 8,  0, 16);                      /* msg_key = zeros */
    memcpy(frame + 24, cipher, 64);

    /* Abridged encoding: length in 4-byte units = 88/4 = 22 → fits in 1 byte */
    uint8_t wire[89];
    wire[0] = 22;
    memcpy(wire + 1, frame, 88);
    mock_socket_set_response(wire, 89);

    (void)t;
}

void test_recv_encrypted_wrong_auth_key_id(void) {
    mock_socket_reset();
    mock_crypto_reset();

    MtProtoSession s;
    mtproto_session_init(&s);
    s.has_auth_key = 1;
    memset(s.auth_key, 0, 256);
    /* With mock SHA256 = zeros, the expected auth_key_id is 0.
     * We deliberately use a non-zero auth_key_id to trigger rejection. */
    uint64_t wrong_id = 0xDEADBEEFCAFEBABEULL;

    Transport t;
    transport_init(&t);
    transport_connect(&t, "localhost", 443);
    mock_socket_clear_sent();

    build_encrypted_frame(wrong_id, s.session_id, &t);

    uint8_t out[256];
    size_t out_len = 0;
    int rc = rpc_recv_encrypted(&s, &t, out, sizeof(out), &out_len);
    ASSERT(rc == -1, "wrong auth_key_id must be rejected");

    transport_close(&t);
}

void test_recv_encrypted_wrong_session_id(void) {
    mock_socket_reset();
    mock_crypto_reset();

    MtProtoSession s;
    mtproto_session_init(&s);
    s.has_auth_key = 1;
    memset(s.auth_key, 0, 256);
    /* Correct auth_key_id = 0 (mock SHA256 zeros); wrong session_id in frame. */
    uint64_t correct_auth_key_id = 0ULL;
    uint64_t wrong_session_id    = s.session_id ^ 0xFFFFFFFFFFFFFFFFULL;

    Transport t;
    transport_init(&t);
    transport_connect(&t, "localhost", 443);
    mock_socket_clear_sent();

    build_encrypted_frame(correct_auth_key_id, wrong_session_id, &t);

    uint8_t out[256];
    size_t out_len = 0;
    int rc = rpc_recv_encrypted(&s, &t, out, sizeof(out), &out_len);
    ASSERT(rc == -1, "wrong session_id must be rejected");

    transport_close(&t);
}

void test_rpc(void) {
    RUN_TEST(test_rpc_send_unencrypted_framing);
    RUN_TEST(test_rpc_recv_unencrypted);
    RUN_TEST(test_rpc_send_unencrypted_null_checks);
    RUN_TEST(test_rpc_recv_unencrypted_short_packet);

    /* msg_container */
    RUN_TEST(test_container_not_container);
    RUN_TEST(test_container_single_msg);
    RUN_TEST(test_container_multiple_msgs);
    RUN_TEST(test_container_too_many_msgs);
    RUN_TEST(test_container_null_args);
    RUN_TEST(test_container_unaligned_body_len);

    /* rpc_recv_encrypted validation */
    RUN_TEST(test_recv_encrypted_wrong_auth_key_id);
    RUN_TEST(test_recv_encrypted_wrong_session_id);

    /* rpc_result / rpc_error */
    RUN_TEST(test_rpc_unwrap_result);
    RUN_TEST(test_rpc_unwrap_result_not_result);
    RUN_TEST(test_rpc_parse_error_flood_wait);
    RUN_TEST(test_rpc_parse_error_phone_migrate);
    RUN_TEST(test_rpc_parse_error_file_migrate);
    RUN_TEST(test_rpc_parse_error_session_password);
    RUN_TEST(test_rpc_parse_error_not_error);
    RUN_TEST(test_rpc_parse_error_null_args);
}
