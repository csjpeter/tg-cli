# DOC-01 — tg-cli-ro --help + man page: fill in missing options

## Gap
Both `src/main/tg_cli_ro.c` `print_usage()` and `man/tg-cli-ro.1` omit
surfaces that either (a) the argument parser already supports or (b)
the user stories explicitly promise.

## Missing items
| Item | Source | Where it's referenced |
|------|--------|------------------------|
| `self` alias of `me` | `arg_parse.c:315` | US-05 acceptance |
| `--offset N` on `history` | `arg_parse.c:120` | US-06 UX block |
| Per-subcommand `--json` hints | `arg_parse.c:52` | US-04..US-10 UX |
| `--batch` global flag | `arg_parse.c:51` | US-03 batch-mode block |
| `--config <path>` global flag | `arg_parse.c:57` | (currently undocumented) |
| `--archived` on `dialogs` | US-04 UX | see FEAT-01 (not yet in code) |
| `--interval SEC` on `watch` | US-07 UX | see FEAT-02 (not yet in code) |
| `--no-media` flag | US-08 UX | see FEAT-05 (not yet in code) |
| `download` works for documents | US-08 acceptance | see FEAT-06 |

## Scope
1. Extend `tg_cli_ro.c:print_usage` with everything that is implemented
   today (`self`, `--offset`, `--batch`, `--config`, and the per-command
   `--json`).
2. Extend `man/tg-cli-ro.1` Subcommand + Global flags sections to match.
3. Items gated on FEAT-01/02/05/06 are documented only after those
   land; this ticket writes a one-line TODO for each.

## Acceptance
- `./bin/tg-cli-ro --help` output mentions every flag the current
  parser accepts; man page renders clean under `groff -man` with no
  warnings.
- Cross-grep: every `str_eq(argv[i], "--...")` in `arg_parse.c` is
  represented either in `--help` or in the man page (or both).
