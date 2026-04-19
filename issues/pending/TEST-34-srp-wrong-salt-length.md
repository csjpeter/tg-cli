# TEST-34: 2FA: no negative test for server sending out-of-range SRP salt lengths

## Location
`src/infrastructure/auth_2fa.c` – `parse_password_kdf_algo()`
`tests/unit/test_auth_2fa.c`

## Problem
`parse_password_kdf_algo` checks `s1 > SRP_SALT_MAX || s2 > SRP_SALT_MAX` and
returns -1, but there are no tests for this guard.  Specifically:

1. Server returns `salt1` of length `SRP_SALT_MAX + 1` → should return -1.
2. Server returns `p` (prime) of length ≠ 256 (`SRP_PRIME_LEN`) → should return
   -1 via the `p_len != SRP_PRIME_LEN` check.
3. Server returns `has_password=true` but zero-length `srp_B` → should return
   -1 via the `srpB_len == 0` check.

The existing `test_auth_2fa.c` tests only the happy path (correct SRP
round-trip).  Malformed server responses during 2FA setup are untested.

## Add to
`tests/unit/test_auth_2fa.c`
