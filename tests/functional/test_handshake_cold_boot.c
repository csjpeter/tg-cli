/**
 * @file test_handshake_cold_boot.c
 * @brief TEST-71 / US-20 — functional coverage for the MTProto 2.0 DH
 *        handshake (src/infrastructure/mtproto_auth.c).
 *
 * Drives the production auth_step_* functions against the in-process
 * mock Telegram server with real OpenSSL on both sides. The mock
 * cannot decrypt the client's RSA_PAD-encrypted inner_data (that would
 * require Telegram's RSA private key, not shipped), so these tests
 * cover all paths reachable WITHOUT that private key:
 *
 *   - req_pq_multi → resPQ happy path (auth_step_req_pq)
 *   - resPQ wrong fingerprint, wrong constructor, wrong nonce, bad PQ
 *   - auth_step_req_dh end-to-end (RSA_PAD encrypt, wire send)
 *   - auth_step_parse_dh rejection of a garbage server_DH_params_ok
 *   - mtproto_auth_key_gen orchestrator failure path + no partial
 *     session persistence
 *
 * The fresh-install happy-path (session.bin created) and the
 * dh_gen_retry / dh_gen_fail variants are explicitly out of scope for
 * this test suite because they require the mock to fabricate a valid
 * AES-IGE-wrapped server_DH_inner_data, which in turn requires knowing
 * the client's new_nonce — sealed inside the RSA envelope. Those
 * scenarios remain covered at unit-test granularity in tests/unit/
 * test_auth.c (which uses the mock crypto backend).
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "mtproto_auth.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CRC_req_pq_multi   0xbe7e8ef1U
#define CRC_req_DH_params  0xd712e4beU

/* ---- Helpers ---------------------------------------------------------- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-handshake-%s", tag);
    /* Best-effort cleanup of any previous session.bin from an earlier run. */
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static int session_bin_exists(void) {
    const char *home = getenv("HOME");
    if (!home) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/tg-cli/session.bin", home);
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

static void fresh_mock(const char *tag) {
    with_tmp_home(tag);
    mt_server_init();
    mt_server_reset();
}

static void bring_transport_up(Transport *t, MtProtoSession *s) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "transport_connect");
    mtproto_session_init(s);
}

/* ---- Step 1: req_pq_multi → resPQ ----------------------------------- */

/**
 * Happy-path: with a valid resPQ armed, auth_step_req_pq should
 * succeed, populate ctx.pq / ctx.server_nonce, and the mock should
 * have observed exactly one req_pq_multi frame.
 */
static void test_cold_boot_req_pq_happy_path(void) {
    fresh_mock("req-pq-happy");
    mt_server_simulate_cold_boot(MT_COLD_BOOT_OK);

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;
    ctx.dc_id     = 2;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == 0, "auth_step_req_pq returns 0 on valid resPQ");
    ASSERT(ctx.pq == 21, "ctx.pq matches the 21 the mock emits");
    ASSERT(mt_server_handshake_req_pq_count() == 1,
           "mock observed exactly one req_pq_multi");
    /* server_nonce must have been populated (0xBB fill from the mock). */
    ASSERT(ctx.server_nonce[0] == 0xBB, "ctx.server_nonce bytes from mock");

    transport_close(&t);
}

/**
 * Negative: resPQ lists a fingerprint the client's hardcoded Telegram
 * RSA key does not match. auth_step_req_pq must return -1 without
 * setting pq.
 */
static void test_cold_boot_bad_fingerprint(void) {
    fresh_mock("bad-fp");
    mt_server_simulate_cold_boot(MT_COLD_BOOT_BAD_FINGERPRINT);

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "auth_step_req_pq rejects unknown fingerprint");
    ASSERT(mt_server_handshake_req_pq_count() == 1,
           "client still sent req_pq_multi once before rejecting resPQ");

    transport_close(&t);
}

/**
 * Negative: resPQ uses a constructor CRC the client does not expect.
 * auth_step_req_pq must detect the mismatch and return -1.
 */
static void test_cold_boot_wrong_constructor(void) {
    fresh_mock("bad-crc");
    mt_server_simulate_cold_boot(MT_COLD_BOOT_WRONG_CONSTRUCTOR);

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "auth_step_req_pq rejects wrong constructor");

    transport_close(&t);
}

/**
 * Negative: server echoes the nonce back tampered. Client must detect
 * MITM / protocol bug and refuse to proceed (returns -1).
 */
static void test_cold_boot_server_nonce_mismatch_refuses(void) {
    fresh_mock("nonce-tamper");
    mt_server_simulate_cold_boot(MT_COLD_BOOT_NONCE_TAMPER);

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;

    int rc = auth_step_req_pq(&ctx);
    ASSERT(rc == -1, "auth_step_req_pq rejects tampered nonce echo");
    ASSERT(!s.has_auth_key, "session auth_key must NOT be set on nonce tamper");

    transport_close(&t);
}

/* ---- Step 2: PQ factorisation + req_DH_params --------------------- */

/**
 * After step 1 succeeds, step 2 must factorise PQ (=21 → 3 * 7),
 * RSA_PAD-encrypt the inner_data, and send req_DH_params. Mock
 * observes the handshake counter incrementing.
 */
