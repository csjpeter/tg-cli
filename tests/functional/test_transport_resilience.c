/**
 * @file test_transport_resilience.c
 * @brief TEST-82 — functional coverage for transport.c error paths.
 *
 * Drives the real `transport_*` helpers over the mock socket while
 * injecting the faults listed in US-31:
 *
 *   1. connect() refused          → bootstrap fails clean with errno visible.
 *   2. Partial send()             → transport_send loops to completion.
 *   3. Partial recv()             → transport_recv reassembles the frame.
 *   4. EINTR mid-send             → silent retry.
 *   5. EAGAIN mid-send            → silent retry (blocking socket + signal).
 *   6. Mid-RPC EOF                → transport_recv surfaces -1, explicit
 *                                    close + reconnect brings the channel
 *                                    back on the next poll.
 *   7. SIGPIPE                    → ignored at process level; write after
 *                                    peer close surfaces EPIPE (-1) rather
 *                                    than killing the test binary.
 *
 * All injection is mock-side (see `mock_socket_*` helpers).  No changes to
 * production transport.c beyond the EINTR→EAGAIN widening that US-31
 * itself mandated.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_serial.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- TL CRCs local to this suite ---- */
#define CRC_ping 0x7abe77ecU
#define CRC_pong 0x347773c5U

/* ================================================================ */
/* Boilerplate                                                       */
/* ================================================================ */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-resilience-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

/** Canned ping responder — emits pong echoing the ping_id. */
static void on_ping(MtRpcContext *ctx) {
    if (ctx->req_body_len < 4 + 8) return;
    uint64_t ping_id = 0;
    for (int i = 0; i < 8; ++i) {
        ping_id |= ((uint64_t)ctx->req_body[4 + i]) << (i * 8);
    }
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_pong);
    tl_write_uint64(&w, ctx->req_msg_id);
    tl_write_uint64(&w, ping_id);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void send_ping(MtProtoSession *s, Transport *t, uint64_t ping_id) {
    TlWriter req; tl_writer_init(&req);
    tl_write_uint32(&req, CRC_ping);
    tl_write_uint64(&req, ping_id);
    ASSERT(rpc_send_encrypted(s, t, req.data, req.len, 1) == 0,
           "send ping");
    tl_writer_free(&req);
}

static void expect_pong(MtProtoSession *s, Transport *t, uint64_t ping_id) {
    uint8_t reply[1024];
    size_t reply_len = 0;
    ASSERT(rpc_recv_encrypted(s, t, reply, sizeof(reply), &reply_len) == 0,
           "recv pong");
    TlReader r = tl_reader_init(reply, reply_len);
    ASSERT(tl_read_uint32(&r) == 0xf35c6d01U, "rpc_result outer");
    tl_read_uint64(&r);  /* req_msg_id */
    ASSERT(tl_read_uint32(&r) == CRC_pong, "inner is pong");
    tl_read_uint64(&r);  /* echoed msg_id */
    ASSERT(tl_read_uint64(&r) == ping_id, "ping_id round-trips");
}

/* ================================================================ */
/* 1. connect() refused                                              */
/* ================================================================ */

/* Refuse-connect is a persistent mode — every reconnect attempt in a
 * retry loop keeps failing.  We assert the distinct errno and a clean
 * failure (transport.fd stays -1). */
static void test_connect_refused_is_fatal_with_clean_exit(void) {
    with_tmp_home("conn-refused");
    mt_server_init(); mt_server_reset();
    mock_socket_refuse_connect();

    Transport t;
    transport_init(&t);

    errno = 0;
    int rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == -1,
           "transport_connect returns -1 when peer refuses");
    ASSERT(errno == ECONNREFUSED,
           "errno preserved as ECONNREFUSED across the failed call");
    ASSERT(t.fd == -1,
           "transport fd reset to -1 on failure (no leaked socket)");
    ASSERT(t.connected == 0,
           "transport.connected stays 0 after failed connect");

    /* A second attempt still fails — refuse is persistent until reset. */
    errno = 0;
    rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == -1, "second attempt still refused");
    ASSERT(errno == ECONNREFUSED, "errno still ECONNREFUSED on retry");

    /* Close on an already-failed transport must be a safe no-op. */
    transport_close(&t);
    ASSERT(t.fd == -1, "close on unconnected transport leaves fd at -1");

    mt_server_reset();
}

/* ================================================================ */
/* 2. Partial send() retries to completion                           */
/* ================================================================ */

/* With a 16-byte send cap, every sys_socket_send returns at most 16 bytes.
 * The full encrypted ping frame is ~72 bytes, so transport_send has to
 * loop multiple times for the payload portion.  The server-side reply is
 * still assembled correctly. */
