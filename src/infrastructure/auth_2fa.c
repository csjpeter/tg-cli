/**
 * @file auth_2fa.c
 * @brief account.getPassword + SRP-based auth.checkPassword (P3-03).
 *
 * SRP spec: https://core.telegram.org/api/srp
 */

#include "infrastructure/auth_2fa.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mtproto_rpc.h"
#include "api_call.h"
#include "crypto.h"
#include "logger.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- TL CRCs ---- */
#define CRC_account_getPassword       0x548a30f5u
#define CRC_auth_checkPassword        0xd18b4d16u
#define CRC_inputCheckPasswordSRP     TL_inputCheckPasswordSRP
#define CRC_account_password          TL_account_password
#define CRC_KdfAlgoPBKDF2 \
    TL_passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow
#define CRC_KdfAlgoUnknown            TL_passwordKdfAlgoUnknown
#define CRC_SecureKdfUnknown          0x4a8537u
#define CRC_securePasswordKdfAlgoPBKDF2 0xbbf2dda0u
#define CRC_securePasswordKdfAlgoSHA512 0x86471d92u
#define CRC_securePasswordKdfAlgoUnknown 0x004a8537u

/* ---- helpers ---- */

static void bytes_xor_eq(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] ^= src[i];
}

/* H(salt | data | salt) — "SH" in the Telegram spec. */
static void sh_sha256(const unsigned char *salt, size_t salt_len,
                       const unsigned char *data, size_t data_len,
                       unsigned char out[32]) {
    size_t total = salt_len * 2 + data_len;
    RAII_STRING uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) { memset(out, 0, 32); return; }
    memcpy(buf, salt, salt_len);
    memcpy(buf + salt_len, data, data_len);
    memcpy(buf + salt_len + data_len, salt, salt_len);
    crypto_sha256(buf, total, out);
}

/* ---- account.getPassword ---- */

static int parse_password_kdf_algo(TlReader *r, Account2faPassword *out) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_KdfAlgoUnknown) {
        logger_log(LOG_WARN, "auth_2fa: server uses unknown KDF algo");
        return -1;
    }
    if (crc != CRC_KdfAlgoPBKDF2) {
        logger_log(LOG_ERROR, "auth_2fa: unsupported KDF algo 0x%08x", crc);
        return -1;
    }
    size_t s1 = 0, s2 = 0;
    RAII_STRING uint8_t *salt1 = tl_read_bytes(r, &s1);
    RAII_STRING uint8_t *salt2 = tl_read_bytes(r, &s2);
    if ((!salt1 && s1) || (!salt2 && s2)) return -1;
    if (s1 > SRP_SALT_MAX || s2 > SRP_SALT_MAX) {
        logger_log(LOG_ERROR, "auth_2fa: salt too large (%zu / %zu)", s1, s2);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    int32_t g = tl_read_int32(r);
    size_t p_len = 0;
    RAII_STRING uint8_t *p = tl_read_bytes(r, &p_len);
    if ((!p && p_len) || p_len != SRP_PRIME_LEN) {
        logger_log(LOG_ERROR, "auth_2fa: unexpected prime length %zu", p_len);
        return -1;
    }
    if (out) {
        if (salt1) memcpy(out->salt1, salt1, s1);
        if (salt2) memcpy(out->salt2, salt2, s2);
        out->salt1_len = s1; out->salt2_len = s2;
        out->g = g;
        memcpy(out->p, p, p_len);
    }
    return 0;
}

