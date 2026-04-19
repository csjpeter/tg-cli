# TEST-61 — Functional test: media_index survives process restart

## Gap
`media_index` (referenced in `cmd_download` and `cmd_history`) caches
`{media_id → local_path}` so the history pane can show inline file
paths for previously-downloaded media.  No FT covers:

1. After `media_index_put(42, "/tmp/pwn.jpg")`, spawning a new
   process and calling `media_index_get(42, ...)` returns `1`
   and populates the same path.
2. If the on-disk media_index file is corrupted, subsequent
   lookups return `0` (miss) rather than returning stale memory.
3. Concurrent writes (two `cmd_download` calls in parallel) don't
   corrupt the index.

## Scope
Create `tests/functional/test_media_index_persistence.c`:
- Case 1: put + fork + get.  Child gets 1 and matching path.
- Case 2: corrupt the index file after put; get returns 0.
- Case 3: two forks both call put; last-writer-wins, no file
  corruption, no duplicate entries.

## Acceptance
- All 3 cases pass under ASAN.
- Index file uses the same flock + atomic rename pattern as session
  store (FEAT-20) — if not, spawn a FEAT ticket.

## Dependencies
- FEAT-20 (flock infrastructure), TEST-35 (concurrent write).
