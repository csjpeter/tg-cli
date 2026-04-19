# TEST-65 — PTY test: tg-tui --logout interactive flow

## Gap
FEAT-12 (verified) added server-side logout RPC + local wipe.  In
`tg-tui`, `--logout` is handled before login — so a fresh user who
hits `tg-tui --logout` ends up with "persisted session cleared"
stderr regardless of whether a session ever existed.  This is fine
as a no-op, but no PTY test confirms:

1. Fresh user (no ~/.config/tg-cli/session.bin): clean "persisted
   session cleared." and exit 0, no crash on missing file.
2. Existing session: server RPC issued, then file wiped, then
   exit 0.
3. Server RPC fails (mock returns RpcError): local wipe still
   succeeds, stderr has a fallback message, exit 0.

## Scope
Add `tests/functional/pty/test_tg_tui_logout_flow.c`:
- Cases 1, 2, 3 above.
- Verify via PTY master that stderr ends with
  "persisted session cleared" in every case.
- Verify file `~/.config/tg-cli/session.bin` is gone after.
- Case 2 verifies `mt_server_rpc_call_count() >= 1` for
  `auth.logOut`.

## Acceptance
- 3 scenarios pass under ASAN.
- No hang on the "missing file" case.

## Dependencies
- PTY-01, FEAT-12, TEST-23.
