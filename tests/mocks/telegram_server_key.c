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
 */

#include "telegram_server_key.h"

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
