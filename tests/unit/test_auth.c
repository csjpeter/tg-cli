/**
 * @file test_auth.c
 * @brief Unit tests for MTProto auth key generation.
 *
 * Tests PQ factorization (Pollard's rho) with known products.
 * Full DH exchange requires integration test with real/fake server.
 */

#include "test_helpers.h"
#include "mtproto_auth.h"
#include "mock_crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- PQ Factorization ---- */

void test_pq_factorize_simple(void) {
    uint32_t p, q;
    /* 3 * 7 = 21 */
    int rc = pq_factorize(21, &p, &q);
    ASSERT(rc == 0, "factorize 21 should succeed");
    ASSERT(p == 3, "p should be 3");
    ASSERT(q == 7, "q should be 7");
}

void test_pq_factorize_larger(void) {
    uint32_t p, q;
    /* 101 * 103 = 10403 */
    int rc = pq_factorize(10403, &p, &q);
    ASSERT(rc == 0, "factorize 10403 should succeed");
    ASSERT(p == 101, "p should be 101");
    ASSERT(q == 103, "q should be 103");
}

void test_pq_factorize_product_of_large_primes(void) {
    uint32_t p, q;
    /* 65537 * 65521 = 4295098177 — typical MTProto pq size */
    uint64_t pq = (uint64_t)65537 * 65521;
    int rc = pq_factorize(pq, &p, &q);
    ASSERT(rc == 0, "factorize 4295098177 should succeed");
    ASSERT(p == 65521, "p should be 65521");
    ASSERT(q == 65537, "q should be 65537");
}

void test_pq_factorize_small_primes(void) {
    uint32_t p, q;
    /* 2 * 3 = 6 */
    int rc = pq_factorize(6, &p, &q);
    ASSERT(rc == 0, "factorize 6 should succeed");
    ASSERT(p == 2, "p should be 2");
    ASSERT(q == 3, "q should be 3");
}

void test_pq_factorize_unequal_primes(void) {
    uint32_t p, q;
    /* 7 * 53 = 371 */
    int rc = pq_factorize(371, &p, &q);
    ASSERT(rc == 0, "factorize 371 should succeed");
    ASSERT(p == 7, "p should be 7");
    ASSERT(q == 53, "q should be 53");
}

void test_pq_factorize_invalid(void) {
    uint32_t p, q;
    ASSERT(pq_factorize(0, &p, &q) == -1, "factorize 0 should fail");
    ASSERT(pq_factorize(1, &p, &q) == -1, "factorize 1 should fail");
    ASSERT(pq_factorize(7, &p, &q) == -1, "factorize prime 7 should fail");
    ASSERT(pq_factorize(49, &p, &q) == -1, "factorize 7*7 (equal primes) should fail");
}

void test_pq_factorize_null(void) {
    uint32_t p;
    ASSERT(pq_factorize(21, NULL, &p) == -1, "NULL p should fail");
    ASSERT(pq_factorize(21, &p, NULL) == -1, "NULL q should fail");
}

void test_pq_factorize_mtproto_sized(void) {
    /* MTProto uses pq that fits in 64 bits, product of two ~32-bit primes.
       Example: 0x4A89CDB5 * 0x7DCCD65D (made-up large primes) */
    uint32_t p, q;
    /* Use a known factorization: 1234567891 * 1234567891 — actually that's p^2.
       Use: 1073741789 * 1073741827 = 1152918994292586403 */
    uint64_t pq = (uint64_t)1073741789ULL * 1073741827ULL;
    int rc = pq_factorize(pq, &p, &q);
    ASSERT(rc == 0, "factorize large product should succeed");
    ASSERT(p * q == pq, "p * q should equal original pq");
    ASSERT(p <= q, "p should be <= q");
}

/* ---- Test suite entry point ---- */

void test_auth(void) {
    RUN_TEST(test_pq_factorize_simple);
    RUN_TEST(test_pq_factorize_larger);
    RUN_TEST(test_pq_factorize_product_of_large_primes);
    RUN_TEST(test_pq_factorize_small_primes);
    RUN_TEST(test_pq_factorize_unequal_primes);
    RUN_TEST(test_pq_factorize_invalid);
    RUN_TEST(test_pq_factorize_null);
    RUN_TEST(test_pq_factorize_mtproto_sized);
}
