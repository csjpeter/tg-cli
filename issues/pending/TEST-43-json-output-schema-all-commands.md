# TEST-43 — Functional test: JSON output schema for every read/write command

## Gap
Many commands support `--json` (see `cmd_me`, `cmd_dialogs`,
`cmd_history`, `cmd_search`, `cmd_user_info`, `cmd_contacts`,
`cmd_download`, `cmd_watch`, `cmd_send`, `cmd_edit`, `cmd_delete`,
`cmd_forward`, `cmd_send_file`, `cmd_read`), but functional tests
never verify the emitted JSON against a known schema.

A silent regression (missing comma, field renamed, wrong type) would
break every downstream script without any test failure.

## Scope
Create `tests/functional/test_json_output.c` that, for each supported
`--json` command, runs the domain function, captures stdout, parses
the JSON with a tiny in-test parser (or `strstr` asserts for each
expected key), and verifies:

| Command        | Required keys |
|----------------|---------------|
| me             | id, username, first_name, last_name, phone, premium, bot |
| dialogs        | type, id, title, username, top, unread |
| history        | id, out, date, text, complex, media, media_id, media_path |
| search         | id, out, date, text, complex |
| user-info      | type, id, username, access_hash |
| contacts       | user_id, mutual |
| download       | saved, kind, id |
| watch          | peer_id, msg_id, date, text (NDJSON per line) |
| send           | sent, message_id |
| edit           | edited |
| delete         | deleted, revoke |
| forward        | forwarded |
| send-file      | uploaded, kind |
| read           | read |

## Acceptance
- Test fails if any key is missing, renamed, or type-changed.
- Uses captured stdout (fmemopen or a tempfile) — not stubs.
- Covers `--json` true/false toggle for at least one command to
  confirm plain path still works.
