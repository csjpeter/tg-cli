# FEAT-02 — implement `--interval SEC` for watch

## Gap
US-07 acceptance:
> Default interval 30 s; configurable down to 2 s (API rate limits).

`arg_parse.c:parse_watch` parses `--peers` only. The watch loop in
`src/domain/read/updates.c` uses a compile-time constant.

## Scope
1. `parse_watch` accepts `--interval N` (clamped to [2, 3600]).
2. Thread `interval_sec` down to the poll loop in `tg_cli_ro.c
   cmd_watch`.
3. Unit test for parsing + clamp.
4. Functional test: two RPCs dispatched under a 2 s interval, server
   counts calls via `mt_server_rpc_call_count()`.

## Acceptance
- `tg-cli-ro watch --interval 5` polls every 5 s.
- Values outside [2, 3600] produce a clear arg-parse error (not
  silently clamped — that hides typos).
- `--help` + man page updated (see DOC-01).
