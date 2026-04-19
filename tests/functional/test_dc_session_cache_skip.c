/**
 * @file test_dc_session_cache_skip.c
 * @brief TEST-21 — cached DC session skips DH handshake on second open.
 *
 * Validates US-15 acceptance criterion:
 *   "Cached foreign sessions skip the full handshake on every subsequent
 *    request."
 *
 * Strategy:
 *   1. Seed the home DC-2 session and a DC-4 foreign session on disk via
 *      mt_server_seed_session + mt_server_seed_extra_dc.  This simulates
 *      a prior run that performed the full DH handshake + auth.importAuth
 *      and persisted the resulting key.
 *   2. Call dc_session_open(4) — the fast path (session_store_load_dc)
 *      must be taken; zero DH-handshake CRCs are expected.
 *   3. Close the session and call dc_session_open(4) a second time — the
 *      persisted key is still on disk; again zero DH CRCs expected.
 *
 * DH handshake CRCs tracked:
 *   req_pq_multi       0xbe7e8ef1
 *   req_pq             0x60469778
 *   req_DH_params      0xd712e4be
 *   set_client_DH_params 0xf5045f1f
 */

#include "test_helpers.h"
#include "mock_tel_server.h"
#include "mock_socket.h"

#include "app/dc_session.h"
#include "app/session_store.h"
#include "transport.h"
#include "mtproto_session.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* TL CRCs for MTProto DH-handshake messages (client → server). */
#define CRC_req_pq_multi         0xbe7e8ef1U
#define CRC_req_pq               0x60469778U
#define CRC_req_DH_params        0xd712e4beU
#define CRC_set_client_DH_params 0xf5045f1fU

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-dcsess-%s", tag);
    /* Ensure config dir exists (mkdir -p equivalent via three mkdir calls). */
    char cfg_dir[512];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config", tmp);
    (void)mkdir(tmp, 0700);
    (void)mkdir(cfg_dir, 0700);
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/tg-cli", tmp);
    (void)mkdir(cfg_dir, 0700);
    /* Wipe any leftover session file from a prior run. */
    char bin[600];
    snprintf(bin, sizeof(bin), "%s/session.bin", cfg_dir);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

/** Assert no DH-handshake frame was sent for the last dc_session_open call. */
static void assert_no_handshake_crcs(void) {
    ASSERT(mt_server_request_crc_count(CRC_req_pq_multi) == 0,
           "no req_pq_multi (0xbe7e8ef1) — DH handshake NOT triggered");
    ASSERT(mt_server_request_crc_count(CRC_req_pq) == 0,
           "no req_pq (0x60469778) — DH handshake NOT triggered");
    ASSERT(mt_server_request_crc_count(CRC_req_DH_params) == 0,
           "no req_DH_params (0xd712e4be) — DH handshake NOT triggered");
    ASSERT(mt_server_request_crc_count(CRC_set_client_DH_params) == 0,
           "no set_client_DH_params (0xf5045f1f) — DH handshake NOT triggered");
}

/**
 * FT-21a — first dc_session_open on DC 4 with a pre-seeded session.
 *
 * dc_session_open must load from session_store (fast path) and open the
 * transport without sending any DH-handshake frames.
 */
static void test_dc4_first_open_uses_cache(void) {
    with_tmp_home("first");
    mt_server_init();
    mt_server_reset();

    /* Seed home DC 2 and foreign DC 4 — simulates a prior successful run. */
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0,
           "seed home DC 2");
    ASSERT(mt_server_seed_extra_dc(4) == 0,
           "seed DC 4 session on disk");

    DcSession sess;
    ASSERT(dc_session_open(4, &sess) == 0,
           "dc_session_open(4) succeeds via cached key");
    ASSERT(sess.dc_id == 4,     "dc_id is 4");
    ASSERT(sess.authorized == 1, "marked authorized (key was cached)");

    /* Core assertion: no DH handshake frames crossed the wire. */
    assert_no_handshake_crcs();

    dc_session_close(&sess);
    mt_server_reset();
}

/**
 * FT-21b — second dc_session_open on DC 4: session persisted by prior call.
 *
 * After FT-21a the DC-4 key is still in session.bin.  A second open must
 * again take the fast path (zero handshake RPCs).
 */
static void test_dc4_second_open_skips_handshake(void) {
    with_tmp_home("second");
    mt_server_init();
    mt_server_reset();

    /* Seed home DC 2 and DC 4. */
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0,
           "seed home DC 2");
    ASSERT(mt_server_seed_extra_dc(4) == 0,
           "seed DC 4 session on disk");

    /* First open — fast path, establishes session + closes. */
    DcSession sess1;
    ASSERT(dc_session_open(4, &sess1) == 0, "first dc_session_open ok");
    dc_session_close(&sess1);

    /* Reset CRC counters so the second open is measured in isolation. */
    mt_server_reset();
    /* Re-seed so the mock can handle the new transport connection. */
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0,
           "re-seed DC 2 after reset");
    /* Note: mt_server_reset wipes seeded flag but session.bin persists on
     * disk because HOME is still the same tmpdir. The server only needs to
     * know the key for encrypted-frame decryption; mt_server_seed_session
     * restores g_srv.auth_key to the same deterministic bytes. */

    /* Second open — must skip handshake. */
    DcSession sess2;
    ASSERT(dc_session_open(4, &sess2) == 0,
           "second dc_session_open(4) succeeds via cached key");
    ASSERT(sess2.authorized == 1, "still marked authorized");

    /* Core assertion: second open sent zero DH frames. */
    assert_no_handshake_crcs();

    dc_session_close(&sess2);
    mt_server_reset();
}

/**
 * FT-21c — dc_session_open on DC 4 sets authorized=1 when key is cached.
 *
 * Verifies that the fast-path branch in dc_session.c sets out->authorized=1
 * so that callers need not run auth.importAuthorization again.
 */
static void test_dc4_cache_hit_authorized_flag(void) {
    with_tmp_home("authflag");
    mt_server_init();
    mt_server_reset();

    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0,
           "seed DC 2");
    ASSERT(mt_server_seed_extra_dc(4) == 0,
           "seed DC 4");

    DcSession sess;
    ASSERT(dc_session_open(4, &sess) == 0, "dc_session_open ok");
    ASSERT(sess.authorized == 1,
           "authorized flag is 1 when key loaded from cache");
    assert_no_handshake_crcs();

    dc_session_close(&sess);
    mt_server_reset();
}

void run_dc_session_cache_skip_tests(void) {
    RUN_TEST(test_dc4_first_open_uses_cache);
    RUN_TEST(test_dc4_second_open_skips_handshake);
    RUN_TEST(test_dc4_cache_hit_authorized_flag);
}
