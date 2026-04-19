# PTY-06 — PTY test: tg-cli-ro dialogs output sanitization on real TTY

## Gap
`tg_cli_ro.c:58-77` has `tty_sanitize()` which replaces control bytes
(< 0x20 except \t, \n), DEL (0x7F), and 8-bit CSI (0x9B) with '.' —
but only when `isatty(STDOUT_FILENO)`.  This code path is never
exercised by any functional test because every existing FT writes to
a regular pipe (not a TTY).

SEC-01 (ANSI escape injection) was fixed in the TUI pane layer, but
the batch `tg-cli-ro` binary has its own independent sanitizer for
`dialogs`/`history`/`search`/`user-info`/`contacts`/`me` outputs.
Without a PTY test, a regression here would silently land.

## Scope
1. Add `tests/functional/pty/test_tg_cli_ro_dialogs_tty.c`:
   - Seed a mock session that emits one dialog whose title contains
     raw ESC `\x1b[31m` and DEL `\x7f` bytes.
   - Spawn `bin/tg-cli-ro dialogs --batch` under a PTY master.
   - Read the child's output via the PTY master (so `isatty` is true).
   - Assert the output contains `.` replacement bytes and no raw ESC.
   - Assert exit code 0.
2. Add a parallel test that writes to a plain pipe (non-TTY) and
   verifies bytes pass through unchanged for scripting.

## Acceptance
- Both tests pass under ASAN.
- Regression in `tty_sanitize` (e.g. forgetting to sanitize 0x9B)
  causes the PTY test to fail.

## Dependencies
- Depends on PTY-01 (libptytest adopted).
- Uses `pty_tel_stub` pattern from existing `tests/functional/pty/`.
