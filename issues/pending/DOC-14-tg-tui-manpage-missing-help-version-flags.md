# DOC-14 — tg-tui man page missing --help and --version flags

## Gap
`man/tg-tui.1` GLOBAL FLAGS section lists only `--tui`, `--phone`,
`--code`, `--password`, and `--logout`. It omits `--help`/`-h` and
`--version`/`-v`, which are implemented in the sibling binaries
(`tg-cli-ro` and `tg-cli`) via `arg_parse`.

`src/main/tg_tui.c` currently does **not** use `arg_parse` at all —
it only scans for `--tui` manually (`has_tui_flag`). Therefore:
- `tg-tui --help` currently silently ignores the flag and starts the
  interactive login flow, rather than printing usage and exiting.
- `tg-tui --version` behaves identically.

This is both a doc gap (man page silent) and a UX gap (flags ignored).

## Scope
1. Wire `arg_parse` (or a minimal subset) into `tg_tui.c:main()` to
   handle `--help`/`-h` (print usage, exit 0) and `--version`/`-v`
   (print version string, exit 0) before starting the login flow.
2. Update `man/tg-tui.1` GLOBAL FLAGS to document both flags.
3. Update `print_help()` in `tg_tui.c` to mention that `--help` works
   from the command line (distinct from the REPL `help` command).

## Acceptance
- `tg-tui --help` prints usage text and exits 0 without prompting for
  a phone number.
- `tg-tui --version` prints the version string and exits 0.
- `man/tg-tui.1` GLOBAL FLAGS section lists `--help` and `--version`.
