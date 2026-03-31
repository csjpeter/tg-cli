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

void test_api_call(void) {
    RUN_TEST(test_api_config_init);
    RUN_TEST(test_api_wrap_query_structure);
    RUN_TEST(test_api_wrap_query_null_args);
    RUN_TEST(test_api_wrap_query_buffer_too_small);
}
