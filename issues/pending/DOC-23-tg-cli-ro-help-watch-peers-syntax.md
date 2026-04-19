# DOC-23 — tg-cli-ro help: expand --peers syntax and --interval range

## Gap
`tg-cli-ro --help` currently says:
```
  watch    [--peers X,Y] [--interval SEC]  Watch updates (US-07)
```
It does not tell the user:
- `X,Y` tokens may be `@username`, numeric id (possibly negative for
  legacy chats), or the literal string `self`.
- `--interval` must be in [2, 3600] seconds.
- Empty list or unknown peer → hard error with stderr message.
- `self` matches messages whose `peer_id == 0` in
  `updates.getDifference` (Saved Messages).

## Scope
1. Expand the `watch` line in `print_usage()` at `tg_cli_ro.c:886-927`:
   ```
   watch  [--peers LIST] [--interval SEC]  Watch live updates (US-07)
     LIST    comma-separated tokens: @username, numeric id, or 'self'
     SEC     [2..3600]; default 30
     --json  emit one NDJSON object per new message (see OUTPUT FORMAT)
   ```
2. Sync the same expansion into `man/tg-cli-ro.1`.
3. Add one worked example to `.SH EXAMPLES` in the man page:
   `tg-cli-ro watch --peers @alice,self --interval 10 --json | jq`.

## Acceptance
- Help output includes the token grammar and the range.
- Man page examples include a filter example.
- Regression caught: if `WATCH_PEERS_MAX` is ever changed, the help
  doesn't need a rebuild — the man page links to the constant's
  doxygen reference.