int auth_2fa_get_password(const ApiConfig *cfg,
                           MtProtoSession *s, Transport *t,
                           Account2faPassword *out, RpcError *err) {
    if (!cfg || !s || !t || !out) return -1;
    memset(out, 0, sizeof(*out));

    uint8_t query[8];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_account_getPassword);
    size_t qlen = w.len;
    memcpy(query, w.data, qlen);
    tl_writer_free(&w);

    uint8_t resp[2048]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_2fa: account.getPassword api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (top != CRC_account_password) {
        logger_log(LOG_ERROR, "auth_2fa: unexpected top 0x%08x", top);
        return -1;
    }

    /* account.password#957b50fb flags:#
     *   has_recovery:flags.0?true
     *   has_secure_values:flags.1?true
     *   has_password:flags.2?true
     *   current_algo:flags.2?PasswordKdfAlgo
     *   srp_B:flags.2?bytes
     *   srp_id:flags.2?long
     *   hint:flags.3?string
     *   email_unconfirmed_pattern:flags.4?string
     *   new_algo:PasswordKdfAlgo
     *   new_secure_algo:SecurePasswordKdfAlgo
     *   secure_random:bytes
     *   pending_reset_date:flags.5?int
     *   login_email_pattern:flags.6?string
     */
    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top */
    uint32_t flags = tl_read_uint32(&r);

    out->has_password = (flags & (1u << 2)) ? 1 : 0;

    if (out->has_password) {
        if (parse_password_kdf_algo(&r, out) != 0) return -1;

        size_t srpB_len = 0;
        RAII_STRING uint8_t *srpB = tl_read_bytes(&r, &srpB_len);
        if (!srpB || srpB_len == 0 || srpB_len > SRP_PRIME_LEN) {
            logger_log(LOG_ERROR, "auth_2fa: bad srp_B length %zu", srpB_len);
            return -1;
        }
        memset(out->srp_B, 0, SRP_PRIME_LEN);
        memcpy(out->srp_B + (SRP_PRIME_LEN - srpB_len), srpB, srpB_len);

        if (r.len - r.pos < 8) return -1;
        out->srp_id = tl_read_int64(&r);
    }
    if (flags & (1u << 3)) { if (tl_skip_string(&r) != 0) return -1; }
    if (flags & (1u << 4)) { if (tl_skip_string(&r) != 0) return -1; }

    /* new_algo and new_secure_algo are not used during check; skip/ignore. */
    /* We don't strictly need to parse the rest to drive checkPassword. */
    return 0;
}

/* ---- SRP math ---- */

/* Compute x = PH2(password, salt1, salt2). Result is 32 bytes (SHA-256). */
static int compute_x(const char *password,
                     const uint8_t *salt1, size_t s1,
                     const uint8_t *salt2, size_t s2,
                     uint8_t out_x[32]) {
    size_t plen = strlen(password);

    /* PH1 = SH(SH(password, salt1), salt2). */
    uint8_t inner[32];
    sh_sha256(salt1, s1, (const uint8_t *)password, plen, inner);
    uint8_t ph1[32];
    sh_sha256(salt2, s2, inner, 32, ph1);

    /* PH2 = SH(pbkdf2(ph1, salt1, 100000), salt2). */
    uint8_t pbkdf2_out[64];
    if (crypto_pbkdf2_hmac_sha512(ph1, 32, salt1, s1,
                                    100000, pbkdf2_out, 64) != 0)
        return -1;
    sh_sha256(salt2, s2, pbkdf2_out, 64, out_x);
    return 0;
}

/* Pack g (int) into left-padded 256-byte big-endian representation. */
static void pack_g(int32_t g, uint8_t out[SRP_PRIME_LEN]) {
    memset(out, 0, SRP_PRIME_LEN);
    uint32_t gu = (uint32_t)g;
    out[SRP_PRIME_LEN - 4] = (uint8_t)(gu >> 24);
    out[SRP_PRIME_LEN - 3] = (uint8_t)(gu >> 16);
    out[SRP_PRIME_LEN - 2] = (uint8_t)(gu >>  8);
    out[SRP_PRIME_LEN - 1] = (uint8_t)(gu      );
}

/* Compute the SRP proof. Writes A[256] and M1[32] on success.
 * When @p a_in is non-NULL the caller pins the 256-byte client private
 * exponent (used by functional tests). Otherwise we pull from
 * crypto_rand_bytes. */
