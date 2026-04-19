# FEAT-11 — `upload` alias for `send-file` in tg-cli

## Gap
US-14 UX:
```
tg-cli send-file <peer> <path> [--caption TEXT]
tg-cli upload    <peer> <path> [--caption TEXT]    # alias
```
`arg_parse.c:359` only recognises `send-file`. The TUI REPL calls this
verb `upload`; the batch binary's two clients should match.

## Scope
1. `arg_parse.c` recognises both `send-file` and `upload` for
   `CMD_SEND_FILE`.
2. `tg_cli.c:print_usage` mentions the alias (DOC-02).
3. Small unit test for the alias parse.

## Alternative — if we prefer to prune
If we decide tg-tui should rename to `send-file` for consistency
instead, change `tg_tui.c:519` to dispatch on `send-file` (keep
`upload` as alias there) and update the US-14 UX block to drop the
batch `upload` alias. Decide before starting.

## Acceptance
- `tg-cli upload @peer ./foo.pdf` behaves identically to `send-file`.
- OR: tg-tui REPL `send-file` works too; US-14 kept in sync with
  whichever path is chosen.
