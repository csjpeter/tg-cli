# TEST-28 — functional test: dialogs --archived flag sends folder_id=1

## Gap
US-04 acceptance: "Correctly unwraps `messages.dialogsSlice` vs
`messages.dialogs` vs `messages.dialogsNotModified`."
US-04 UX: `dialogs [--limit N] [--archived]` (passes `folder_id=1`).

`src/domain/read/dialogs.c` presumably passes `folder_id` when
`archived != 0`. Existing functional tests (`test_read_path.c`) cover
`messages.dialogs` and `messages.dialogsNotModified` but **none** test
the `--archived` path at the functional level (only the arg-parse unit
test verifies the flag is stored, not that it reaches the wire).

## Scope
Add `test_dialogs_archived_folder_id` to `tests/functional/test_read_path.c`:
1. Capture the raw `messages.getDialogs` request in a responder.
2. Decode the `folder_id` field from the captured bytes.
3. Call `domain_get_dialogs` with `archived=1`; assert `folder_id == 1`.
4. Call again with `archived=0`; assert `folder_id == 0`.

## Acceptance
- Test passes under ASAN and Valgrind.
- Confirms the `--archived` flag wire encoding is correct.
