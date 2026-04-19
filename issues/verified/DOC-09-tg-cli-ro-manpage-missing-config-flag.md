# DOC-09 — tg-cli-ro man page GLOBAL FLAGS missing --config

## Gap
`man/tg-cli-ro.1` GLOBAL FLAGS section does not document `--config <path>`,
even though `src/main/tg_cli_ro.c:print_usage()` (line 739) explicitly
lists it and `src/core/arg_parse.c` parses it into `ArgResult.config_path`.
A user reading the man page has no way to discover the non-default config
path feature.

## Scope
- Add a `.TP` entry for `--config <path>` in the `.SH GLOBAL FLAGS` section
  of `man/tg-cli-ro.1`, immediately after the `--json` entry, matching the
  wording already present in `print_usage()`:
  "Use a non-default config file path (default: `~/.config/tg-cli/config.ini`)."

## Acceptance
- `groff -man -Tutf8 man/tg-cli-ro.1 | grep -q '\-\-config'` exits 0.
- `man/tg-cli-ro.1` renders clean under `groff -man` with no warnings.
- Cross-grep: every `--config` reference in `print_usage()` is mirrored
  in the man page.
