# US-32 — Session file schema migration (v1 → v2, future v3)

Applies to: all three binaries.

**Status:** gap — `src/app/session_store.c` writes and reads the v2
multi-DC format. The v1 → v2 migration path exists in code but is
0 % functional covered (partially hit by unit tests only). Future
v3 upgrades will need the same pattern.

## Story
As an existing tg-cli user whose `~/.config/tg-cli/session.bin`
was created by an older release, I want a client upgrade to
recognise my v1 session, migrate it to v2 on first save, and keep
me logged in with no manual intervention.

## Uncovered practical paths
- **v1 file detected:** magic + version match v1 → load all fields
  into a v2 in-memory struct → subsequent save persists in v2
  format with mode 0600.
- **v2 load → immediate v1 rewrite refused:** if the user runs an
  older client, the newer format must be ignored cleanly (already
  the case for US-25 "unknown version").
- **Atomic migration:** if the save-v2 step fails after the load
  succeeded, the v1 file is not lost; re-running the client
  retries.
- **Forward-compat placeholder for v3:** write a regression test
  that asserts the v2 reader rejects v3 without clobbering.

## Acceptance
- New suite `tests/functional/test_session_migration.c`:
  - seed a hand-crafted valid v1 file, run bootstrap, assert the
    next save produces a v2 file equivalent to the seed entry.
  - simulate crash between load and save: truncate mid-save,
    assert v1 file still usable on retry.
  - v3 (fake future) seed → load fails, v1/v2 file left untouched.
- Functional coverage of `session_store.c` ≥ 98 % (combined with
  US-25 corruption tests).

## Dependencies
US-16 (session format), US-25 (corruption). Together these close
the session-file contract.
