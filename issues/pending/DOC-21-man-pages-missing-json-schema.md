# DOC-21 — Man pages should document the JSON output schema per command

## Gap
Every `--json` command in `tg-cli-ro` and `tg-cli` emits a specific
key set, but the man pages only say "machine-readable JSON output
(where supported)".  Users must read the source to know:

- `me --json` emits `{"id": …, "username": …, "first_name": …,
  "last_name": …, "phone": …, "premium": bool, "bot": bool}`.
- `dialogs --json` emits `[{"type": "user|chat|channel", "id": …,
  "title": …, "username": …, "top": …, "unread": …}, …]`.
- `history --json` emits `[{"id": …, "out": bool, "date": …,
  "text": …, "complex": bool, "media": "photo|document|…",
  "media_id": …, "media_path": …}, …]`.
- `watch --json` emits NDJSON (one object per line):
  `{"peer_id": …, "msg_id": …, "date": …, "text": …}`.
- … (see TEST-43 for the full matrix)

Pipeline authors need a contract they can code against.

## Scope
1. Add `.SH OUTPUT FORMAT` section to `man/tg-cli.1` and `man/tg-tui.1`.
   (The section already exists in `man/tg-cli-ro.1` — expand it to
   list every command's schema, not just the "table/JSON" description.)
2. Under each subcommand `.TP` in all three man pages, add a brief
   "JSON:" line pointing into the OUTPUT FORMAT section.
3. Add one example JSON line per command to the OUTPUT FORMAT
   section.

## Acceptance
- Every `--json`-capable command has a schema in OUTPUT FORMAT.
- `man tg-cli-ro history` → user sees the JSON contract without
  opening source.
- Matches the tests in TEST-43 (single source of truth).
