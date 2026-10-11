/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-11 */
/* config.ini line the PTY tests give tg-tui: a test-only RSA public key. */

#ifndef TESTS_PTY_TEST_CONFIG_H
#define TESTS_PTY_TEST_CONFIG_H

/**
 * @file pty_test_config.h
 * @brief The `rsa_pem` line of the PTY tests' config.ini.
 *
 * The binary refuses to start without a configured RSA public key (it ships
 * none).  The stub server seeds a session, so no handshake happens and the
 * key is never used to encrypt; it only has to be a valid PEM.  This is the
 * test-only key of tests/mocks/telegram_server_key.c, written the way
 * config.ini wants it: one line, with literal "\n" sequences.
 */
#define PTY_TEST_RSA_INI_LINE \
    "rsa_pem = -----BEGIN PUBLIC KEY-----\\n" \
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAmxv4/EXb0wAFr/O9GshQ\\n" \
    "mySO93xBAeN/5fzZ6HGHgIfS/2XL/R8GMBTl5rPaNnHL6fnv+BhOeb1M2PF6zNYe\\n" \
    "nbQpRmDb0Tab3LYX5la6RhiIyh9m97J4qeGo7VDnGSMk8p2aNbzBubqgsWGl1soQ\\n" \
    "cDDyUPOxsOVm3GijSVoN42dRiNIPrSVAKl6Xz8BdyoysyGcGv625yfYnJDl9djmh\\n" \
    "tXNnp1tfrL9Stas+gnMZmwskbg/sVClUyx3OcpJnGAyddgixEvA3X2zzrVBgD9PN\\n" \
    "gkDqbX6T5uX751OnOd7LiCLZ3XY4k++PockAV0Kb1lyiGcxRHYMSdkFM6U5UcKlY\\n" \
    "owIDAQAB\\n" \
    "-----END PUBLIC KEY-----\\n\n"

#endif /* TESTS_PTY_TEST_CONFIG_H */
