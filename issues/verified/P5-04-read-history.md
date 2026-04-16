# messages.readHistory (mark as read)

## Batch
`tg-cli read <peer>`

## Estimate
~50 lines

## Dependencies
- P5-01 (pending) — peer azonosítás
- P3-02 (pending) — bejelentkezés szükséges

## Completion notes (2026-04-16)
- domain/write/read_history.{h,c}: domain_mark_read dispatches on peer
  kind — messages.readHistory for users / chats / self (response is
  messages.affectedMessages), channels.readHistory for channels
  (response is Bool).
- arg_parse learned `read <peer> [--max-id N]`; tg-cli cmd_read calls
  domain_mark_read after resolving the peer. max_id=0 means "mark
  everything up to now" per the Telegram spec.
- tg-cli-ro intentionally does NOT expose the command (read-only).
- Tests: test_domain_read_history.c — affectedMessages happy path,
  channel boolTrue / boolFalse, RPC error, null args.
- 1940 unit + 131 functional tests green, valgrind clean.
