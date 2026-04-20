# TEST-83 — Functional coverage for session schema v1 → v2 migration

## Gap
`src/app/session_store.c` has migration code for v1 → v2 but no
functional test exercises a v1-on-disk file being loaded by the
current binary. TEST-76 covers corruption, not schema upgrade.

## Scope
New suite `tests/functional/test_session_migration.c`:

1. `test_v1_file_loads_into_v2_in_memory` — seed a hand-crafted
   v1 session.bin with a known auth_key + dc_id; bootstrap loads
   it; `session_store_load` returns dc_id intact.
2. `test_v1_file_rewritten_as_v2_on_save` — after the load above,
   a save produces a v2-shaped file (check magic + version byte).
3. `test_crash_between_v1_load_and_v2_save_keeps_v1` — simulate
   atomic-rename failure (remove write permission on the parent
   dir); v1 file remains; next run retries.
4. `test_future_v3_file_rejected_without_clobber` — fake v3 magic;
   load fails, file left untouched, stderr "unknown session
   version — upgrade client".

Fixture helpers for v1 byte layout live in
`tests/common/session_fixture.c`.

## Acceptance
- 4 tests pass under ASAN.
- Functional coverage of `session_store.c` ≥ 98 %.

## Dependencies
US-32 (the story). TEST-76 (corruption) covers the other half of
file-format safety.
