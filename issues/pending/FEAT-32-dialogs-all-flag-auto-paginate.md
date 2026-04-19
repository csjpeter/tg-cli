# FEAT-32 — dialogs --all auto-paginates past the 100-entry cap

## Gap
See TEST-68: users with > 100 dialogs need multiple calls.  Adding
`--all` (or `--no-limit`) that internally loops
`messages.getDialogs` with `offset_peer` / `offset_id` cursors until
the server reports the end would make scripting easier.

## Scope
1. Add `--all` flag to `arg_parse.c`.  Semantics:
   - Ignore `--limit`.
   - Fetch pages of 100 until `messages.dialogsSlice.count` is
     reached OR returned slice is empty.
   - Emit one combined list (or NDJSON for `--json`).
2. Add a safety cap (e.g. 5000 total) to avoid runaway loops on
   misbehaving servers.
3. Paired test: `tests/functional/test_dialogs_all.c`:
   - Mock returns 3 pages of 100 with count=250.
   - Assert stdout has exactly 250 rows.
   - Assert exactly 3 RPCs issued.

## Acceptance
- `--all` ignores `--limit`.
- `--all --json` emits NDJSON (streamable).
- Max total = 5000 (hard-coded).
- Man page + help updated.

## Dependencies
- TEST-68 frames the contract.
