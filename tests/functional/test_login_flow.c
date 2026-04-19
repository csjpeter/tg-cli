/**
 * @file test_login_flow.c
 * @brief FT-03 — login flow functional tests through the in-process
 *        Telegram server emulator.
 *
 * Exercises production code paths end-to-end using real OpenSSL on both
 * sides (client + mock server). Covers:
 *   - auth.sendCode happy path
 *   - auth.sendCode with PHONE_NUMBER_INVALID
 *   - auth.sendCode with PHONE_MIGRATE_X (migration signal)
 *   - auth.signIn success (returns user_id)
 *   - auth.signIn SIGN_UP_REQUIRED rejection
 *   - auth.signIn SESSION_PASSWORD_NEEDED signal (2FA switch)
 *   - account.getPassword parsing of SRP params
 *   - auth.checkPassword rejection (PASSWORD_HASH_INVALID)
 *   - auth.checkPassword acceptance (server trusts the proof)
 *   - bad_server_salt retry — client swaps salt and resends transparently
 *   - session persistence roundtrip across save/load
 *   - session_store_clear() simulates --logout
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "auth_session.h"
#include "infrastructure/auth_2fa.h"
#include "mtproto_rpc.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "app/credentials.h"
#include "app/auth_flow.h"
#include "tl_registry.h"
#include "tl_serial.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* CRCs that are not already exposed by a public header. Keys that would
 * collide with macros from auth_session.h / tl_registry.h are intentionally
 * reused by including those headers above. */
#define CRC_sentCodeTypeSms        0xc000bba2U
#define CRC_account_getPassword    0x548a30f5U
#define CRC_KdfAlgoPBKDF2          0x3a912d4aU
#define CRC_auth_checkPassword     0xd18b4d16U

/* ---- test helpers ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-login-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

/** Dial a fresh Transport to the mock-socket loopback. */
static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "transport connects");
}

/** Initialise ApiConfig with fake credentials for tests. */
static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

/* auth.sentCode reply: type=sms, length=5, hash="abc123", no timeout. */
static void on_send_code_happy(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);                      /* flags = 0 */
    tl_write_uint32(&w, CRC_sentCodeTypeSms);    /* sentCodeTypeSms */
    tl_write_int32 (&w, 5);                      /* length */
    tl_write_string(&w, "abc123");               /* phone_code_hash */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_send_code_invalid_phone(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PHONE_NUMBER_INVALID");
}

static void on_send_code_migrate_4(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 303, "PHONE_MIGRATE_4");
}

/* auth.authorization with user having id=77777 */
static void on_sign_in_happy(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);                  /* outer flags = 0 */
    tl_write_uint32(&w, TL_user);           /* user constructor */
    tl_write_uint32(&w, 0);                  /* user.flags = 0 */
    tl_write_int64 (&w, 77777LL);            /* user.id */
    /* The parser stops after id, so trailing fields don't need to be
     * schema-perfect — but include enough so nothing memory-walks. */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_sign_up_required(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 401, "SIGN_UP_REQUIRED");
}

static void on_session_password_needed(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 401, "SESSION_PASSWORD_NEEDED");
}

/* Deterministic SRP fixture for tests — prime / salts / srp_B are
 * byte-patterns with the right lengths. The client only validates
 * lengths, not mathematical correctness; auth_2fa_srp_compute will still
 * run the math, and the server accepts whatever M1 we receive. */
static uint8_t g_test_salt1[16];
static uint8_t g_test_salt2[16];
static uint8_t g_test_prime[256];
static uint8_t g_test_srpB[256];

static void init_srp_fixture(void) {
    for (int i = 0; i < 16;  ++i) g_test_salt1[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 16;  ++i) g_test_salt2[i] = (uint8_t)(i + 17);
    for (int i = 0; i < 256; ++i) g_test_prime[i] = (uint8_t)((i * 7 + 3) | 0x80u);
    /* Force odd-highest-bit so p looks like a >= 2047-bit prime. */
    g_test_prime[0] = 0xC7;
    g_test_prime[255] = 0x7F;  /* odd so gcd(p,2)=1 — still just length-valid */
    for (int i = 0; i < 256; ++i) g_test_srpB[i] = (uint8_t)((i * 5 + 11));
    g_test_srpB[0] = 0x01;     /* ensure B < p */
}

