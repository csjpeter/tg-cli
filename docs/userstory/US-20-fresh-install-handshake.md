# US-20 — Fresh-install MTProto handshake, functional coverage

Applies to: all three binaries.

**Status:** gap — `src/infrastructure/mtproto_auth.c` is 6 % functional
covered. Unit tests cover the PQ / DH math. Every functional test
currently *seeds* a session via `mt_server_seed_session`, so the
full `req_pq_multi → server_DH_params → set_client_DH_params →
dh_gen_ok` handshake is never exercised end-to-end against the mock
server.

## Story
As a new user installing tg-cli on a clean machine (no
`~/.config/tg-cli/session.bin`), I want the first connect to finish
the MTProto 2.0 Diffie-Hellman handshake and persist the resulting
auth_key so every subsequent run reuses it instantly.

## Uncovered practical paths
- **Cold boot (seedless):** `session.bin` does not exist → handshake
  runs → auth_key is derived from scratch and stored.
- **`dh_gen_retry`:** server rejects our `set_client_DH_params` with
  the "try again" sentinel → client retries once with a new random b.
- **`dh_gen_fail`:** persistent DH rejection → client aborts with a
  clear, actionable error (not a cryptic "handshake error 3").
- **Nonce mismatch:** server nonce echoed wrongly → client detects
  MITM attempt and refuses to persist anything.
- **server_salt rotation:** post-handshake salt drift across runs.

## Acceptance
- Mock server gains a `mt_server_simulate_cold_boot()` helper that
  reverts seeded state and plays the full PQ/DH response sequence.
- New functional test `tests/functional/test_handshake_cold_boot.c`
  - asserts `session.bin` is absent at start.
  - drives one RPC (e.g. `help.getConfig`) that triggers the
    full handshake.
  - asserts `session.bin` now exists and `auth_key_id` != 0.
- Matching negative tests for `dh_gen_retry` and `dh_gen_fail`.
- Functional coverage of `mtproto_auth.c` ≥ 70 % (from 6 %).

## Dependencies
US-16 session management, US-03 login flow. Mock-server extension
belongs with ADR-0007.
