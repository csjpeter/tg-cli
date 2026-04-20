/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file test_pii_redact.c
 * @brief Unit tests for pii_redact helpers and log-redaction of auth PII.
 *
 * Verifies:
 *   1. redact_phone with various phone shapes (short, long, NULL).
 *   2. A log-capture test that confirms a send_code log line does NOT
 *      contain the raw phone_code_hash value.
 */

#include "test_helpers.h"
#include "pii_redact.h"
#include "logger.h"
#include "auth_session.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* redact_phone tests                                                   */
/* ------------------------------------------------------------------ */

static void test_redact_phone_long(void) {
    char out[32];
    redact_phone("+15551234567", out, sizeof(out));
    /* Last 4 digits must be visible */
    ASSERT(strstr(out, "4567") != NULL,
           "redact_phone: last 4 digits must be present");
    /* Raw middle digits must NOT be present */
    ASSERT(strstr(out, "555123") == NULL,
           "redact_phone: middle digits must be masked");
    /* Must start with '+' */
    ASSERT(out[0] == '+', "redact_phone: must start with '+'");
}

static void test_redact_phone_short(void) {
    char out[32];
    /* A 4-digit bare number — too short to keep last 4 and still mask */
    redact_phone("1234", out, sizeof(out));
    /* Must not expose digits, output should be the full-mask form */
    ASSERT(strstr(out, "1234") == NULL,
           "redact_phone: short phone must be fully masked");
    ASSERT(out[0] == '+', "redact_phone: short phone must start with '+'");
}

static void test_redact_phone_null(void) {
    char out[32];
    redact_phone(NULL, out, sizeof(out));
    ASSERT(strstr(out, "null") != NULL,
           "redact_phone: NULL phone must produce '(null)' output");
}

static void test_redact_phone_empty(void) {
    char out[32];
    redact_phone("", out, sizeof(out));
    ASSERT(strstr(out, "null") != NULL,
           "redact_phone: empty phone must produce '(null)' output");
}

static void test_redact_phone_with_plus(void) {
    char out[32];
    redact_phone("+447700900123", out, sizeof(out));
    ASSERT(strstr(out, "0123") != NULL,
           "redact_phone: last 4 of +447700900123 must be 0123");
    ASSERT(strstr(out, "77009") == NULL,
           "redact_phone: internal digits must not appear");
}

/* ------------------------------------------------------------------ */
/* Log-capture test: send_code must NOT log the raw hash               */
/* ------------------------------------------------------------------ */

/* Reuse the fake-response builder from test_auth_session.c */
static void build_fake_encrypted_response_pr(const uint8_t *payload, size_t plen,
                                              uint8_t *out, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);
    uint8_t zeros24[24] = {0};
    tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);
    size_t total = w.len;
    size_t payload_start = 24;
    size_t enc_part = total - payload_start;
    if (enc_part % 16 != 0) {
        size_t pad = 16 - (enc_part % 16);
        uint8_t zeros[16] = {0};
        tl_write_raw(&w, zeros, pad);
    }
    size_t wire_bytes = w.len;
    size_t wire_units = wire_bytes / 4;
    uint8_t *result = (uint8_t *)malloc(1 + wire_bytes);
    result[0] = (uint8_t)wire_units;
    memcpy(result + 1, w.data, wire_bytes);
    *out_len = 1 + wire_bytes;
    memcpy(out, result, *out_len);
    free(result);
    tl_writer_free(&w);
}

/** The secret hash used in the log-capture test. */
#define SECRET_HASH "SUPERSECRET_HASH_XYZ_9876"

static void test_send_code_does_not_log_hash(void) {
    mock_socket_reset();
    mock_crypto_reset();

    /* Build a fake sentCode response containing the secret hash */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_sentCode);
    tl_write_uint32(&w, 0);                          /* flags */
    tl_write_uint32(&w, CRC_auth_sentCodeTypeApp);
    tl_write_int32(&w, 5);                           /* length */
    tl_write_string(&w, SECRET_HASH);
    uint8_t payload[512];
    size_t plen = w.len < sizeof(payload) ? w.len : sizeof(payload);
    memcpy(payload, w.data, plen);
    tl_writer_free(&w);

    uint8_t resp_buf[1024];
    size_t resp_len = 0;
    build_fake_encrypted_response_pr(payload, plen, resp_buf, &resp_len);
    mock_socket_set_response(resp_buf, resp_len);

    /* Direct logging to a temp file so we can inspect it */
    const char *log_path = "/tmp/tg-cli-pii-test.log";
    unlink(log_path);
    logger_init(log_path, LOG_DEBUG);

    MtProtoSession s;
    mtproto_session_init(&s);
    s.session_id = 0;
    uint8_t fake_key[256] = {0};
    mtproto_session_set_auth_key(&s, fake_key);
    mtproto_session_set_salt(&s, 0x1122334455667788ULL);

    Transport t;
    transport_init(&t);
    t.fd = 42; t.connected = 1; t.dc_id = 1;

    ApiConfig cfg;
    api_config_init(&cfg);
    cfg.api_id = 12345; cfg.api_hash = "deadbeef";

    AuthSentCode result;
    memset(&result, 0, sizeof(result));
    int rc = auth_send_code(&cfg, &s, &t, "+15551234567", &result, NULL);
    logger_close();

    ASSERT(rc == 0, "log-capture: send_code must succeed");

    /* Read log and verify the secret hash is absent */
    FILE *f = fopen(log_path, "r");
    ASSERT(f != NULL, "log-capture: log file must exist");
    if (!f) return;

    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, SECRET_HASH)) { found = 1; break; }
    }
    fclose(f);
    unlink(log_path);

    ASSERT(found == 0,
           "log-capture: phone_code_hash must NOT appear in log output");
}

/* ------------------------------------------------------------------ */
/* Entry point called from test_runner.c                               */
/* ------------------------------------------------------------------ */

void run_pii_redact_tests(void) {
    RUN_TEST(test_redact_phone_long);
    RUN_TEST(test_redact_phone_short);
    RUN_TEST(test_redact_phone_null);
    RUN_TEST(test_redact_phone_empty);
    RUN_TEST(test_redact_phone_with_plus);
    RUN_TEST(test_send_code_does_not_log_hash);
}
