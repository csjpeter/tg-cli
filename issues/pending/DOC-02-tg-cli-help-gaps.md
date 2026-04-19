# DOC-02 — tg-cli --help + man page: error references + upload alias

## Gap
`src/main/tg_cli.c` `print_usage()` and `man/tg-cli.1` do not mention
the error codes the user stories promise, and reference an `upload`
alias that does not exist in code (see FEAT-11 if we decide to add it).

## Missing items
| Item | Source |
|------|--------|
| `FLOOD_WAIT_X` surfacing behaviour | US-12 acceptance |
| `MESSAGE_ID_INVALID`, `MESSAGE_AUTHOR_REQUIRED`, `PEER_ID_INVALID` | US-13 acceptance |
| `--config <path>` global flag | `arg_parse.c:57` |
| `--batch` global flag | `arg_parse.c:51` |
| UPLOAD_MAX_SIZE (1.5 GiB) note | US-14 acceptance |

## Scope
1. Extend `tg_cli.c:print_usage` with `--batch` / `--config` and a one
   line on the expected non-zero exit on `FLOOD_WAIT_X`, etc.
2. Add a DIAGNOSTICS section to `man/tg-cli.1` listing the server
   error codes that bubble up with their exit code.
3. Remove the `upload` alias reference from man/README if FEAT-11 is
   rejected; otherwise document both.

## Acceptance
- `tg-cli send bogus ...` under a `FLOOD_WAIT` response surfaces the
  code + retry-after seconds; DIAGNOSTICS section documents the
  observable string.
- Man page renders clean under `groff -man`.