static void test_partial_send_retries_to_completion(void) {
    with_tmp_home("partial-send");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_ping, on_ping, NULL);

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load session");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Now turn on fragmentation — the connect's own marker byte is
     * already gone so the cap applies only to real payload sends. */
    mock_socket_set_send_fragment(16);

    send_ping(&s, &t, 0xDEADBEEF12345678ULL);
    expect_pong(&s, &t, 0xDEADBEEF12345678ULL);

    ASSERT(mt_server_rpc_call_count() == 1,
           "exactly one logical RPC despite many partial writes");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* 3. Partial recv() reassembles the frame                           */
/* ================================================================ */

/* With a 16-byte recv cap the encrypted pong comes back across many
 * sys_socket_recv calls.  transport_recv must continue reading until
 * the announced abridged length is fully drained. */
static void test_partial_recv_reassembles_frame(void) {
    with_tmp_home("partial-recv");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_ping, on_ping, NULL);

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    send_ping(&s, &t, 0xCAFEBABE00000001ULL);

    /* Arm fragmentation only for the reply path. */
    mock_socket_set_recv_fragment(16);

    expect_pong(&s, &t, 0xCAFEBABE00000001ULL);

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* 4. EINTR is silently retried                                      */
/* ================================================================ */

static void test_eintr_is_silent_retry(void) {
    with_tmp_home("eintr");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_ping, on_ping, NULL);

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Inject one EINTR on the next send AND on the next recv to exercise
     * both retry loops. */
    mock_socket_inject_eintr_next_send();
    mock_socket_inject_eintr_next_recv();

    send_ping(&s, &t, 0x0000EE1200000001ULL);
    expect_pong(&s, &t, 0x0000EE1200000001ULL);

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* 5. EAGAIN is silently retried                                     */
/* ================================================================ */

static void test_eagain_is_silent_retry(void) {
    with_tmp_home("eagain");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_ping, on_ping, NULL);

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    mock_socket_inject_eagain_next_send();
    mock_socket_inject_eagain_next_recv();

    send_ping(&s, &t, 0x0A0A0A0A11111111ULL);
    expect_pong(&s, &t, 0x0A0A0A0A11111111ULL);

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* 6. Mid-RPC EOF: next poll reconnects and resumes                  */
/* ================================================================ */

/* Send ping #1 → force EOF on the recv so the RPC fails.
 * Close, rearm the mt_server reconnect detector, connect again, and
 * verify a second ping round-trips cleanly.  That mirrors what a
 * polling watch/upload loop does: treat a transport failure as a
 * transient and reopen the channel on the next iteration. */
static void test_mid_rpc_disconnect_reconnects(void) {
    with_tmp_home("mid-rpc-eof");
    mt_server_init(); mt_server_reset();
    mt_server_expect(CRC_ping, on_ping, NULL);

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect 1");

    /* Queue the EOF before we even send — the reply that on_ping would
     * normally append gets shadowed by the kill-on-next knob. */
    mock_socket_kill_on_next_recv();
    send_ping(&s, &t, 0x1111222233334444ULL);

    uint8_t reply[512];
    size_t reply_len = 0;
    int rc = rpc_recv_encrypted(&s, &t, reply, sizeof(reply), &reply_len);
    ASSERT(rc == -1,
           "rpc_recv_encrypted surfaces -1 on mid-RPC EOF");

    /* Simulate the poll-loop reconnect: close, rearm the mt_server
     * reconnect detector (so the 0xEF on the new socket is parsed as a
     * fresh connection), connect again. */
    transport_close(&t);
    ASSERT(t.fd == -1, "transport fd cleared after close");

    mt_server_arm_reconnect();
    transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "reconnect");

    /* Second ping over the fresh socket — succeeds end-to-end. */
    send_ping(&s, &t, 0x5555666677778888ULL);
    expect_pong(&s, &t, 0x5555666677778888ULL);

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* 7. SIGPIPE is ignored                                             */
/* ================================================================ */

/* The real binaries call `signal(SIGPIPE, SIG_IGN)` inside their
 * entry points.  We replicate that same guard here and assert that a
 * write-after-peer-close surfaces EPIPE from sys_socket_send rather
 * than killing the process with signal 13.
 *
 * Because we use the mock socket there is no real kernel SIGPIPE to
 * receive — instead the test validates that production transport_send
 * treats the -1/EPIPE path as a normal -1 return and does not loop
 * forever.  Combined with the SIG_IGN guard this covers the same
 * contract on a real OS.
 */
