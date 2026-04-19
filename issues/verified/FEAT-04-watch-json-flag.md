# FEAT-04 — `--json` output for `watch`

## Gap
US-07 UX:
```
tg-cli-ro watch [--peers @a,@b] [--interval SEC] [--json]
```
The global `--json` flag is parsed but the watch handler in
`src/main/tg_cli_ro.c cmd_watch` prints plain lines only.

## Scope
1. Respect `args.json` in `cmd_watch`: emit one JSON object per
   delivered update (`{"peer":..., "msg_id":..., "date":..., "text":
   "..."}`), flushing per line so pipes get live data.
2. Functional test: responder pushes two queued updates, stdout
   captured via `freopen` and asserted to contain two valid JSON
   objects on separate lines.

## Acceptance
- `tg-cli-ro watch --json | jq .` works; every line is a valid JSON
  object.
- Plain mode unchanged.
