# TEST-71 — Functional coverage for MTProto handshake (cold boot)

## Gap
`src/infrastructure/mtproto_auth.c` is 6 % functionally covered.
All current functional tests seed the auth_key via
`mt_server_seed_session`, skipping the real PQ/DH handshake.

## Scope
1. Mock server helper `mt_server_simulate_cold_boot(void)` that
   resets seeded state and arms responders for the handshake
   sequence: `req_pq_multi → resPQ`, `req_DH_params →
   server_DH_params_ok`, `set_client_DH_params → dh_gen_ok`.
2. Variant helpers for negative paths:
   - `mt_server_handshake_reply_dh_gen_retry(n)` — retry `n` times,
     then `dh_gen_ok`.
   - `mt_server_handshake_reply_dh_gen_fail()` — persistent fail.
   - `mt_server_handshake_mismatch_server_nonce()` — nonce tamper.
3. Tests in `tests/functional/test_handshake_cold_boot.c`:
   - `test_cold_boot_creates_auth_key`
   - `test_dh_gen_retry_then_ok`
   - `test_dh_gen_fail_persists_is_fatal`
   - `test_server_nonce_mismatch_refuses`

## Acceptance
- 4 tests pass under ASAN + Valgrind.
- Functional coverage of `mtproto_auth.c` ≥ 70 %.
- No regression in existing seeded-session tests.

## Dependencies
US-20 (the story). ADR-0007.
