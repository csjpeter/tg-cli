/**
 * @file auth_session.c
 * @brief Telegram phone-number login: auth.sendCode + auth.signIn.
 */

#include "auth_session.h"
#include "mtproto_rpc.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

/* ---- Internal: build auth.sendCode request ---- */

static int build_send_code(const ApiConfig *cfg, const char *phone,
                            uint8_t *out, size_t max_len, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    /* auth.sendCode#a677244f phone_number:string api_id:int api_hash:string
     *                        settings:CodeSettings */
    tl_write_uint32(&w, CRC_auth_sendCode);
    tl_write_string(&w, phone);
    tl_write_int32(&w,  cfg->api_id);
    tl_write_string(&w, cfg->api_hash ? cfg->api_hash : "");

    /* codeSettings#ad253d78 flags:# (flags=0 → no special options) */
    tl_write_uint32(&w, CRC_codeSettings);
    tl_write_uint32(&w, 0); /* flags = 0 */

    if (w.len > max_len) {
        tl_writer_free(&w);
        return -1;
    }
    memcpy(out, w.data, w.len);
    *out_len = w.len;
    tl_writer_free(&w);
    return 0;
}

/* ---- Internal: skip a sentCodeType sub-object ---- */

static int skip_sent_code_type(TlReader *r) {
    if (!tl_reader_ok(r)) return -1;
    uint32_t type_crc = tl_read_uint32(r);

    switch (type_crc) {
    case CRC_auth_sentCodeTypeApp:
    case CRC_auth_sentCodeTypeSms:
    case CRC_auth_sentCodeTypeCall:
        /* length:int */
        tl_read_int32(r);
        return 0;

    case CRC_auth_sentCodeTypeFlashCall: {
        /* pattern:string */
        RAII_STRING char *pattern = tl_read_string(r);
        (void)pattern;
        return 0;
    }

    default:
        logger_log(LOG_WARN, "auth_send_code: unknown sentCodeType 0x%08x", type_crc);
        return -1;
    }
}

/* ---- auth_send_code ---- */

int auth_send_code(const ApiConfig *cfg,
                   MtProtoSession *s, Transport *t,
                   const char *phone,
                   AuthSentCode *out) {
    if (!cfg || !s || !t || !phone || !out) return -1;

    uint8_t query[4096];
    size_t qlen = 0;
    if (build_send_code(cfg, phone, query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "auth_send_code: failed to build request");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;

    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_send_code: api_call failed");
        return -1;
    }

    if (resp_len < 4) {
        logger_log(LOG_ERROR, "auth_send_code: response too short");
        return -1;
    }

    /* Check for rpc_error */
    uint32_t constructor;
    memcpy(&constructor, resp, 4);
    if (constructor == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "auth_send_code: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (constructor != CRC_auth_sentCode) {
        logger_log(LOG_ERROR, "auth_send_code: unexpected constructor 0x%08x",
                   constructor);
        return -1;
    }

    /* Parse auth.sentCode:
     * flags:# type:auth.SentCodeType phone_code_hash:string
     * next_type:flags.1?auth.CodeType timeout:flags.2?int */
    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* skip constructor */

    uint32_t flags = tl_read_uint32(&r);

    if (skip_sent_code_type(&r) != 0) {
        logger_log(LOG_ERROR, "auth_send_code: failed to parse sentCodeType");
        return -1;
    }

    RAII_STRING char *hash = tl_read_string(&r);
    if (!hash) {
        logger_log(LOG_ERROR, "auth_send_code: failed to read phone_code_hash");
        return -1;
    }

    size_t hash_len = strlen(hash);
    if (hash_len >= AUTH_CODE_HASH_MAX) {
        logger_log(LOG_ERROR, "auth_send_code: phone_code_hash too long");
        return -1;
    }
    memcpy(out->phone_code_hash, hash, hash_len + 1);

    /* timeout: flags.2 */
    out->timeout = 0;
    if (flags & (1u << 2)) {
        out->timeout = tl_read_int32(&r);
    }

    logger_log(LOG_INFO, "auth_send_code: code sent, hash=%.12s... timeout=%d",
               out->phone_code_hash, out->timeout);
    return 0;
}

/* ---- Internal: build auth.signIn request ---- */

static int build_sign_in(const char *phone, const char *hash, const char *code,
                          uint8_t *out, size_t max_len, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    /* auth.signIn#8d52a951 flags:# phone_number:string phone_code_hash:string
     *                      phone_code:flags.0?string */
    tl_write_uint32(&w, CRC_auth_signIn);
    tl_write_uint32(&w, 1u);  /* flags = 0x1 → phone_code present */
    tl_write_string(&w, phone);
    tl_write_string(&w, hash);
    tl_write_string(&w, code); /* flags.0 is set */

    if (w.len > max_len) {
        tl_writer_free(&w);
        return -1;
    }
    memcpy(out, w.data, w.len);
    *out_len = w.len;
    tl_writer_free(&w);
    return 0;
}

/* ---- auth_sign_in ---- */

int auth_sign_in(const ApiConfig *cfg,
                 MtProtoSession *s, Transport *t,
                 const char *phone,
                 const char *phone_code_hash,
                 const char *code,
                 int64_t *user_id_out) {
    if (!cfg || !s || !t || !phone || !phone_code_hash || !code) return -1;

    uint8_t query[4096];
    size_t qlen = 0;
    if (build_sign_in(phone, phone_code_hash, code,
                      query, sizeof(query), &qlen) != 0) {
        logger_log(LOG_ERROR, "auth_sign_in: failed to build request");
        return -1;
    }

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;

    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_sign_in: api_call failed");
        return -1;
    }

    if (resp_len < 4) {
        logger_log(LOG_ERROR, "auth_sign_in: response too short");
        return -1;
    }

    uint32_t constructor;
    memcpy(&constructor, resp, 4);

    if (constructor == TL_rpc_error) {
        RpcError err;
        rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "auth_sign_in: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (constructor != CRC_auth_authorization) {
        logger_log(LOG_ERROR, "auth_sign_in: unexpected constructor 0x%08x",
                   constructor);
        return -1;
    }

    /* Parse auth.authorization:
     * flags:# setup_password_required:flags.0?true
     *         otherwise_relogin_days:flags.1?int
     *         tmp_sessions:flags.0?int
     *         user:User */
    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* skip constructor */
    uint32_t flags = tl_read_uint32(&r);

    /* skip optional fields */
    if (flags & (1u << 1)) tl_read_int32(&r); /* otherwise_relogin_days */

    /* user starts with constructor + id */
    uint32_t user_crc = tl_read_uint32(&r);
    if (user_crc != TL_user && user_crc != TL_userFull) {
        logger_log(LOG_WARN, "auth_sign_in: unexpected user constructor 0x%08x",
                   user_crc);
        /* still consider successful — we authenticated */
        if (user_id_out) *user_id_out = 0;
        return 0;
    }

    /* user#3ff6ecb0: flags:# self:... id:long ... */
    tl_read_uint32(&r); /* flags */
    int64_t uid = tl_read_int64(&r);
    if (user_id_out) *user_id_out = uid;

    logger_log(LOG_INFO, "auth_sign_in: authenticated as user_id=%lld",
               (long long)uid);
    return 0;
}
