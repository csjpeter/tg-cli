# DOC-03 — tg-tui REPL help + man page: missing aliases and commands

## Gap
`src/main/tg_tui.c` `print_help()` and `man/tg-tui.1` omit aliases the
REPL actually dispatches on, and a `download` REPL verb that US-08 +
US-11 imply should exist.

## Missing items (existing in code)
| Alias | Source | Target |
|-------|--------|--------|
| `list` | `tg_tui.c:474` | `dialogs` |
| `?` | `tg_tui.c:472` | `help` |
| `del` | `tg_tui.c:512` | `delete` |
| `fwd` | `tg_tui.c:515` | `forward` |

## Missing items (spec promises, not in code — companion tickets)
- `download <peer> <msg_id>` REPL verb — see FEAT-10.
- `self` alias of `me` — see FEAT-13 (US-05 acceptance).

## Scope
1. Extend `tg_tui.c:print_help` Read section with `list` alias under
   `dialogs`; Write section with `del`/`fwd`; Session section with `?`.
2. Mirror in `man/tg-tui.1` REPL COMMANDS subsections.
3. If FEAT-10 / FEAT-13 land, document in the same commit.

## Acceptance
- `tg-tui` REPL `help` output mentions every alias dispatched by the
  REPL main loop (grep the file for `!strcmp(cmd,` and cross-check).
- Man page renders clean under `groff -man`.
