# FEAT-01 — implement `--archived` flag for dialogs

## Gap
US-04 (list dialogs) UX block:
```
tg-cli-ro dialogs [--limit N] [--archived] [--json]
```
`--archived` is not currently parsed (`arg_parse.c:parse_dialogs`).
Telegram exposes an archive folder via `messages.getDialogs` folder_id=1.

## Scope
1. `arg_parse.c:parse_dialogs` accepts `--archived` → sets a new
   `args.archived` bit.
2. `domain_get_dialogs` takes an `int archived` arg and sets
   `folder_id = 1` in the `messages.getDialogs` call when set.
3. Unit test: arg_parse round-trip + wire-inspection that folder_id=1
   lands on the wire when `--archived` is passed.
4. Functional test: responder dispatches on the folder_id and returns
   a canned dialog list.

## Acceptance
- `tg-cli-ro dialogs --archived` returns the archived-folder dialogs
  (different set from the default folder).
- `--help` + man page updated (see DOC-01).
