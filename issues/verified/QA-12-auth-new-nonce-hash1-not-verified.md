# new_nonce_hash1 not verified in dh_gen_ok response

## Description
`auth_step_set_client_dh()` in `src/infrastructure/mtproto_auth.c` (lines 627-632)
reads `new_nonce_hash1` from the `dh_gen_ok` response but discards it with
`(void)new_nonce_hash`. The comment says "skip verification for now".

The MTProto spec **requires** verifying new_nonce_hash1:
```
new_nonce_hash1 = last 128 bits of SHA1(new_nonce + 0x01 + auth_key_aux_hash)
```
where `auth_key_aux_hash = first 8 bytes of SHA1(auth_key)`.

Without this check, a MITM attacker can substitute the auth_key during the
DH exchange without detection.

## Severity
CRITICAL — missing MITM protection in auth key exchange.

## Steps
1. Compute `auth_key_aux_hash = SHA1(auth_key)[0:8]`
2. Build buffer: `new_nonce(32) + 0x01(1) + auth_key_aux_hash(8)` = 41 bytes
3. Compute SHA1 of that buffer
4. Verify last 16 bytes match received `new_nonce_hash1`
5. Return -1 on mismatch
6. Add unit test for the verification

## Estimate
~20 lines code + ~30 lines tests

## Dependencies
P2-auth-key-exchange

## Verified — 2026-04-16
- `src/infrastructure/mtproto_auth.c::auth_step_set_client_dh` now
  computes `auth_key_aux_hash = SHA1(auth_key)[0:8]`, then
  `expected = SHA1(new_nonce || 0x01 || auth_key_aux_hash)[4:20]`
  and rejects the frame with a logged MITM warning when the value
  does not match the received `new_nonce_hash1`.
- `tests/unit/test_auth.c` — `build_dh_gen_ok` updated to ship the
  correct zero hash under mock crypto; new
  `test_set_client_dh_rejects_bad_new_nonce_hash` asserts a corrupt
  hash is rejected and `has_auth_key` stays 0.

Tests: 1824 -> 1826. Valgrind: 0 leaks. Zero warnings.
