# TEST-70 — Functional coverage for cross-DC auth transfer

## Gap
`src/infrastructure/auth_transfer.c` is 0 % functionally covered
despite being critical to US-15 (cross-DC media routing). Unit tests
alone do not exercise the mock-server-driven export/import handshake.

## Scope
1. Extend `tests/mocks/mock_tel_server.c` with:
   - a second virtual DC (`mt_server_set_active_dc(id)`),
   - `auth.exportAuthorization` responder emitting
     `auth.exportedAuthorization { id, bytes }`,
   - `auth.importAuthorization` responder emitting
     `auth.authorization` (and a toggle for
     `auth.authorizationSignUpRequired`).
2. New test file `tests/functional/test_cross_dc_auth_transfer.c`:
   - `test_export_import_happy` — normal case, asserts export CRC
     fires once and import CRC fires once; retry of `upload.getFile`
     succeeds.
   - `test_import_signup_required_is_distinct_error` — sign-up
     sentinel returns a specific, documented error code.
   - `test_second_migrate_reuses_cached_auth_key` — same DC second
     migrate must NOT re-issue export/import.
3. CMakeLists registration.

## Acceptance
- 3 tests pass under ASAN + Valgrind.
- Functional coverage of `auth_transfer.c` ≥ 80 %.

## Dependencies
US-19 (the story). ADR-0007 (mock server) — the new responders are
additive; no behavioural change to existing tests.
