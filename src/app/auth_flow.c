/**
 * @file app/auth_flow.c
 * @brief High-level login flow with DC migration.
 */

#include "app/auth_flow.h"
#include "app/dc_config.h"
#include "app/session_store.h"

#include "auth_session.h"
#include "infrastructure/auth_2fa.h"
#include "mtproto_auth.h"
#include "mtproto_rpc.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

/** Maximum redirects we follow before giving up. Telegram usually needs 1. */
#define AUTH_MAX_MIGRATIONS 3

int auth_flow_connect_dc(int dc_id, Transport *t, MtProtoSession *s) {
    if (!t || !s) return -1;

    const DcEndpoint *ep = dc_lookup(dc_id);
    if (!ep) {
        logger_log(LOG_ERROR, "auth_flow: unknown DC id %d", dc_id);
        return -1;
    }

    if (transport_connect(t, ep->host, ep->port) != 0) {
        logger_log(LOG_ERROR, "auth_flow: connect failed for DC%d (%s:%d)",
                   dc_id, ep->host, ep->port);
        return -1;
    }
    t->dc_id = dc_id;

    if (mtproto_auth_key_gen(t, s) != 0) {
        logger_log(LOG_ERROR, "auth_flow: DH auth key gen failed on DC%d",
                   dc_id);
        transport_close(t);
        return -1;
    }
    logger_log(LOG_INFO, "auth_flow: connected to DC%d, auth key ready", dc_id);
    return 0;
}

/** Tear down the current session/transport and reconnect to a new DC. */
static int migrate(int new_dc, Transport *t, MtProtoSession *s) {
    logger_log(LOG_INFO, "auth_flow: migrating to DC%d", new_dc);
    transport_close(t);
    /* Re-init session — auth key is DC-scoped. */
    mtproto_session_init(s);
    return auth_flow_connect_dc(new_dc, t, s);
}

int auth_flow_login(const ApiConfig *cfg,
                    const AuthFlowCallbacks *cb,
                    Transport *t, MtProtoSession *s,
                    AuthFlowResult *out) {
    if (!cfg || !cb || !t || !s) return -1;
    if (!cb->get_phone || !cb->get_code) {
        logger_log(LOG_ERROR, "auth_flow: get_phone/get_code callbacks required");
        return -1;
    }

    if (out) memset(out, 0, sizeof(*out));

    /* Fast path: restore a persisted session. */
    {
        int saved_dc = 0;
        mtproto_session_init(s);
        if (session_store_load(s, &saved_dc) == 0 && s->has_auth_key) {
            const DcEndpoint *ep = dc_lookup(saved_dc);
            if (ep && transport_connect(t, ep->host, ep->port) == 0) {
                t->dc_id = saved_dc;
                logger_log(LOG_INFO,
                           "auth_flow: reusing persisted session on DC%d",
                           saved_dc);
                if (out) { out->dc_id = saved_dc; out->user_id = 0; }
                return 0;
            }
            logger_log(LOG_WARN,
                       "auth_flow: persisted session unusable, re-login");
            if (ep) transport_close(t);
            mtproto_session_init(s);
        }
    }

    int current_dc = DEFAULT_DC_ID;
    if (auth_flow_connect_dc(current_dc, t, s) != 0) return -1;

    char phone[64];
    if (cb->get_phone(cb->user, phone, sizeof(phone)) != 0) {
        logger_log(LOG_ERROR, "auth_flow: phone number input failed");
        return -1;
    }

    /* ---- auth.sendCode (with migration) ---- */
    AuthSentCode sent = {0};
    int migrations = 0;
    for (;;) {
        RpcError err = {0};
        err.migrate_dc = -1;
        int rc = auth_send_code(cfg, s, t, phone, &sent, &err);
        if (rc == 0) break;
        if (err.migrate_dc > 0 && migrations < AUTH_MAX_MIGRATIONS) {
            migrations++;
            if (migrate(err.migrate_dc, t, s) != 0) return -1;
            current_dc = err.migrate_dc;
            continue;
        }
        logger_log(LOG_ERROR, "auth_flow: sendCode failed (%d: %s)",
                   err.error_code, err.error_msg);
        return -1;
    }

    /* ---- user enters code ---- */
    char code[32];
    if (cb->get_code(cb->user, code, sizeof(code)) != 0) {
        logger_log(LOG_ERROR, "auth_flow: code input failed");
        return -1;
    }

    /* ---- auth.signIn (with migration; 2FA detected but unimplemented) ---- */
    int64_t uid = 0;
    for (;;) {
        RpcError err = {0};
        err.migrate_dc = -1;
        int rc = auth_sign_in(cfg, s, t, phone, sent.phone_code_hash, code,
                              &uid, &err);
        if (rc == 0) break;
        if (err.migrate_dc > 0 && migrations < AUTH_MAX_MIGRATIONS) {
            migrations++;
            if (migrate(err.migrate_dc, t, s) != 0) return -1;
            current_dc = err.migrate_dc;
            /* After migration we must re-send the code from scratch because
             * phone_code_hash is DC-scoped. Loop back via a recursive call
             * after the outer caller handles the new state. For now we
             * signal failure with a clear diagnostic. */
            logger_log(LOG_ERROR,
                       "auth_flow: unexpected migration during signIn — "
                       "restart login flow");
            return -1;
        }
        if (strcmp(err.error_msg, "SESSION_PASSWORD_NEEDED") == 0) {
            if (out) out->needs_password = 1;
            if (!cb->get_password) {
                logger_log(LOG_ERROR,
                           "auth_flow: 2FA required but no get_password callback");
                return -1;
            }
            logger_log(LOG_INFO,
                       "auth_flow: 2FA password required — running SRP flow");

            Account2faPassword params = {0};
            RpcError gp_err = {0};
            if (auth_2fa_get_password(cfg, s, t, &params, &gp_err) != 0) {
                logger_log(LOG_ERROR,
                           "auth_flow: account.getPassword failed (%d: %s)",
                           gp_err.error_code, gp_err.error_msg);
                return -1;
            }
            if (!params.has_password) {
                logger_log(LOG_ERROR,
                           "auth_flow: SESSION_PASSWORD_NEEDED but server "
                           "reports no password configured");
                return -1;
            }
            char pwd[128];
            if (cb->get_password(cb->user, pwd, sizeof(pwd)) != 0) {
                logger_log(LOG_ERROR, "auth_flow: password input failed");
                return -1;
            }
            RpcError cp_err = {0};
            int64_t cp_uid = 0;
            if (auth_2fa_check_password(cfg, s, t, &params, pwd,
                                           &cp_uid, &cp_err) != 0) {
                logger_log(LOG_ERROR,
                           "auth_flow: auth.checkPassword failed (%d: %s)",
                           cp_err.error_code, cp_err.error_msg);
                return -1;
            }
            uid = cp_uid;
            break;
        }
        logger_log(LOG_ERROR, "auth_flow: signIn failed (%d: %s)",
                   err.error_code, err.error_msg);
        return -1;
    }

    if (out) {
        out->dc_id = current_dc;
        out->user_id = uid;
    }
    logger_log(LOG_INFO, "auth_flow: login complete on DC%d, user_id=%lld",
               current_dc, (long long)uid);

    /* Persist so the next run can skip sendCode + signIn. */
    if (session_store_save(s, current_dc) != 0) {
        logger_log(LOG_WARN,
                   "auth_flow: failed to persist session (non-fatal)");
    }
    return 0;
}
