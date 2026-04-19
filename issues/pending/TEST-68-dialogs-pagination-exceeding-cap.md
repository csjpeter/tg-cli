# TEST-68 — Functional test: dialogs list pagination past the 100 cap

## Gap
`cmd_dialogs` clamps limit to 100.  Users with > 100 dialogs cannot
see the rest without another call.  There is no FT for the
paginated scenario:

1. Server returns `messages.dialogsSlice` with `count=250`.
2. The CLI should either:
   A. Loop internally to stitch pages until `limit` is served.
   B. Emit the first 100 with a stderr note "only first 100 shown,
      use --limit 200 for more".

Today the code appears to do (B) silently — no stderr note.

## Scope
1. DECIDE the contract (A vs B).  Propose B for now (aligns with
   `--limit` explicit).
2. Add the stderr note in `cmd_dialogs` when the server's `count`
   exceeds the returned slice AND `--limit` was not specified.
3. Add `tests/functional/test_dialogs_pagination.c`:
   - Case: server returns 100 entries with `count=250`.
   - Assert stdout has 100 rows.
   - Assert stderr has "only first 100 shown; use --limit 200 to
     see more".
4. Optional follow-up: add `--all` flag (spawn FEAT ticket).

## Acceptance
- Pagination hint visible to users with > 100 dialogs.
- Regression-catching FT in place.
