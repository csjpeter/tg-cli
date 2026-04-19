# TEST-25 — functional test: watch exponential backoff on transient error

## Gap
`src/main/tg_cli_ro.c:cmd_watch()` implements exponential backoff
(5 s → 10 s → 20 s … capped at 300 s) when `domain_updates_difference`
returns an error (lines 201–211). This logic is **not tested** at any
level — neither unit tests nor functional tests cover the backoff
calculation or the "retry after error, succeed on second attempt"
scenario.

## Scope
1. Add `test_watch_backoff_then_succeed` in `test_read_path.c` (or a
   new `test_watch.c`):
   - First `getDifference` call: responder returns `rpc_error(500, "INTERNAL")`.
   - Second `getDifference` call: responder returns `updates_differenceEmpty`.
   - Call `domain_updates_difference` twice in sequence; assert the first
     returns non-zero and the second returns 0.
   - Verify `mt_server_rpc_call_count(CRC_updates_getDifference) == 2`.
2. This tests the domain-layer retry path without the sleep loop
   (the sleep is in `cmd_watch`, which is a main-binary concern).

## Acceptance
- Test passes under ASAN and Valgrind.
- The error-then-success path in `updates.c` is covered.
