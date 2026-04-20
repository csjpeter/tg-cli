# TEST-76 — Functional coverage for session file corruption

## Gap
`src/app/session_store.c` is 82 % functionally covered. The
corruption and adversarial-state paths from US-25 are not tested
end-to-end.

## Scope
New suite `tests/functional/test_session_corruption.c`:

1. `test_truncated_session_refuses_load` — write first 8 bytes of a
   valid file, assert `session_store_load` returns ≠0 and stderr
   notes "truncated".
2. `test_bad_magic_refuses_load` — overwrite header magic, same
   assertion with distinct error text.
3. `test_unknown_version_refuses_load_and_does_not_overwrite` —
   version byte set to an impossible high value; load fails, and a
   subsequent save does NOT clobber (user must `tg-cli logout`
   manually, acting as a defensive barrier against data loss on
   older clients).
4. `test_concurrent_writers_both_correct` — fork, both processes
   call `session_store_save` simultaneously; assert exactly one
   entry per dc_id.
5. `test_stale_tmp_leftover_ignored` — create `session.bin.tmp`
   manually, run save; assert clean final file.
6. `test_mode_drift_corrected_on_save` — chmod 0644, run save,
   assert final mode is 0600.

## Acceptance
- 6 tests pass under ASAN + Valgrind.
- Functional coverage of `session_store.c` ≥ 95 %.

## Dependencies
US-25 (the story). US-16 (session management).