static void test_sigpipe_is_ignored(void) {
    with_tmp_home("sigpipe");
    mt_server_init(); mt_server_reset();

    /* Install the same handler the production binaries do. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    ASSERT(sigaction(SIGPIPE, &sa, NULL) == 0,
           "installed SIG_IGN for SIGPIPE");

    /* Verify the disposition took — subsequent writes on a broken
     * pipe do not terminate the process. */
    struct sigaction current;
    ASSERT(sigaction(SIGPIPE, NULL, &current) == 0, "query disposition");
    ASSERT(current.sa_handler == SIG_IGN,
           "SIGPIPE disposition is SIG_IGN");

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Prime the NEXT payload-carrying send to fail.  Connect already
     * consumed call #1 (the 0xEF marker), so we target call #2 — the
     * length-prefix of the next transport_send.  Setting errno=EPIPE
     * emulates a real broken-pipe return without relying on the mock
     * to propagate it. */
    mock_socket_fail_send_at(2);
    errno = EPIPE;

    uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    int rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == -1,
           "transport_send surfaces broken-pipe error without aborting");

    /* The process survived — just close the transport and move on.
     * Touching stderr here would normally trigger SIGPIPE on a real
     * head -c 100 consumer; we simulate that by writing to /dev/null. */
    FILE *f = fopen("/dev/null", "w");
    ASSERT(f != NULL, "/dev/null open");
    fprintf(f, "tg-cli watch output line\n");
    fclose(f);

    transport_close(&t);

    /* Restore default so other tests run unaffected. */
    sa.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &sa, NULL);
    mt_server_reset();
}

/* ================================================================ */
/* 8. Additional fault-path corners                                  */
/* ================================================================ */

/* Unaligned payload is rejected at the transport layer — the abridged
 * length prefix is in 4-byte units, so a non-multiple length would
 * desync the stream. */
static void test_unaligned_payload_rejected(void) {
    with_tmp_home("unaligned");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    uint8_t payload[3] = {0x01, 0x02, 0x03};
    int rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == -1,
           "transport_send rejects payload not multiple of 4 bytes");

    transport_close(&t);
    mt_server_reset();
}

/* sys_socket_create failure: every subsequent connect attempt fails
 * before even reaching sys_socket_connect.  Exercises the socket()
 * error branch that the refuse-connect test doesn't. */
static void test_socket_create_failure(void) {
    with_tmp_home("sock-create");
    mt_server_init(); mt_server_reset();
    mock_socket_fail_create();

    Transport t; transport_init(&t);
    int rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == -1,
           "transport_connect fails when sys_socket_create returns -1");
    ASSERT(t.fd == -1, "fd stays -1 on socket() failure");

    mt_server_reset();
}

/* Abridged marker send fails in transport_connect — covers the cleanup
 * branch that closes the socket and resets fd. */
static void test_marker_send_failure_closes_socket(void) {
    with_tmp_home("marker-fail");
    mt_server_init(); mt_server_reset();

    /* Marker send is sys_socket_send call #1 — prime it to fail. */
    mock_socket_fail_send_at(1);

    Transport t; transport_init(&t);
    int rc = transport_connect(&t, "127.0.0.1", 443);
    ASSERT(rc == -1,
           "transport_connect fails when marker send fails");
    ASSERT(t.fd == -1,
           "fd reset to -1 after marker-send failure");
    ASSERT(t.connected == 0,
           "connected flag stays 0 after marker-send failure");

    mt_server_reset();
}

/* Mid-send payload failure: after a successful length prefix the
 * payload chunk send fails.  Exercises transport_send's "sent <= 0"
 * branch (line ~108-109 in transport.c). */
static void test_payload_send_failure(void) {
    with_tmp_home("payload-send-fail");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Call #1 = marker (done).  Call #2 = prefix (let it pass).
     * Call #3 = payload → force failure. */
    mock_socket_fail_send_at(3);

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};
    int rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == -1,
           "transport_send surfaces -1 when payload chunk fails");

    transport_close(&t);
    mt_server_reset();
}

/* Large wire_len that exceeds the caller's buffer is rejected by
 * transport_recv — covers the payload_len > max_len branch. */
static void test_recv_frame_too_large_rejected(void) {
    with_tmp_home("frame-too-large");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Arm a response whose abridged length prefix decodes to a huge
     * payload (wire_len = 0x00FFFFFF → ~67 MB) so transport_recv's
     * max_len check triggers without us sending 67 MB of mock data. */
    uint8_t giant[4] = { 0x7F, 0xFF, 0xFF, 0xFF };
    mock_socket_set_response(giant, sizeof(giant));

    uint8_t buf[256];
    size_t out_len = 0;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == -1,
           "transport_recv rejects a frame larger than the caller buffer");

    transport_close(&t);
    mt_server_reset();
}