static void test_cold_boot_step2_sends_req_dh_params(void) {
    fresh_mock("step2");
    /* Mode 2 ensures the mock also sends a server_DH_params_ok on the
     * second frame, so rpc_recv_unencrypted in auth_step_parse_dh does
     * not hang. For this test we only care that step 2 fires. */
    mt_server_simulate_cold_boot_through_step3();

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;
    ctx.dc_id     = 2;

    ASSERT(auth_step_req_pq(&ctx) == 0, "step 1 succeeds");
    ASSERT(ctx.pq == 21, "pq = 21 as emitted");

    int rc = auth_step_req_dh(&ctx);
    ASSERT(rc == 0, "auth_step_req_dh returns 0 on wire success");
    ASSERT(ctx.p == 3, "Pollard's rho factored pq=21 → p=3");
    ASSERT(ctx.q == 7, "Pollard's rho factored pq=21 → q=7");
    ASSERT(mt_server_handshake_req_dh_count() == 1,
           "mock observed exactly one req_DH_params");
    ASSERT(mt_server_request_crc_count(CRC_req_DH_params) == 1,
           "CRC ring also records req_DH_params");

    transport_close(&t);
}

/**
 * Negative: server emits pq that cannot be factored (a 64-bit prime
 * just below 2^64). Step 1 succeeds (client trusts the fingerprint
 * check and parses pq), but step 2's pq_factorize returns -1 so the
 * orchestrator bails out cleanly without writing session.bin.
 */
static void test_cold_boot_bad_pq_rejected_in_step2(void) {
    fresh_mock("bad-pq");
    mt_server_simulate_cold_boot(MT_COLD_BOOT_BAD_PQ);

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;
    ctx.dc_id     = 2;

    ASSERT(auth_step_req_pq(&ctx) == 0, "step 1 still succeeds with prime pq");
    ASSERT(ctx.pq == 0xFFFFFFFFFFFFFFC5ULL, "pq is the prime mock emits");

    int rc = auth_step_req_dh(&ctx);
    ASSERT(rc == -1, "step 2 rejects unfactorable PQ");

    transport_close(&t);
}

/* ---- Step 3: server_DH_params_ok rejection ------------------------- */

/**
 * With steps 1 + 2 passing and a synthetic server_DH_params_ok whose
 * encrypted_answer is random bytes, auth_step_parse_dh must decrypt,
 * find a bogus inner constructor, and return -1. The session must
 * remain un-keyed.
 */
static void test_cold_boot_parse_dh_rejects_garbage(void) {
    fresh_mock("parse-dh-garbage");
    mt_server_simulate_cold_boot_through_step3();

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    AuthKeyCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &t;
    ctx.session   = &s;
    ctx.dc_id     = 2;

    ASSERT(auth_step_req_pq(&ctx) == 0, "step 1 ok");
    ASSERT(auth_step_req_dh(&ctx) == 0, "step 2 ok");

    int rc = auth_step_parse_dh(&ctx);
    ASSERT(rc == -1, "step 3 rejects garbage server_DH_params_ok");
    ASSERT(!s.has_auth_key, "auth_key must NOT be set on step 3 failure");

    transport_close(&t);
}

/* ---- Orchestrator: mtproto_auth_key_gen ---------------------------- */

/**
 * End-to-end via the public orchestrator. Without a valid RSA private
 * key on the mock side the handshake cannot complete, so the
 * orchestrator must return -1 and the session must remain un-keyed.
 * Crucially, session.bin must NOT appear — a half-finished handshake
 * has no useful auth_key to persist and writing anything would confuse
 * the next cold-boot attempt.
 */
static void test_cold_boot_orchestrator_fails_cleanly(void) {
    fresh_mock("orchestrator-fail");
    ASSERT(!session_bin_exists(),
           "session.bin absent at start of cold-boot test");

    mt_server_simulate_cold_boot_through_step3();

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    int rc = mtproto_auth_key_gen(&t, &s);
    ASSERT(rc == -1, "mtproto_auth_key_gen returns -1 on step-3 failure");
    ASSERT(!s.has_auth_key, "no auth_key on session after failure");
    ASSERT(!session_bin_exists(),
           "session.bin still absent — no partial persistence");
    ASSERT(mt_server_handshake_req_pq_count() == 1,
           "orchestrator sent exactly one req_pq_multi");
    ASSERT(mt_server_handshake_req_dh_count() == 1,
           "orchestrator sent exactly one req_DH_params before failing");

    transport_close(&t);
}

/**
 * Null-argument guard: mtproto_auth_key_gen must reject NULL without
 * touching session.bin. This is the only branch of the orchestrator
 * that does not require a live mock; keep it here so functional
 * coverage of mtproto_auth_key_gen's error paths is complete.
 */
static void test_cold_boot_orchestrator_null_args(void) {
    fresh_mock("null-args");

    Transport t; MtProtoSession s;
    bring_transport_up(&t, &s);

    ASSERT(mtproto_auth_key_gen(NULL, &s) == -1, "NULL transport rejected");
    ASSERT(mtproto_auth_key_gen(&t, NULL) == -1, "NULL session rejected");
    ASSERT(!session_bin_exists(), "session.bin still absent");

    transport_close(&t);
}

/* ---- Suite entry point --------------------------------------------- */

void run_handshake_cold_boot_tests(void) {
    RUN_TEST(test_cold_boot_req_pq_happy_path);
    RUN_TEST(test_cold_boot_bad_fingerprint);
    RUN_TEST(test_cold_boot_wrong_constructor);
    RUN_TEST(test_cold_boot_server_nonce_mismatch_refuses);
    RUN_TEST(test_cold_boot_step2_sends_req_dh_params);
    RUN_TEST(test_cold_boot_bad_pq_rejected_in_step2);
    RUN_TEST(test_cold_boot_parse_dh_rejects_garbage);
    RUN_TEST(test_cold_boot_orchestrator_fails_cleanly);
    RUN_TEST(test_cold_boot_orchestrator_null_args);
}
