# DOC-07 — remove dead `arg_print_help()` from arg_parse.c

## Gap
`src/core/arg_parse.c:421` defines `arg_print_help(void)` with a long
block of usage text covering *all* subcommands across the three
binaries. Nothing calls it — each of `src/main/{tg_cli,tg_cli_ro}.c`
has its own `print_usage()` that is actually wired to `ARG_HELP`.
`arg_print_help` is therefore dead code and drifts silently from the
per-binary helps.

## Scope
1. Delete `arg_print_help()` from `src/core/arg_parse.c` and its
   declaration from `arg_parse.h`.
2. If a test references it, delete that test too.
3. Keep `arg_print_version()` — it is live.

## Acceptance
- `grep -rn arg_print_help src/ tests/` returns zero hits.
- Build + `./manage.sh test` green.
- `./manage.sh coverage` numbers unchanged or slightly improved
  (dead code removed from the denominator).
