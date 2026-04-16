/**
 * @file api_call.c
 * @brief Telegram API call wrapper — initConnection + invokeWithLayer.
 */

#include "api_call.h"
#include "mtproto_rpc.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

void api_config_init(ApiConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->device_model = "tg-cli";
    cfg->system_version = "Linux";
    cfg->app_version = "0.1.0";
    cfg->system_lang_code = "en";
    cfg->lang_pack = "";
    cfg->lang_code = "en";
}

int api_wrap_query(const ApiConfig *cfg,
                   const uint8_t *query, size_t qlen,
                   uint8_t *out, size_t max_len, size_t *out_len) {
    if (!cfg || !query || !out || !out_len) return -1;

    TlWriter w;
    tl_writer_init(&w);

    /* invokeWithLayer#da9b0d0d layer:int query:!X = X */
    tl_write_uint32(&w, CRC_invokeWithLayer);
    tl_write_int32(&w, TL_LAYER);

    /* initConnection#c1cd5ea9 flags:# api_id:int device_model:string
       system_version:string app_version:string system_lang_code:string
       lang_pack:string lang_code:string proxy:flags.0?InputClientProxy
       params:flags.1?JSONValue query:!X = X */
    tl_write_uint32(&w, CRC_initConnection);
    tl_write_int32(&w, 0);  /* flags = 0 (no proxy, no params) */
    tl_write_int32(&w, cfg->api_id);
    tl_write_string(&w, cfg->device_model ? cfg->device_model : "");
    tl_write_string(&w, cfg->system_version ? cfg->system_version : "");
    tl_write_string(&w, cfg->app_version ? cfg->app_version : "");
    tl_write_string(&w, cfg->system_lang_code ? cfg->system_lang_code : "");
    tl_write_string(&w, cfg->lang_pack ? cfg->lang_pack : "");
    tl_write_string(&w, cfg->lang_code ? cfg->lang_code : "");

    /* Append the inner query */
    tl_write_raw(&w, query, qlen);

    if (w.len > max_len) {
        tl_writer_free(&w);
        return -1;
    }

    memcpy(out, w.data, w.len);
    *out_len = w.len;
    tl_writer_free(&w);
    return 0;
}

/** @brief Try to handle bad_server_salt: update session salt, signal retry.
 *
 * bad_server_salt#edab447b
 *   bad_msg_id:long bad_msg_seqno:int error_code:int new_server_salt:long
 *
 * Returns 1 if the response was bad_server_salt and the caller should
 * retry once, 0 otherwise.
 */
static int maybe_handle_bad_salt(MtProtoSession *s,
                                   const uint8_t *resp, size_t resp_len) {
    if (resp_len < 4) return 0;
    uint32_t crc;
    memcpy(&crc, resp, 4);
    if (crc != TL_bad_server_salt) return 0;

    /* Layout: crc(4) + bad_msg_id(8) + bad_msg_seqno(4) + error_code(4)
     *       + new_server_salt(8) = 28 bytes minimum. */
    if (resp_len < 28) return 0;
    uint64_t new_salt;
    memcpy(&new_salt, resp + 20, 8);
    s->server_salt = new_salt;
    logger_log(LOG_INFO,
               "api_call: server issued new salt 0x%016llx (retry)",
               (unsigned long long)new_salt);
    return 1;
}

static int api_call_once(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const uint8_t *query, size_t qlen,
                          uint8_t *resp, size_t max_len, size_t *resp_len,
                          int *bad_salt) {
    *bad_salt = 0;

    RAII_STRING uint8_t *wrapped = (uint8_t *)malloc(65536);
    if (!wrapped) return -1;
    size_t wrapped_len = 0;
    if (api_wrap_query(cfg, query, qlen, wrapped, 65536, &wrapped_len) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to wrap query");
        return -1;
    }

    if (rpc_send_encrypted(s, t, wrapped, wrapped_len, 1) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to send");
        return -1;
    }

    RAII_STRING uint8_t *raw_resp = (uint8_t *)malloc(65536);
    if (!raw_resp) return -1;
    size_t raw_len = 0;
    if (rpc_recv_encrypted(s, t, raw_resp, 65536, &raw_len) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to receive");
        return -1;
    }

    if (maybe_handle_bad_salt(s, raw_resp, raw_len)) {
        *bad_salt = 1;
        return 0;
    }

    uint64_t req_msg_id;
    const uint8_t *inner;
    size_t inner_len;
    const uint8_t *payload = raw_resp;
    size_t payload_len = raw_len;
    if (rpc_unwrap_result(raw_resp, raw_len, &req_msg_id,
                          &inner, &inner_len) == 0) {
        payload = inner;
        payload_len = inner_len;
    }

    if (rpc_unwrap_gzip(payload, payload_len,
                        resp, max_len, resp_len) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to unwrap response");
        return -1;
    }
    return 0;
}

int api_call(const ApiConfig *cfg,
             MtProtoSession *s, Transport *t,
             const uint8_t *query, size_t qlen,
             uint8_t *resp, size_t max_len, size_t *resp_len) {
    if (!cfg || !s || !t || !query || !resp || !resp_len) return -1;

    int bad_salt = 0;
    int rc = api_call_once(cfg, s, t, query, qlen,
                            resp, max_len, resp_len, &bad_salt);
    if (rc != 0) return rc;
    if (!bad_salt) return 0;

    /* One-shot retry with the newly-received salt. */
    rc = api_call_once(cfg, s, t, query, qlen,
                        resp, max_len, resp_len, &bad_salt);
    if (rc != 0) return rc;
    if (bad_salt) {
        logger_log(LOG_ERROR,
                   "api_call: bad_server_salt after retry — giving up");
        return -1;
    }
    return 0;
}