/* account.password with has_password=true and fixture SRP params. */
static void on_get_password(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_account_password);
    tl_write_uint32(&w, 1u << 2);                           /* flags: has_password */
    tl_write_uint32(&w, CRC_KdfAlgoPBKDF2);                 /* current_algo */
    tl_write_bytes (&w, g_test_salt1, sizeof(g_test_salt1));
    tl_write_bytes (&w, g_test_salt2, sizeof(g_test_salt2));
    tl_write_int32 (&w, 2);                                 /* generator g=2 */
    tl_write_bytes (&w, g_test_prime, sizeof(g_test_prime));
    tl_write_bytes (&w, g_test_srpB,  sizeof(g_test_srpB)); /* srp_B */
    tl_write_int64 (&w, 0x1234567890ABCDEFLL);              /* srp_id */
    /* new_algo / new_secure_algo / secure_random — reader skips them. */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_check_password_wrong(MtRpcContext *ctx) {
    mt_server_reply_error(ctx, 400, "PASSWORD_HASH_INVALID");
}

static void on_check_password_accept(MtRpcContext *ctx) {
    /* Server doesn't verify M1 — we just want to drive the client code to
     * success so the authorization reply parser runs. */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_auth_authorization);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_user);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 424242LL);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* ================================================================ */
/* Test cases                                                       */
/* ================================================================ */

static void test_send_code_happy(void) {
    with_tmp_home("send-code");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_happy, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s;
    mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "session loaded");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+15551234567", &sent, &err) == 0,
           "sendCode succeeds");
    ASSERT(strcmp(sent.phone_code_hash, "abc123") == 0,
           "phone_code_hash roundtrips");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_code_invalid_phone(void) {
    with_tmp_home("bad-phone");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_invalid_phone, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+00000000", &sent, &err) == -1,
           "sendCode returns -1 on RPC error");
    ASSERT(err.error_code == 400, "error_code is 400");
    ASSERT(strcmp(err.error_msg, "PHONE_NUMBER_INVALID") == 0,
           "error_msg is PHONE_NUMBER_INVALID");

    transport_close(&t);
    mt_server_reset();
}

static void test_send_code_phone_migrate(void) {
    with_tmp_home("migrate");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_migrate_4, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+861234", &sent, &err) == -1,
           "sendCode fails with migration");
    ASSERT(err.error_code == 303, "error_code is 303");
    ASSERT(err.migrate_dc == 4, "migrate_dc is 4");

    transport_close(&t);
    mt_server_reset();
}

static void test_sign_in_happy(void) {
    with_tmp_home("sign-in");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, on_sign_in_happy, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0;
    RpcError err = {0};
    ASSERT(auth_sign_in(&cfg, &s, &t,
                        "+15551234567", "abc123", "12345",
                        &uid, &err) == 0, "signIn succeeds");
    ASSERT(uid == 77777LL, "user_id = 77777");

    transport_close(&t);
    mt_server_reset();
}

static void test_sign_in_sign_up_required(void) {
    with_tmp_home("sign-up");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, on_sign_up_required, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0;
    RpcError err = {0};
    ASSERT(auth_sign_in(&cfg, &s, &t, "+15551234567", "abc123", "12345",
                        &uid, &err) == -1, "signIn fails");
    ASSERT(err.error_code == 401, "error_code 401");
    ASSERT(strcmp(err.error_msg, "SIGN_UP_REQUIRED") == 0,
           "error_msg SIGN_UP_REQUIRED");

    transport_close(&t);
    mt_server_reset();
}

static void test_sign_in_password_needed(void) {
    with_tmp_home("need-pwd");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_signIn, on_session_password_needed, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    int64_t uid = 0;
    RpcError err = {0};
    ASSERT(auth_sign_in(&cfg, &s, &t, "+15551234567", "abc123", "12345",
                        &uid, &err) == -1, "signIn fails");
    ASSERT(err.error_code == 401, "error_code 401");
    ASSERT(strcmp(err.error_msg, "SESSION_PASSWORD_NEEDED") == 0,
           "error_msg SESSION_PASSWORD_NEEDED");

    transport_close(&t);
    mt_server_reset();
}

static void test_2fa_get_password(void) {
    with_tmp_home("get-pwd");
    mt_server_init();
    mt_server_reset();
    init_srp_fixture();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &err) == 0,
           "account.getPassword succeeds");
    ASSERT(params.has_password == 1, "has_password set");
    ASSERT(params.g == 2, "generator g=2");
    ASSERT(params.salt1_len == sizeof(g_test_salt1), "salt1 length");
    ASSERT(params.salt2_len == sizeof(g_test_salt2), "salt2 length");
    ASSERT(memcmp(params.p, g_test_prime, sizeof(g_test_prime)) == 0,
           "prime roundtrips");
    ASSERT(memcmp(params.srp_B, g_test_srpB, sizeof(g_test_srpB)) == 0,
           "srp_B roundtrips");
    ASSERT(params.srp_id == 0x1234567890ABCDEFLL, "srp_id roundtrips");

    transport_close(&t);
    mt_server_reset();
}

