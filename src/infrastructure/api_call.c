/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

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

/** @brief Classify an incoming encrypted frame.
 *
 * Returns one of the SVC_* codes. When SVC_BAD_SALT the salt on @p s is
 * already updated; when SVC_SKIP the caller should simply recv again.
 */
enum {
    SVC_RESULT = 0,    /**< ordinary rpc_result / unwrapped payload */
    SVC_BAD_SALT,      /**< new salt stored, caller should retry send */
    SVC_SKIP,          /**< service-only frame, loop back to recv */
    SVC_ERROR,         /**< unrecoverable */
};

static int classify_service_frame(MtProtoSession *s,
                                   const uint8_t *resp, size_t resp_len) {
    if (resp_len < 4) return SVC_ERROR;
    uint32_t crc;
    memcpy(&crc, resp, 4);

    if (crc == TL_bad_server_salt) {
        if (resp_len < 28) return SVC_ERROR;
        uint64_t new_salt;
        memcpy(&new_salt, resp + 20, 8);
        s->server_salt = new_salt;
        logger_log(LOG_INFO,
                   "api_call: server issued new salt 0x%016llx (retry)",
                   (unsigned long long)new_salt);
        return SVC_BAD_SALT;
    }

    if (crc == TL_bad_msg_notification) {
        /* bad_msg_notification#a7eff811 bad_msg_id:long bad_msg_seqno:int
         *                               error_code:int = BadMsgNotification
         * Layout: crc(4) + bad_msg_id(8) + bad_msg_seqno(4) + error_code(4). */
        int32_t error_code = 0;
        if (resp_len >= 20) memcpy(&error_code, resp + 16, 4);
        logger_log(LOG_WARN, "api_call: bad_msg_notification code=%d", error_code);
        /* We cannot recover from msg_id / seqno disagreements here; bailing. */
        return SVC_ERROR;
    }

    if (crc == TL_new_session_created) {
        /* new_session_created#9ec20908 first_msg_id:long unique_id:long
         *                              server_salt:long = NewSession
         * Layout: crc(4) + first_msg_id(8) + unique_id(8) + server_salt(8). */
        if (resp_len >= 28) {
            uint64_t fresh_salt;
            memcpy(&fresh_salt, resp + 20, 8);
            s->server_salt = fresh_salt;
            logger_log(LOG_INFO,
                       "api_call: new_session_created, salt=0x%016llx",
                       (unsigned long long)fresh_salt);
        }
        return SVC_SKIP;
    }

    if (crc == TL_msgs_ack || crc == TL_pong) {
        logger_log(LOG_DEBUG, "api_call: ignoring service frame 0x%08x", crc);
        return SVC_SKIP;
    }

    return SVC_RESULT;
}

/** Maximum number of service frames we'll drain before giving up. */
#define SERVICE_FRAME_LIMIT 8

static int api_call_once(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const uint8_t *query, size_t qlen,
                          uint8_t *resp, size_t max_len, size_t *resp_len,
                          int *bad_salt) {
    *bad_salt = 0;

    /* 1 MiB accommodates the largest call we make — a saveBigFilePart
     * carrying a 512 KiB chunk + initConnection + invokeWithLayer. */
    enum { API_BUF_SIZE = 1024 * 1024 };
    RAII_STRING uint8_t *wrapped = (uint8_t *)malloc(API_BUF_SIZE);
    if (!wrapped) return -1;
    size_t wrapped_len = 0;
    if (api_wrap_query(cfg, query, qlen, wrapped, API_BUF_SIZE,
                         &wrapped_len) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to wrap query");
        return -1;
    }

    if (rpc_send_encrypted(s, t, wrapped, wrapped_len, 1) != 0) {
        logger_log(LOG_ERROR, "api_call: failed to send");
        return -1;
    }

    RAII_STRING uint8_t *raw_resp = (uint8_t *)malloc(API_BUF_SIZE);
    if (!raw_resp) return -1;
    size_t raw_len = 0;

    /* Drain service frames until we see a real result. If we never see one
     * within SERVICE_FRAME_LIMIT iterations, treat it as a protocol failure —
     * without this guard the loop would fall through with `raw_resp` still
     * holding a service frame (e.g. msgs_ack) and `rpc_unwrap_gzip` — which
     * is permissive about non-gzip payloads — would succeed, surfacing the
     * service frame bytes to the caller as if they were a real result. */
    int saw_result = 0;
    for (int attempt = 0; attempt < SERVICE_FRAME_LIMIT; attempt++) {
        if (rpc_recv_encrypted(s, t, raw_resp, API_BUF_SIZE, &raw_len) != 0) {
            logger_log(LOG_ERROR, "api_call: failed to receive");
            return -1;
        }
        int klass = classify_service_frame(s, raw_resp, raw_len);
        if (klass == SVC_ERROR) return -1;
        if (klass == SVC_BAD_SALT) { *bad_salt = 1; return 0; }
        if (klass == SVC_SKIP)     continue;
        saw_result = 1;
        break; /* SVC_RESULT */
    }
    if (!saw_result) {
        logger_log(LOG_ERROR,
                   "api_call: drained %d service frames without a real result",
                   SERVICE_FRAME_LIMIT);
        return -1;
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
