/**
 * @file test_api_call.c
 * @brief Unit tests for API call wrapper (initConnection + invokeWithLayer).
 */

#include "test_helpers.h"
#include "api_call.h"
#include "tl_serial.h"
#include "mock_crypto.h"

#include <stdlib.h>
#include <string.h>

void test_api_config_init(void) {
    ApiConfig cfg;
    api_config_init(&cfg);
    ASSERT(cfg.api_id == 0, "api_id should be 0 before setting");
    ASSERT(strcmp(cfg.device_model, "tg-cli") == 0, "device_model default");
    ASSERT(strcmp(cfg.app_version, "0.1.0") == 0, "app_version default");
    ASSERT(strcmp(cfg.system_lang_code, "en") == 0, "lang_code default");
}

void test_api_wrap_query_structure(void) {
    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 12345;

    /* Simple inner query: just a constructor */
    TlWriter q;
    tl_writer_init(&q);
    tl_write_uint32(&q, 0xDEADBEEF); /* fake query constructor */

    uint8_t out[4096];
    size_t out_len = 0;
    int rc = api_wrap_query(&cfg, q.data, q.len, out, sizeof(out), &out_len);
    tl_writer_free(&q);

    ASSERT(rc == 0, "wrap should succeed");
    ASSERT(out_len > 0, "output should have data");

    /* Parse the wrapped output */
    TlReader r = tl_reader_init(out, out_len);

    /* invokeWithLayer constructor */
    uint32_t crc1 = tl_read_uint32(&r);
    ASSERT(crc1 == CRC_invokeWithLayer, "should start with invokeWithLayer");

    /* layer */
    int32_t layer = tl_read_int32(&r);
    ASSERT(layer == TL_LAYER, "layer should match TL_LAYER");

    /* initConnection constructor */
    uint32_t crc2 = tl_read_uint32(&r);
    ASSERT(crc2 == CRC_initConnection, "should have initConnection");

    /* flags */
    int32_t flags = tl_read_int32(&r);
    ASSERT(flags == 0, "flags should be 0");

    /* api_id */
    int32_t api_id = tl_read_int32(&r);
    ASSERT(api_id == 12345, "api_id should be 12345");

    /* device_model */
    char *dm = tl_read_string(&r);
    ASSERT(dm != NULL, "device_model should not be NULL");
    ASSERT(strcmp(dm, "tg-cli") == 0, "device_model should be tg-cli");
    free(dm);

    /* system_version */
    char *sv = tl_read_string(&r);
    ASSERT(sv != NULL, "system_version");
    free(sv);

    /* app_version */
    char *av = tl_read_string(&r);
    ASSERT(av != NULL, "app_version");
    free(av);

    /* system_lang_code */
    char *slc = tl_read_string(&r);
    ASSERT(slc != NULL, "system_lang_code");
    free(slc);

    /* lang_pack */
    char *lp = tl_read_string(&r);
    ASSERT(lp != NULL, "lang_pack");
    free(lp);

    /* lang_code */
    char *lc = tl_read_string(&r);
    ASSERT(lc != NULL, "lang_code");
    free(lc);

    /* Inner query should follow */
    uint32_t inner_crc = tl_read_uint32(&r);
    ASSERT(inner_crc == 0xDEADBEEF, "inner query constructor should be at the end");
}

void test_api_wrap_query_null_args(void) {
    ApiConfig cfg;
    api_config_init(&cfg);
    uint8_t query[4] = {0};
    uint8_t out[256];
    size_t out_len;

    ASSERT(api_wrap_query(NULL, query, 4, out, 256, &out_len) == -1, "NULL cfg");
    ASSERT(api_wrap_query(&cfg, NULL, 4, out, 256, &out_len) == -1, "NULL query");
    ASSERT(api_wrap_query(&cfg, query, 4, NULL, 256, &out_len) == -1, "NULL out");
    ASSERT(api_wrap_query(&cfg, query, 4, out, 256, NULL) == -1, "NULL out_len");
}

void test_api_wrap_query_buffer_too_small(void) {
    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 1;

    uint8_t query[4] = {0};
    uint8_t out[8]; /* way too small */
    size_t out_len;

    int rc = api_wrap_query(&cfg, query, 4, out, 8, &out_len);
    ASSERT(rc == -1, "should fail with buffer too small");
}

/* ---- bad_server_salt retry ---- */

#include "mock_socket.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_registry.h"