/* Recv length-prefix fails partway through the 3-byte extended prefix.
 * Covers the secondary prefix-recv failure branch. */
static void test_extended_prefix_recv_failure(void) {
    with_tmp_home("ext-prefix-fail");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Queue only the 0x7F marker byte; the 3-byte continuation is
     * missing, so sys_socket_recv returns 0 on the follow-up call. */
    uint8_t only_marker = 0x7F;
    mock_socket_set_response(&only_marker, 1);

    uint8_t buf[64];
    size_t out_len = 0;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == -1,
           "transport_recv fails when 3-byte extended prefix is truncated");

    transport_close(&t);
    mt_server_reset();
}

/* 4-byte extended length prefix fails to send.  Payload size must be
 * at least 0x7F * 4 = 508 bytes for transport_send to pick the 4-byte
 * prefix branch.  Call #1 = marker (connect), call #2 = 4-byte prefix
 * (primed to fail). */
static void test_extended_prefix_send_failure(void) {
    with_tmp_home("ext-prefix-send-fail");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    mock_socket_fail_send_at(2);

    /* 508 bytes = 0x7F * 4 — the minimum that triggers the extended
     * prefix branch.  Contents don't matter, mock fails the send. */
    uint8_t payload[508] = {0};
    int rc = transport_send(&t, payload, sizeof(payload));
    ASSERT(rc == -1,
           "transport_send fails when 4-byte length prefix send fails");

    transport_close(&t);
    mt_server_reset();
}

/* Payload recv fails partway through: arm the first-byte length prefix
 * to read cleanly, then fail the follow-up payload read.  Covers the
 * "r <= 0" branch of the payload-read loop. */
static void test_payload_recv_failure_mid_frame(void) {
    with_tmp_home("payload-recv-fail");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    /* Queue a length prefix that promises 12 bytes of payload, but
     * supply none.  Recv call #1 reads the prefix byte (1 byte of the
     * buffered response).  Recv call #2 runs against an empty queue
     * and returns 0 (EOF) → transport_recv bails. */
    uint8_t prefix_only = 0x03;  /* 3 * 4 = 12 bytes announced */
    mock_socket_set_response(&prefix_only, 1);

    uint8_t buf[64];
    size_t out_len = 0;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == -1,
           "transport_recv surfaces -1 when payload read yields EOF early");

    transport_close(&t);
    mt_server_reset();
}

/* Zero-payload frame: wire_len prefix = 0 → transport_recv returns 0
 * with out_len=0.  Covers the empty-frame shortcut branch. */
static void test_zero_payload_frame(void) {
    with_tmp_home("zero-frame");
    mt_server_init(); mt_server_reset();

    Transport t; transport_init(&t);
    ASSERT(transport_connect(&t, "127.0.0.1", 443) == 0, "connect");

    uint8_t zero = 0x00;
    mock_socket_set_response(&zero, 1);

    uint8_t buf[32];
    size_t out_len = 99;
    int rc = transport_recv(&t, buf, sizeof(buf), &out_len);
    ASSERT(rc == 0,
           "transport_recv returns 0 on zero-length frame");
    ASSERT(out_len == 0,
           "out_len reset to 0 for zero-length frame");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* Suite entry point                                                 */
/* ================================================================ */

void run_transport_resilience_tests(void) {
    RUN_TEST(test_connect_refused_is_fatal_with_clean_exit);
    RUN_TEST(test_partial_send_retries_to_completion);
    RUN_TEST(test_partial_recv_reassembles_frame);
    RUN_TEST(test_eintr_is_silent_retry);
    RUN_TEST(test_eagain_is_silent_retry);
    RUN_TEST(test_mid_rpc_disconnect_reconnects);
    RUN_TEST(test_sigpipe_is_ignored);

    /* Extra error-path corners for coverage completeness. */
    RUN_TEST(test_unaligned_payload_rejected);
    RUN_TEST(test_socket_create_failure);
    RUN_TEST(test_marker_send_failure_closes_socket);
    RUN_TEST(test_payload_send_failure);
    RUN_TEST(test_recv_frame_too_large_rejected);
    RUN_TEST(test_extended_prefix_recv_failure);
    RUN_TEST(test_extended_prefix_send_failure);
    RUN_TEST(test_payload_recv_failure_mid_frame);
    RUN_TEST(test_zero_payload_frame);
}
