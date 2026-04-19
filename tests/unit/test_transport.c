/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_transport.c
 * @brief Unit tests for transport EINTR retry behaviour.
 *
 * Verifies that transport_send() and transport_recv() transparently retry
 * when the underlying sys_socket_send / sys_socket_recv call is interrupted
 * by a signal (errno == EINTR).
 */

#include "test_helpers.h"
#include "transport.h"
#include "mock_socket.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Helpers                                                              *
 * ------------------------------------------------------------------ */

/**
 * Build an abridged-framed payload in `out` suitable for
 * mock_socket_set_response().  payload must be 4-byte aligned.
 * Returns total frame length written.
 */
static size_t make_abridged_frame(uint8_t *out, const uint8_t *payload,
                                  size_t payload_len)
{
    size_t wire_len = payload_len / 4;
    size_t off = 0;
    if (wire_len < 0x7F) {
        out[off++] = (uint8_t)wire_len;
    } else {
        out[off++] = 0x7F;
        out[off++] = (uint8_t)(wire_len & 0xFF);
        out[off++] = (uint8_t)((wire_len >> 8) & 0xFF);
        out[off++] = (uint8_t)((wire_len >> 16) & 0xFF);
    }
    memcpy(out + off, payload, payload_len);
    return off + payload_len;
}

/* ------------------------------------------------------------------ *
 * Tests                                                                *
 * ------------------------------------------------------------------ */

/**
 * transport_send() should succeed even when the first sys_socket_send call
 * for the payload returns -1/EINTR.
 *
 * Call sequence inside transport_send (after connect which uses send #1 for
 * the 0xEF abridged marker):
 *   call 1 (abridged marker, during connect): succeeds
 *   call 2 (1-byte length prefix):            succeeds
 *   call 3 (payload):                         EINTR on call 3 → retry → succeeds
 */
static void test_transport_send_eintr_retry(void) {
    mock_socket_reset();

    Transport t;
    transport_init(&t);
    int rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == 0, "connect should succeed");

    /* Prime EINTR on the 3rd sys_socket_send call (payload chunk) */
    mock_socket_eintr_send_at(3);

    uint8_t payload[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == 0, "transport_send should succeed after EINTR retry");

    transport_close(&t);
}

/**
 * transport_send() with EINTR on the length-prefix send (call 2) also retries.
 */
static void test_transport_send_eintr_on_prefix(void) {
    mock_socket_reset();

    Transport t;
    transport_init(&t);
    int rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == 0, "connect should succeed");

    /* EINTR on 2nd call (length prefix) */
    mock_socket_eintr_send_at(2);

    uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == 0, "transport_send should succeed after EINTR on prefix");

    transport_close(&t);
}

/**
 * transport_recv() should succeed even when the first recv for the length
 * prefix byte returns -1/EINTR.
 *
 * recv call sequence:
 *   call 1: length prefix byte — inject EINTR → retry → succeeds
 *   call 2: payload bytes      — succeeds
 */
static void test_transport_recv_eintr_on_length(void) {
    mock_socket_reset();

    Transport t;
    transport_init(&t);
    transport_connect(&t, "127.0.0.1", 443);

    /* Build a canned response frame */
    uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t frame[16];
    size_t frame_len = make_abridged_frame(frame, payload, sizeof(payload));
    mock_socket_set_response(frame, frame_len);

    /* EINTR on 1st recv (length prefix byte) */
    mock_socket_eintr_recv_at(1);

    uint8_t buf[64];
    size_t out_len = 0;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == 0, "transport_recv should succeed after EINTR on length prefix");
    ASSERT(out_len == 4, "should have received 4 payload bytes");
    ASSERT(memcmp(buf, payload, 4) == 0, "payload bytes should match");

    transport_close(&t);
}

/**
 * transport_recv() should succeed when EINTR fires during the payload read.
 *
 * recv call sequence:
 *   call 1: length prefix byte — succeeds
 *   call 2: payload chunk      — inject EINTR → retry → succeeds
 */
static void test_transport_recv_eintr_on_payload(void) {
    mock_socket_reset();

    Transport t;
    transport_init(&t);
    transport_connect(&t, "127.0.0.1", 443);

    uint8_t payload[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t frame[32];
    size_t frame_len = make_abridged_frame(frame, payload, sizeof(payload));
    mock_socket_set_response(frame, frame_len);

    /* EINTR on 2nd recv (payload chunk) */
    mock_socket_eintr_recv_at(2);

    uint8_t buf[64];
    size_t out_len = 0;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == 0, "transport_recv should succeed after EINTR on payload");
    ASSERT(out_len == 8, "should have received 8 payload bytes");
    ASSERT(memcmp(buf, payload, 8) == 0, "payload bytes should match");

    transport_close(&t);
}

/* ------------------------------------------------------------------ *
 * Suite entry point                                                    *
 * ------------------------------------------------------------------ */

void run_transport_eintr_tests(void) {
    RUN_TEST(test_transport_send_eintr_retry);
    RUN_TEST(test_transport_send_eintr_on_prefix);
    RUN_TEST(test_transport_recv_eintr_on_length);
    RUN_TEST(test_transport_recv_eintr_on_payload);
}