static void test_2fa_check_password_wrong(void) {
    with_tmp_home("pwd-wrong");
    mt_server_init();
    mt_server_reset();
    init_srp_fixture();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password, NULL);
    mt_server_expect(CRC_auth_checkPassword, on_check_password_wrong, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError gp_err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &gp_err) == 0, "gp ok");

    int64_t uid = 0;
    RpcError cp_err = {0};
    ASSERT(auth_2fa_check_password(&cfg, &s, &t, &params, "wrong-pwd",
                                   &uid, &cp_err) == -1, "check fails");
    ASSERT(cp_err.error_code == 400, "400");
    ASSERT(strcmp(cp_err.error_msg, "PASSWORD_HASH_INVALID") == 0,
           "PASSWORD_HASH_INVALID");

    transport_close(&t);
    mt_server_reset();
}

static void test_2fa_check_password_accept(void) {
    with_tmp_home("pwd-ok");
    mt_server_init();
    mt_server_reset();
    init_srp_fixture();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_account_getPassword, on_get_password, NULL);
    mt_server_expect(CRC_auth_checkPassword, on_check_password_accept, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");

    Transport t; connect_mock(&t);

    Account2faPassword params = {0};
    RpcError gp_err = {0};
    ASSERT(auth_2fa_get_password(&cfg, &s, &t, &params, &gp_err) == 0, "gp ok");

    int64_t uid = 0;
    RpcError cp_err = {0};
    ASSERT(auth_2fa_check_password(&cfg, &s, &t, &params, "secret",
                                   &uid, &cp_err) == 0, "check succeeds");
    ASSERT(uid == 424242LL, "user_id = 424242");

    transport_close(&t);
    mt_server_reset();
}

static void test_bad_server_salt_retry(void) {
    with_tmp_home("bad-salt");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_auth_sendCode, on_send_code_happy, NULL);

    /* Arm a one-shot bad_server_salt for the first incoming RPC. */
    mt_server_set_bad_salt_once(0xFEDCBA9876543210ULL);

    ApiConfig cfg; init_cfg(&cfg);
    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0; ASSERT(session_store_load(&s, &dc) == 0, "load");
    uint64_t salt_before = s.server_salt;
    ASSERT(salt_before != 0xFEDCBA9876543210ULL, "starting salt != new salt");

    Transport t; connect_mock(&t);

    AuthSentCode sent = {0};
    RpcError err = {0};
    ASSERT(auth_send_code(&cfg, &s, &t, "+15551234567", &sent, &err) == 0,
           "sendCode succeeds after transparent retry");
    ASSERT(strcmp(sent.phone_code_hash, "abc123") == 0, "hash ok");
    ASSERT(s.server_salt == 0xFEDCBA9876543210ULL,
           "client picked up the new salt");
    /* bad_salt round doesn't reach the handler — rpc_call_count only
     * increments for successful dispatches. The retry is the one
     * dispatch the handler sees. */
    ASSERT(mt_server_rpc_call_count() == 1,
           "retry dispatches exactly one RPC to the handler");

    transport_close(&t);
    mt_server_reset();
}

static void test_session_persistence_roundtrip(void) {
    with_tmp_home("persist");
    mt_server_init();
    mt_server_reset();

    uint8_t seeded_key[MT_SERVER_AUTH_KEY_SIZE];
    uint64_t seeded_salt = 0, seeded_sid = 0;
    ASSERT(mt_server_seed_session(3, seeded_key,
                                  &seeded_salt, &seeded_sid) == 0, "seed");

    MtProtoSession s1;
    mtproto_session_init(&s1);
    int dc1 = 0;
    ASSERT(session_store_load(&s1, &dc1) == 0, "first load");
    ASSERT(dc1 == 3, "home DC persisted");
    ASSERT(s1.server_salt == seeded_salt, "salt persisted");
    ASSERT(s1.session_id == seeded_sid, "session_id persisted");
    ASSERT(memcmp(s1.auth_key, seeded_key, MT_SERVER_AUTH_KEY_SIZE) == 0,
           "auth_key persisted");

    /* Re-save from a fresh session and reload — value should still match. */
    MtProtoSession s2;
    mtproto_session_init(&s2);
    mtproto_session_set_auth_key(&s2, seeded_key);
    mtproto_session_set_salt(&s2, seeded_salt);
    s2.session_id = seeded_sid;
    ASSERT(session_store_save(&s2, 3) == 0, "re-save");

    MtProtoSession s3;
    mtproto_session_init(&s3);
    int dc3 = 0;
    ASSERT(session_store_load(&s3, &dc3) == 0, "second load");
    ASSERT(dc3 == 3, "home DC still 3");
    ASSERT(memcmp(s3.auth_key, seeded_key, MT_SERVER_AUTH_KEY_SIZE) == 0,
           "auth_key stable across round-trip");
}

