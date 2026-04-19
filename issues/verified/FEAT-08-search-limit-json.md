# FEAT-08 — `search` honors `--limit` and `--json`

## Gap
US-10 UX:
```
tg-cli-ro search [<peer>] <query> [--limit N] [--json]
```
`arg_parse.c:parse_search` does not accept `--limit`; `tg-cli-ro
cmd_search` does not respect `args.json`.

## Scope
1. `parse_search` accepts `--limit N` (range [1, 100], default 20).
2. `cmd_search` threads it into `domain_search_global` /
   `domain_search_peer`, and emits JSON objects per result when
   `args.json` is set.
3. Functional tests for both modes (see TEST-10).

## Acceptance
- `tg-cli-ro search "hello" --limit 50` yields up to 50 results.
- `--json` emits a JSON array with stable keys.
- Behaviour without flags unchanged.
