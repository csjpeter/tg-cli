/**
 * @file test_rpc.c
 * @brief Unit tests for MTProto RPC framework.
 *
 * Uses mock socket + mock crypto to verify message framing.
 */

#include "test_helpers.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
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

void test_rpc(void) {
    RUN_TEST(test_rpc_send_unencrypted_framing);
    RUN_TEST(test_rpc_recv_unencrypted);
    RUN_TEST(test_rpc_send_unencrypted_null_checks);
    RUN_TEST(test_rpc_recv_unencrypted_short_packet);
}
