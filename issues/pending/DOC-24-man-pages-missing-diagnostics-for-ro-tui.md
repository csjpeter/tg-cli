# DOC-24 — tg-cli-ro.1 and tg-tui.1 missing DIAGNOSTICS sections

## Gap
`man/tg-cli.1` has a `.SH DIAGNOSTICS` section explaining common
error messages (e.g. "login failed (see logs)", "dc-migrate
unavailable").  The same section is missing from `tg-cli-ro.1`
and `tg-tui.1`, yet both emit analogous stderr messages:

- `tg-cli-ro: login failed (see logs)` — from `session_bringup`
- `tg-cli-ro: dialogs: failed (see logs)` — from every read command
- `tg-cli-ro watch: getDifference failed, retrying in Ns (backoff)`
- `tg-tui: cannot enter raw mode` — when terminal is not a TTY
- `tg-tui: cannot initialize TUI (size AxB)` — when size < 40x3

Users hitting these have no mapping from message → root cause or
log path.

## Scope
1. Add `.SH DIAGNOSTICS` to `man/tg-cli-ro.1` and `man/tg-tui.1`.
2. Each entry:
   - Verbatim stderr message (bolded).
   - Root cause explanation.
   - Log file to consult (`~/.cache/tg-cli/logs/`).
3. Keep the section alphabetical by stderr text so users can find
   lines via `man -k`.

## Acceptance
- Every unique `fprintf(stderr, ...)` line in `src/main/tg_cli_ro.c`
  and `src/main/tg_tui.c` is covered by a DIAGNOSTICS entry.
- CI grep check: at minimum, `grep -c fprintf.*stderr
  src/main/tg_cli_ro.c` equals the number of entries in the
  DIAGNOSTICS section (manual verification acceptable).
