# FEAT-15 — tg-tui batch argv flags not wired (--logout, --phone, --code, --password)

## Gap
`src/main/tg_tui.c:print_help()` (lines 581–584) documents four
command-line flags:
```
--phone <number>   Batch login phone (E.164)
--code <digits>    Batch login SMS/app code
--password <pass>  Batch login 2FA password
--logout           Clear persisted session and exit
```
None of these are processed. `main()` only calls `has_tui_flag()` to
scan for `--tui`; all other argv entries are ignored. The interactive
`AuthFlowCallbacks` always prompt on the terminal regardless of what
was passed on the command line.

Specific failures:
- `tg-tui --logout` does NOT wipe the session; it starts the REPL.
- `tg-tui --phone +1... --code 12345` still prompts interactively.
- `man/tg-tui.1` documents these flags under GLOBAL FLAGS.

## Scope
1. Adopt `arg_parse()` in `tg_tui.c:main()` (or implement a minimal
   subset) to handle the four flags above, consistent with
   `tg_cli_ro.c` / `tg_cli.c`.
2. Wire `--logout` using the same pattern as `tg_cli_ro.c:767–800`.
3. Wire `--phone`/`--code`/`--password` into `BatchCreds`-style
   callbacks that fall back to interactive prompts when the flag is
   absent (so the interactive workflow is preserved).
4. Add a functional test asserting `--logout` clears the session file.

## Acceptance
- `tg-tui --logout` wipes `~/.config/tg-cli/session.bin` and exits 0.
- `tg-tui --phone +1X --code 12345` performs the login without a
  terminal prompt (mirrors tg-cli-ro batch mode).
- The interactive REPL/TUI remains the default when no batch flags
  are passed.
