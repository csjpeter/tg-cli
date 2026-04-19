# TEST-49 — Functional test: read --max-id reaches server correctly

## Gap
`cmd_read` in `tg_cli.c` forwards `args->msg_id` to `domain_mark_read`
as the max-id argument.  There is an existing FT
`test_mark_read_{user,channel}` that covers the happy path with a
zero max_id, but no test pins that a non-zero `--max-id 100` reaches
the server-side `messages.readHistory` / `channels.readHistory` RPC.

A regression where `msg_id` is dropped before the RPC call would
cause `--max-id` to become a no-op silently.

## Scope
Amend `test_write_path.c` with two new tests:
- `test_mark_read_user_with_max_id`:
  - Arm mock on `messages.readHistory` to capture the `max_id` arg.
  - Call `domain_mark_read(..., msg_id=100, ...)`.
  - Assert captured `max_id == 100`.
- `test_mark_read_channel_with_max_id`: same but for
  `channels.readHistory`.

## Acceptance
- Both tests pass under ASAN.
- Regression if `msg_id` is ignored in the wiring from args → domain.

## Dependencies
- `mt_server` mock needs a one-shot argument capture hook if not
  already present.
