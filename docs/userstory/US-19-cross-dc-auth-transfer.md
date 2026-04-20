# US-19 — Cross-DC authorization transfer, end-to-end functional coverage

Applies to: tg-cli, tg-cli-ro (tg-tui inherits).

**Status:** gap — `src/infrastructure/auth_transfer.c` ships the flow
(P10-04) but functional coverage is 0 %. Unit tests exist
(`tests/unit/test_auth_transfer.c`); no functional test drives the
full export→import round trip through the mock server.

## Story
As a user whose home DC is DC2, I want to reach a message, photo, or
document that Telegram has placed on DC4 without ever being asked to
re-login. The client must exchange a short-lived authorization token
with the foreign DC transparently so my session is trusted there.

## Current flow (should stay invisible)
1. Home DC RPC returns `FILE_MIGRATE_4` (or `NETWORK_MIGRATE_4`,
   `USER_MIGRATE_4`, `PHONE_MIGRATE_4`).
2. `dc_session_ensure_authorized(4)` opens DC4, runs DH handshake if
   the auth_key is not cached, then:
   - on home DC: `auth.exportAuthorization(dc_id=4)` →
     `auth.exportedAuthorization { id, bytes }` (lives a few seconds).
   - on DC4: `auth.importAuthorization(id, bytes)` →
     `auth.authorization { user }` (or `…SignUpRequired` for the
     signup edge case).
3. DC4 session is now authorized; the retry of the original RPC
   succeeds.

## Uncovered practical paths
- **Token expiry race:** DC4 handshake takes longer than the token's
  server-side TTL → `AUTH_KEY_INVALID`-shaped error on import → should
  re-export once, then fail loudly if it still does not work.
- **`authorizationSignUpRequired`**: foreign DC replies with the
  "fresh account" sentinel (`0x44747e9a`). Currently we treat anything
  that is not `auth.authorization` as fatal; the signup sentinel
  deserves a dedicated, translatable error message pointing the user
  at the login wizard.
- **Second migrate in one session:** home DC RPC returns migrate twice
  in a row (e.g. the file is moved mid-transfer); the cached token
  from round 1 must not be reused for round 2.

## Acceptance
- New functional test `tests/functional/test_cross_dc_auth_transfer.c`
  drives a mock with two DCs and asserts:
  - `mt_server_request_crc_count(CRC_auth_exportAuthorization) == 1`
  - `mt_server_request_crc_count(CRC_auth_importAuthorization) == 1`
  - `getFile` retry succeeds after import.
  - On `…SignUpRequired` the domain layer surfaces a distinct error
    code that tg-cli prints as `cross-dc: account not registered on
    DC<n>, run \`tg-cli login\``.
- Functional coverage of `auth_transfer.c` ≥ 80 %.
- Man page: `man/tg-cli.1` §DIAGNOSTICS gains a one-liner about the
  new error.

## Dependencies
US-08, US-14, US-15. Unit test in `tests/unit/test_auth_transfer.c`
stays as the micro-coverage baseline.