static int srp_compute(const Account2faPassword *p, const char *password,
                       const uint8_t *a_in,
                       uint8_t A_out[SRP_PRIME_LEN], uint8_t M1_out[32]) {
    uint8_t g_bytes[SRP_PRIME_LEN]; pack_g(p->g, g_bytes);

    uint8_t a[SRP_PRIME_LEN];
    if (a_in) {
        memcpy(a, a_in, SRP_PRIME_LEN);
    } else if (crypto_rand_bytes(a, SRP_PRIME_LEN) != 0) {
        return -1;
    }

    CryptoBnCtx *ctx = crypto_bn_ctx_new();
    if (!ctx) return -1;

    /* A = g^a mod p */
    size_t A_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_exp(A_out, &A_len, g_bytes, SRP_PRIME_LEN,
                            a, SRP_PRIME_LEN, p->p, SRP_PRIME_LEN, ctx) != 0)
        goto fail;

    /* x = PH2(password, salt1, salt2) */
    uint8_t x[32];
    if (compute_x(password, p->salt1, p->salt1_len,
                   p->salt2, p->salt2_len, x) != 0) goto fail;

    /* v = g^x mod p */
    uint8_t v[SRP_PRIME_LEN]; size_t v_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_exp(v, &v_len, g_bytes, SRP_PRIME_LEN,
                            x, 32, p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    /* k = H(p | g) */
    uint8_t kbuf[SRP_PRIME_LEN * 2]; uint8_t k[32];
    memcpy(kbuf, p->p, SRP_PRIME_LEN);
    memcpy(kbuf + SRP_PRIME_LEN, g_bytes, SRP_PRIME_LEN);
    crypto_sha256(kbuf, sizeof(kbuf), k);

    /* u = H(A | B) */
    uint8_t ubuf[SRP_PRIME_LEN * 2]; uint8_t u[32];
    memcpy(ubuf, A_out, SRP_PRIME_LEN);
    memcpy(ubuf + SRP_PRIME_LEN, p->srp_B, SRP_PRIME_LEN);
    crypto_sha256(ubuf, sizeof(ubuf), u);

    /* kv = k*v mod p */
    uint8_t kv[SRP_PRIME_LEN]; size_t kv_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_mul(kv, &kv_len, k, 32, v, SRP_PRIME_LEN,
                            p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    /* base = (B - kv) mod p */
    uint8_t base[SRP_PRIME_LEN]; size_t base_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_sub(base, &base_len, p->srp_B, SRP_PRIME_LEN,
                            kv, SRP_PRIME_LEN,
                            p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    /* ux = u*x (not reduced mod p — exponent can exceed p).
     * We want a + u*x as a raw integer for modular exponentiation, so
     * compute it modulo (p - 1) is NOT required; OpenSSL BN_mod_exp
     * handles arbitrary exponents. We just need the correct value. */
    /* Use mod_mul with modulus = p as an approximation? No — a + u*x
     * mod p would change the result. Instead reduce via mod (p-1) per
     * Fermat, but Telegram's server expects the exponent itself. The
     * standard trick: compute S = (B - k*v)^(a + u*x) mod p by
     * chaining BN_mul + BN_add with a regular context, not modular.
     *
     * We implement that directly here using two temp BN ops: first
     * (u*x) without modular reduction, then (a + ux). We expose an
     * internal helper below. */
    /* For simplicity, compute S via two BN_mod_exp steps using
     * mathematical identity:
     *   S = pow(base, a) * pow(base, u*x) mod p
     *     = pow(base, a) mod p  *  pow(pow(base, x), u) mod p  mod p
     *
     * This avoids dealing with the full 256*256 = 512-byte product. */
    uint8_t base_a[SRP_PRIME_LEN]; size_t ba_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_exp(base_a, &ba_len, base, SRP_PRIME_LEN,
                            a, SRP_PRIME_LEN, p->p, SRP_PRIME_LEN, ctx) != 0)
        goto fail;

    uint8_t base_x[SRP_PRIME_LEN]; size_t bx_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_exp(base_x, &bx_len, base, SRP_PRIME_LEN,
                            x, 32, p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    uint8_t base_xu[SRP_PRIME_LEN]; size_t bxu_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_exp(base_xu, &bxu_len, base_x, SRP_PRIME_LEN,
                            u, 32, p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    uint8_t S[SRP_PRIME_LEN]; size_t S_len = SRP_PRIME_LEN;
    if (crypto_bn_mod_mul(S, &S_len, base_a, SRP_PRIME_LEN,
                            base_xu, SRP_PRIME_LEN,
                            p->p, SRP_PRIME_LEN, ctx) != 0) goto fail;

    /* K = H(S) */
    uint8_t K[32];
    crypto_sha256(S, SRP_PRIME_LEN, K);

    /* M1 = H(H(p) XOR H(g) | H(salt1) | H(salt2) | A | B | K) */
    uint8_t h_p[32], h_g[32];
    crypto_sha256(p->p, SRP_PRIME_LEN, h_p);
    crypto_sha256(g_bytes, SRP_PRIME_LEN, h_g);
    bytes_xor_eq(h_p, h_g, 32);

    uint8_t h_s1[32], h_s2[32];
    crypto_sha256(p->salt1, p->salt1_len, h_s1);
    crypto_sha256(p->salt2, p->salt2_len, h_s2);

    uint8_t m1_buf[32 + 32 + 32 + SRP_PRIME_LEN + SRP_PRIME_LEN + 32];
    size_t off = 0;
    memcpy(m1_buf + off, h_p, 32); off += 32;
    memcpy(m1_buf + off, h_s1, 32); off += 32;
    memcpy(m1_buf + off, h_s2, 32); off += 32;
    memcpy(m1_buf + off, A_out, SRP_PRIME_LEN); off += SRP_PRIME_LEN;
    memcpy(m1_buf + off, p->srp_B, SRP_PRIME_LEN); off += SRP_PRIME_LEN;
    memcpy(m1_buf + off, K, 32); off += 32;
    crypto_sha256(m1_buf, off, M1_out);

    crypto_bn_ctx_free(ctx);
    return 0;

fail:
    crypto_bn_ctx_free(ctx);
    return -1;
}

/* ---- auth.checkPassword ---- */

int auth_2fa_check_password(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const Account2faPassword *params,
                             const char *password,
                             int64_t *user_id_out, RpcError *err) {
    if (!cfg || !s || !t || !params || !password) return -1;
    if (!params->has_password) {
        logger_log(LOG_ERROR, "auth_2fa: no password configured on account");
        return -1;
    }

    uint8_t A[SRP_PRIME_LEN], M1[32];
    if (srp_compute(params, password, NULL, A, M1) != 0) return -1;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_checkPassword);
    tl_write_uint32(&w, CRC_inputCheckPasswordSRP);
    tl_write_int64 (&w, params->srp_id);
    tl_write_bytes (&w, A, SRP_PRIME_LEN);
    tl_write_bytes (&w, M1, 32);

    uint8_t query[512];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[2048]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "auth_2fa: auth.checkPassword api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (top != TL_auth_authorization) {
        logger_log(LOG_ERROR, "auth_2fa: unexpected top 0x%08x", top);
        return -1;
    }

    /* auth.authorization — extract user.id (flags.0=setup_password_required). */
    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top */
    uint32_t flags = tl_read_uint32(&r);
    if (flags & (1u << 1)) { if (r.len - r.pos < 4) return -1; tl_read_int32(&r); }

    uint32_t user_crc = tl_read_uint32(&r);
    if (user_crc == TL_user || user_crc == TL_userFull) {
        tl_read_uint32(&r); /* user flags */
        if (r.len - r.pos >= 8) {
            int64_t uid = tl_read_int64(&r);
            if (user_id_out) *user_id_out = uid;
        }
    } else if (user_id_out) {
        *user_id_out = 0;
    }
    return 0;
}

int auth_2fa_srp_compute(const Account2faPassword *params,
                          const char *password,
                          const unsigned char *a_priv_in,
                          unsigned char A_out[SRP_PRIME_LEN],
                          unsigned char M1_out[32]) {
    if (!params || !password || !A_out || !M1_out) return -1;
    if (!params->has_password) return -1;
    return srp_compute(params, password, a_priv_in, A_out, M1_out);
}
