# US-25 — Session file corruption recovery

Applies to: all three binaries.

**Status:** gap — `src/app/session_store.c` is 82 % functional
covered. Happy-path save/load, multi-DC upserts, and the happy
`lock_file` path are exercised. Corruption and adversarial states
are not.

## Story
As a user whose machine crashed, lost power, or ran out of disk
while tg-cli was writing `session.bin`, I want the next run to either
recover the session cleanly or tell me exactly how to reset, never
dump a partial file into production and never silently sign me out.

## Uncovered practical paths
- **Truncated file:** `session.bin` exists but is shorter than the
  header → detect, refuse to load, log a warning, continue as
  logged-out.
- **Corrupt CRC / magic:** leading bytes don't match the v2 magic →
  same as above, with distinct error text.
- **Unknown version:** file has a newer `version` byte we don't
  understand → refuse to overwrite, tell the user to upgrade the
  client.
- **Concurrent writers:** two tg-cli processes try to save at the
  same instant → `flock` keeps both correct; after both exit,
  exactly one DC entry per `dc_id` survives.
- **Stale `.tmp` leftover:** `session.bin.tmp` present from a
  prior crash → harmless; atomic rename flow still works.
- **Mode drift:** user `chmod 644 session.bin` → next save restores
  mode 0600 (defence in depth).

## Acceptance
- New functional test `tests/functional/test_session_corruption.c`
  seeds each damaged variant and asserts:
  - `session_store_load` returns ≠ 0.
  - stderr / log contains the corresponding diagnostic.
  - A subsequent save completes and resulting file is mode 0600.
- Functional coverage of `session_store.c` ≥ 95 % (from 82 %).
- Man pages §FILES get a one-liner: "corrupt session.bin is
  automatically ignored; run `tg-cli login` to rebuild it".

## Dependencies
US-16 session management (session.bin format). FEAT-34 log
rotation is adjacent but separate.
