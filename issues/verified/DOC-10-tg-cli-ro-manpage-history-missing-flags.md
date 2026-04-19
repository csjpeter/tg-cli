# DOC-10 — tg-cli-ro man page: history subcommand missing --offset and --no-media

## Gap
`man/tg-cli-ro.1` documents the `history` subcommand as:
```
history <peer> [--limit N]
```
but `src/main/tg_cli_ro.c:print_usage()` (line 729) and the actual
implementation (`tg_cli_ro.c:456`, `tg_cli_ro.c:486-495`) both support
two additional flags:
- `--offset N` — wire-level `offset_id` for pagination (US-06 UX block)
- `--no-media` — suppress pure-media entries; show only caption for mixed
  entries (US-08 acceptance)

Users cannot discover these flags from `man tg-cli-ro(1)`.

## Scope
1. Update the `.BI history` entry in `man/tg-cli-ro.1` to read:
   `history <peer> [--limit N] [--offset N] [--no-media]`
2. Add a brief description of each flag in the surrounding prose.

## Acceptance
- `groff -man -Tutf8 man/tg-cli-ro.1 | grep -q '\-\-offset'` exits 0.
- `groff -man -Tutf8 man/tg-cli-ro.1 | grep -q '\-\-no-media'` exits 0.
- Man page renders clean under `groff -man` with no warnings.
