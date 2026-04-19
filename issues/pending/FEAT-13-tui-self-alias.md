# FEAT-13 — `self` alias for `me` in tg-tui REPL

## Gap
US-05 UX:
```
tg-cli-ro me [--json]
tg-cli-ro self           # alias
```
The tg-cli-ro batch parser already accepts both (`arg_parse.c:315`).
The tg-tui REPL main loop only dispatches on `me`, leaving `self`
unmatched.

## Scope
1. Extend the REPL match at `tg_tui.c:473` to accept `self` too.
2. Update `print_help` + `man/tg-tui.1` (DOC-03).
3. (Trivial test not strictly required — REPL parse path is covered
   by `test_tui_app.c`.)

## Acceptance
- `tg> self` produces the same output as `tg> me`.