static void pack_encrypted(const uint8_t *payload, size_t plen,
                            uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t z24[24] = {0}; tl_write_raw(&w, z24, 24);
    uint8_t hdr[32] = {0};
    uint32_t pl32 = (uint32_t)plen;
    memcpy(hdr + 28, &pl32, 4);
    tl_write_raw(&w, hdr, 32);
    tl_write_raw(&w, payload, plen);
    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0}; tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    out[0] = (uint8_t)(w.len / 4);
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
    tl_writer_free(&w);
}

static void test_bad_server_salt_retry(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* First response: bad_server_salt. */
    uint8_t bad_payload[64];
    memset(bad_payload, 0, sizeof(bad_payload));
    uint32_t crc = TL_bad_server_salt;
    memcpy(bad_payload, &crc, 4);
    /* bad_msg_id (8) + bad_msg_seqno (4) + error_code (4) zeros */
    uint64_t new_salt = 0x9988776655443322ULL;
    memcpy(bad_payload + 20, &new_salt, 8);

    uint8_t resp1[256]; size_t rlen1 = 0;
    pack_encrypted(bad_payload, 28, resp1, &rlen1);

    /* Second response (post-retry): a valid rpc_result carrying bool_true. */
    uint8_t ok_payload[32];
    TlWriter w2; tl_writer_init(&w2);
    tl_write_uint32(&w2, TL_boolTrue);
    memcpy(ok_payload, w2.data, w2.len);
    size_t ok_plen = w2.len;
    tl_writer_free(&w2);

    uint8_t resp2[256]; size_t rlen2 = 0;
    pack_encrypted(ok_payload, ok_plen, resp2, &rlen2);

    mock_socket_set_response(resp1, rlen1);
    mock_socket_append_response(resp2, rlen2);

    MtProtoSession s;
    mtproto_session_init(&s);
    uint8_t key[256] = {0};
    mtproto_session_set_auth_key(&s, key);
    mtproto_session_set_salt(&s, 0x1111111111111111ULL);

    Transport t;
    transport_init(&t); t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg; api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    uint8_t query[8];
    memset(query, 0, sizeof(query));
    crc = TL_boolTrue; /* any tiny query */
    memcpy(query, &crc, 4);

    uint8_t resp[128];
    size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, query, 4, resp, sizeof(resp), &resp_len);

    ASSERT(rc == 0, "api_call succeeds after bad_salt retry");
    ASSERT(s.server_salt == 0x9988776655443322ULL,
           "salt updated from bad_server_salt");
}

/* ---- new_session_created skip ---- */
static void test_new_session_created_skipped(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* First response: new_session_created — should be silently swallowed. */
    uint8_t ns_payload[32];
    memset(ns_payload, 0, sizeof(ns_payload));
    uint32_t ns_crc = TL_new_session_created;
    memcpy(ns_payload, &ns_crc, 4);
    /* first_msg_id (8) + unique_id (8) + server_salt (8) — put a
     * recognisable salt at offset 20. */
    uint64_t salt = 0xAABBCCDD00112233ULL;
    memcpy(ns_payload + 20, &salt, 8);

    uint8_t resp_ns[256]; size_t rlen_ns = 0;
    pack_encrypted(ns_payload, 28, resp_ns, &rlen_ns);

    /* Second response: real bool_true. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolTrue);
    uint8_t ok_pay[8]; memcpy(ok_pay, w.data, w.len);
    size_t ok_plen = w.len; tl_writer_free(&w);

    uint8_t resp_ok[256]; size_t rlen_ok = 0;
    pack_encrypted(ok_pay, ok_plen, resp_ok, &rlen_ok);

    mock_socket_set_response(resp_ns, rlen_ns);
    mock_socket_append_response(resp_ok, rlen_ok);

    MtProtoSession s;
    mtproto_session_init(&s);
    uint8_t key[256] = {0};
    mtproto_session_set_auth_key(&s, key);
    mtproto_session_set_salt(&s, 0x1);

    Transport t;
    transport_init(&t); t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg; api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    uint32_t q_crc = TL_boolTrue;
    uint8_t query[4]; memcpy(query, &q_crc, 4);

    uint8_t resp[64]; size_t resp_len = 0;
    int rc = api_call(&cfg, &s, &t, query, 4, resp, sizeof(resp), &resp_len);
    ASSERT(rc == 0, "new_session_created was skipped cleanly");
    ASSERT(s.server_salt == 0xAABBCCDD00112233ULL,
           "salt taken from new_session_created");
}

void test_api_call(void) {
    RUN_TEST(test_api_config_init);
    RUN_TEST(test_api_wrap_query_structure);
    RUN_TEST(test_api_wrap_query_null_args);
    RUN_TEST(test_api_wrap_query_buffer_too_small);
    RUN_TEST(test_bad_server_salt_retry);
    RUN_TEST(test_new_session_created_skipped);
}
