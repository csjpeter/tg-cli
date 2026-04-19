# DOC-16: No exit-code table in man pages or help output

## Location
`src/main/tg_cli.c`, `src/main/tg_cli_ro.c`
Man pages (if any) under `docs/`

## Problem
The `main()` functions return a mix of `0`, `1`, `-1` (which the shell sees as
255), and implicit `EXIT_SUCCESS`.  There is no documented exit-code contract:

| Code | Meaning |
|------|---------|
| 0    | Success |
| 1    | Runtime error (network, auth, API) |
| 2    | Usage error (bad arguments) — **only partially** (DOC-11 already covers exit-2 in history) |
| 255  | Crash / uncaught -1 return from main |

Without a public exit-code table, shell scripts cannot reliably distinguish
"no messages found" (success, exit 0) from "network error" (exit 1).

`--help` output (`arg_print_help()`) and the man pages should include an EXIT
CODES section listing codes 0, 1, 2, and noting that code 2 is reserved for
argument errors.

## Fix direction
1. Ensure all error returns from `main()` use consistent codes (0=ok, 1=runtime,
   2=usage; never raw -1).
2. Add an EXIT CODES section to all three man pages.
3. Add it to the `--help` output printed by `arg_print_help()`.
