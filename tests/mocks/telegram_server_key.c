/**
 * @file telegram_server_key.c  [TEST_ONLY]
 * @brief Test-only RSA key pair for functional DH handshake tests.
 *
 * WARNING: TEST_ONLY — NEVER use these keys in production.
 *
 * This 2048-bit RSA key pair was generated solely for testing purposes.
 * The public key is embedded here so the production fingerprint check
 * passes; the private key is embedded in mock_tel_server.c so the mock
 * server can decrypt the client's RSA_PAD-encrypted inner_data and
 * complete the full DH handshake with real OpenSSL on both sides.
 *
 * Key fingerprint computation:
 *   SHA1(n_LE_4bytes_len + n_bytes + e_LE_4bytes_len + e_bytes)
 *   lower 64 bits (little-endian) = 0x8671de275f1cabc5
 *
 * Linked by: functional-test-runner
 * NOT linked by: tg-cli, tg-cli-ro, tg-tui, or any unit test runner.
 *
 * FEAT-38: also implements telegram_server_key_get_pem(),
 * telegram_server_key_get_fingerprint(), and telegram_server_key_set_override()
 * so the mock shadows the entire production translation unit.
 */

#include "telegram_server_key.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* TEST_ONLY: 2048-bit RSA public key fingerprint.
 * Computed as lower 64 bits of SHA1(n_bytes_LE_len+n+e_bytes_LE_len+e). */
const uint64_t TELEGRAM_RSA_FINGERPRINT = 0x8671de275f1cabc5ULL;

/* TEST_ONLY: 2048-bit RSA public key (PKCS#8 SubjectPublicKeyInfo format). */
const char * const TELEGRAM_RSA_PEM =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAmxv4/EXb0wAFr/O9GshQ\n"
    "mySO93xBAeN/5fzZ6HGHgIfS/2XL/R8GMBTl5rPaNnHL6fnv+BhOeb1M2PF6zNYe\n"
    "nbQpRmDb0Tab3LYX5la6RhiIyh9m97J4qeGo7VDnGSMk8p2aNbzBubqgsWGl1soQ\n"
    "cDDyUPOxsOVm3GijSVoN42dRiNIPrSVAKl6Xz8BdyoysyGcGv625yfYnJDl9djmh\n"
    "tXNnp1tfrL9Stas+gnMZmwskbg/sVClUyx3OcpJnGAyddgixEvA3X2zzrVBgD9PN\n"
    "gkDqbX6T5uX751OnOd7LiCLZ3XY4k++PockAV0Kb1lyiGcxRHYMSdkFM6U5UcKlY\n"
    "owIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

/* ---- FEAT-38 runtime override (test stub) ---- */

static char    *g_mock_override_pem         = NULL;
static uint64_t g_mock_override_fingerprint = 0;

const char *telegram_server_key_get_pem(void) {
    return g_mock_override_pem ? g_mock_override_pem : TELEGRAM_RSA_PEM;
}

uint64_t telegram_server_key_get_fingerprint(void) {
    return g_mock_override_pem
               ? g_mock_override_fingerprint
               : TELEGRAM_RSA_FINGERPRINT;
}

/**
 * Expand literal \\n → '\n' in @p src; return heap-allocated result.
 * (Duplicated from production telegram_server_key.c — kept private.)
 */
static char *mock_expand_newlines(const char *src) {
    size_t src_len = strlen(src);
    char *dst = malloc(src_len + 1);
    if (!dst) return NULL;
    size_t di = 0;
    for (size_t si = 0; si < src_len; si++) {
        if (src[si] == '\\' && si + 1 < src_len && src[si + 1] == 'n') {
            dst[di++] = '\n';
            si++;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
    return dst;
}

int telegram_server_key_set_override(const char *pem) {
    /* In functional tests we accept any non-NULL PEM and store it.
     * Fingerprint computation is skipped (we hard-code the test key's
     * known fingerprint for the override path, or just reuse the mock's). */
    if (!pem) {
        free(g_mock_override_pem);
        g_mock_override_pem = NULL;
        g_mock_override_fingerprint = 0;
        return 0;
    }
    char *expanded = mock_expand_newlines(pem);
    if (!expanded) return -1;
    free(g_mock_override_pem);
    g_mock_override_pem = expanded;
    /* Reuse the mock fingerprint so the handshake still works when the
     * caller sets the mock PEM verbatim. */
    g_mock_override_fingerprint = TELEGRAM_RSA_FINGERPRINT;
    return 0;
}