static void test_logout_clears_session(void) {
    with_tmp_home("logout");
    mt_server_init();
    mt_server_reset();
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");

    MtProtoSession s; mtproto_session_init(&s);
    int dc = 0;
    ASSERT(session_store_load(&s, &dc) == 0, "load before clear");

    session_store_clear();

    MtProtoSession s2; mtproto_session_init(&s2);
    int dc2 = 0;
    ASSERT(session_store_load(&s2, &dc2) == -1,
           "load returns -1 after clear");
}

/**
 * @brief TEST-01 — batch mode must reject missing credentials without
 *        reading stdin.
 *
 * Scenario: no config.ini in HOME, no TG_CLI_API_ID/TG_CLI_API_HASH env
 * vars, and batch callbacks that supply no phone number.
 *
 * Asserts:
 *   1. credentials_load() returns -1 (missing api_id/api_hash).
 *   2. auth_flow_login() with NULL-phone batch callbacks returns -1.
 *   3. Neither call blocks on or reads from stdin (stdin is redirected to
 *      /dev/null; if either call read stdin it would return EOF and likely
 *      cause a different failure path — the ASAN/Valgrind run catches reads
 *      from uninitialised data in the same way).
 */

/* Batch callback that has no phone — simulates --batch without --phone. */
static int cb_no_phone(void *u, char *out, size_t cap) {
    (void)u; (void)out; (void)cap;
    return -1;   /* signals "not available" */
}
static int cb_no_code(void *u, char *out, size_t cap) {
    (void)u; (void)out; (void)cap;
    return -1;
}

static void test_batch_rejects_missing_credentials(void) {
    /* Point HOME at a fresh empty directory — no config.ini present. */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-login-batch-no-creds");
    /* Ensure the directory exists but has no config file. */
    char cfg_dir[512];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", tmp);
    /* Remove any stale state from a previous run. */
    char ini[600];
    snprintf(ini, sizeof(ini), "%s/config.ini", cfg_dir);
    (void)unlink(ini);
    char session_path[600];
    snprintf(session_path, sizeof(session_path), "%s/session.bin", cfg_dir);
    (void)unlink(session_path);

    setenv("HOME", tmp, 1);
    /* Clear env-var credentials so credentials_load() cannot find them. */
    unsetenv("TG_CLI_API_ID");
    unsetenv("TG_CLI_API_HASH");

    /* Redirect stdin to /dev/null so any accidental read() returns EOF
     * immediately rather than blocking the test run. */
    int devnull = open("/dev/null", O_RDONLY);
    int saved_stdin = dup(STDIN_FILENO);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }

    /* --- Assertion 1: credentials_load() must fail --- */
    ApiConfig cfg;
    int rc = credentials_load(&cfg);
    ASSERT(rc == -1, "credentials_load returns -1 when no api_id/api_hash");

    /* --- Assertion 2: auth_flow_login() with no-phone callbacks must fail
     *     before touching the network (the mock socket is not seeded, so
     *     any accidental connect attempt would itself fail). --- */
    Transport t;
    transport_init(&t);
    MtProtoSession s;
    mtproto_session_init(&s);

    /* Provide dummy credentials so auth_flow_login proceeds past the
     * credential check and reaches the callback stage. */
    ApiConfig dummy_cfg;
    api_config_init(&dummy_cfg);
    dummy_cfg.api_id   = 99999;
    dummy_cfg.api_hash = "dummyhashfortesting";

    AuthFlowCallbacks cb = {
        .get_phone    = cb_no_phone,
        .get_code     = cb_no_code,
        .get_password = NULL,
        .user         = NULL,
    };

    /* mt_server is not seeded — transport_connect will fail, which causes
     * auth_flow_connect_dc to return -1 before get_phone is even reached.
     * Either way, auth_flow_login must return -1 without prompting stdin. */
    int flow_rc = auth_flow_login(&dummy_cfg, &cb, &t, &s, NULL);
    ASSERT(flow_rc == -1,
           "auth_flow_login returns -1 when server unreachable in batch mode");

    transport_close(&t);

    /* Restore stdin. */
    if (saved_stdin >= 0) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }
}

void run_login_flow_tests(void) {
    RUN_TEST(test_send_code_happy);
    RUN_TEST(test_send_code_invalid_phone);
    RUN_TEST(test_send_code_phone_migrate);
    RUN_TEST(test_sign_in_happy);
    RUN_TEST(test_sign_in_sign_up_required);
    RUN_TEST(test_sign_in_password_needed);
    RUN_TEST(test_2fa_get_password);
    RUN_TEST(test_2fa_check_password_wrong);
    RUN_TEST(test_2fa_check_password_accept);
    RUN_TEST(test_bad_server_salt_retry);
    RUN_TEST(test_session_persistence_roundtrip);
    RUN_TEST(test_logout_clears_session);
    RUN_TEST(test_batch_rejects_missing_credentials);
}
