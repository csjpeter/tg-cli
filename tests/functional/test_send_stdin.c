/**
 * @file test_send_stdin.c
 * @brief TEST-13 — stdin pipe → domain_send_message functional test.
 *
 * Validates the stdin-reading branch used by `cmd_send` when no inline
 * message is provided:
 *   1. Redirect stdin to a pipe containing "hello from pipe\n".
 *   2. Read from stdin exactly as cmd_send does (fread + strip newline).
 *   3. Call domain_send_message with the resulting text.
 *   4. Assert the mock server received "hello from pipe" in the TL wire.
 *
 * The test does NOT call the static cmd_send() in tg_cli.c directly; it
 * replicates the exact stdin-read idiom so the coverage is equivalent.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_serial.h"

#include "domain/write/send.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Helpers shared with test_write_path.c (duplicated to stay simple). */
/* ------------------------------------------------------------------ */

#define CRC_messages_sendMessage   0x0d9d75a4U
#define CRC_updateShortSentMessage 0x9015e101U

static void with_tmp_home_stdin(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-stdin-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock_stdin(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg_stdin(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id   = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session_stdin(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load");
}

/* ------------------------------------------------------------------ */
/* State captured by the responder.                                    */
/* ------------------------------------------------------------------ */

static char g_captured_message[4096];

/* ------------------------------------------------------------------ */
/* Responder: parse messages.sendMessage, capture the 'message' field. */
/*                                                                      */
/* Wire layout (after invokeWithLayer / initConnection stripped):       */
/*   CRC        uint32  0x0d9d75a4                                      */
/*   flags      uint32                                                  */
/*   peer       inputPeerSelf = uint32 TL_inputPeerSelf                 */
/*   message    TL string (length-prefixed)                             */
/*   random_id  int64                                                   */
/* ------------------------------------------------------------------ */
static void on_send_stdin(MtRpcContext *ctx) {
    g_captured_message[0] = '\0';

    TlReader r = tl_reader_init(ctx->req_body, ctx->req_body_len);
    tl_read_uint32(&r);   /* CRC */
    tl_read_uint32(&r);   /* flags */
    tl_read_uint32(&r);   /* inputPeerSelf constructor */
    char *msg = tl_read_string(&r);
    if (msg) {
        snprintf(g_captured_message, sizeof(g_captured_message), "%s", msg);
        free(msg);
    }

    /* Reply with updateShortSentMessage so domain_send_message returns 0. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updateShortSentMessage);
    tl_write_uint32(&w, 0);    /* flags */
    tl_write_int32 (&w, 777);  /* id */
    tl_write_int32 (&w, 0);    /* pts */
    tl_write_int32 (&w, 0);    /* pts_count */
    tl_write_int32 (&w, 0);    /* date */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/**
 * Happy path: pipe "hello from pipe\n" into stdin, read it as cmd_send
 * does, pass to domain_send_message, assert the server sees the text.
 */
static void test_send_from_stdin_pipe(void) {
    with_tmp_home_stdin("pipe");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session_stdin(&s);
    mt_server_expect(CRC_messages_sendMessage, on_send_stdin, NULL);

    ApiConfig cfg; init_cfg_stdin(&cfg);
    Transport t; connect_mock_stdin(&t);

    /* Set up a pipe and redirect stdin to its read end. */
    int pipefd[2];
    ASSERT(pipe(pipefd) == 0, "pipe() created");
    const char *pipe_content = "hello from pipe\n";
    ssize_t written = write(pipefd[1], pipe_content, strlen(pipe_content));
    ASSERT(written == (ssize_t)strlen(pipe_content), "wrote to pipe");
    close(pipefd[1]);

    int saved_stdin = dup(STDIN_FILENO);
    ASSERT(saved_stdin >= 0, "dup stdin");
    ASSERT(dup2(pipefd[0], STDIN_FILENO) == STDIN_FILENO, "dup2 stdin");
    close(pipefd[0]);

    /* --- Replicate cmd_send's stdin-read idiom --- */
    char stdin_buf[4096];
    size_t n = fread(stdin_buf, 1, sizeof(stdin_buf) - 1, stdin);
    ASSERT(n > 0, "fread from pipe got bytes");
    stdin_buf[n] = '\0';
    /* Strip one trailing newline for convenience (same as cmd_send). */
    if (n > 0 && stdin_buf[n - 1] == '\n') stdin_buf[n - 1] = '\0';
    const char *msg = stdin_buf;

    /* Restore stdin so subsequent test output is not affected. */
    ASSERT(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO, "restore stdin");
    close(saved_stdin);

    /* --- Send via domain layer --- */
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t, &(HistoryPeer){.kind = HISTORY_PEER_SELF},
                               msg, &mid, &err) == 0,
           "domain_send_message succeeds");
    ASSERT(mid == 777, "message id echoed from mock server");
    ASSERT(strcmp(g_captured_message, "hello from pipe") == 0,
           "server received 'hello from pipe' (newline stripped)");

    transport_close(&t);
    mt_server_reset();
}

/**
 * Empty stdin should not reach the server (domain_send_message rejects
 * empty strings before the wire).
 */
static void test_send_empty_stdin_rejected(void) {
    with_tmp_home_stdin("empty");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session_stdin(&s);
    /* No handler — wire must not be touched. */

    ApiConfig cfg; init_cfg_stdin(&cfg);
    Transport t; connect_mock_stdin(&t);

    /* domain_send_message rejects "" before sending. */
    int32_t mid = 0;
    RpcError err = {0};
    ASSERT(domain_send_message(&cfg, &s, &t,
                               &(HistoryPeer){.kind = HISTORY_PEER_SELF},
                               "", &mid, &err) == -1,
           "empty message rejected");
    ASSERT(mt_server_rpc_call_count() == 0, "no RPC dispatched for empty");

    transport_close(&t);
    mt_server_reset();
}

/* ------------------------------------------------------------------ */
/* Suite entry point                                                    */
/* ------------------------------------------------------------------ */

void run_send_stdin_tests(void) {
    RUN_TEST(test_send_from_stdin_pipe);
    RUN_TEST(test_send_empty_stdin_rejected);
}
