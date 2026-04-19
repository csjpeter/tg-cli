# TEST-67 — Functional test: FLOOD_WAIT honored across send + getHistory

## Gap
`test_send_message_flood_wait` (verified) confirms `send` handles
FLOOD_WAIT_X by bubbling the error.  It does NOT verify:

1. The CLI honors the `retry_after` value and sleeps before the next
   send (today the client surfaces the error and exits — user must
   retry manually).
2. Read commands (`history`, `search`) surface FLOOD_WAIT clearly
   rather than silently returning empty results.
3. TUI mode handles FLOOD_WAIT on poll cycle (exponential backoff is
   already tested via TEST-25 — verify it triggers FOR the flood
   case specifically).

## Scope
Create `tests/functional/test_flood_wait_contract.c`:
- Case 1: `domain_get_history` with server returning
  `FLOOD_WAIT(5)` → rc != 0, `err.error_code == 420`, `err.error_msg`
  starts with "FLOOD_WAIT_5".
- Case 2: `cmd_history` prints "rate limited, retry in 5s" to
  stderr.
- Case 3: TUI mode's poll loop detects the FLOOD_WAIT and backs off
  for `max(backoff, retry_after)`.

## Acceptance
- 3 cases pass.
- Document the contract in a new subsection in `docs/dev/mtproto-reference.md`.
