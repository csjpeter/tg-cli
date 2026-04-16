# messages.sendMessage

## Description
Send a text message.

## Batch
`tg-cli send <peer> "text"`
`echo "msg" | tg-cli send <peer>`

## Estimate
~150 lines

## Dependencies
- P5-01 (pending) — peer azonosítás
- P3-02 (pending) — bejelentkezés szükséges

## Completion notes (2026-04-16)
- Introduced tg-domain-write static lib — first write-side domain module.
  CMake wiring keeps tg-cli-ro off the link line, enforcing ADR-0005
  read-only guarantee by construction.
- Added src/domain/write/send.{h,c}: domain_send_message builds
  messages.sendMessage#d9d75a4 (flags=0) with a random_id sourced from
  crypto_rand_bytes, parses updateShortSentMessage for the outgoing
  message id, and tolerates the broader updates / updatesCombined /
  updateShort envelopes.
- Created src/main/tg_cli.c — the third binary (batch, read+write).
  v1 exposes `send <peer> <message>` and `send <peer> --stdin` (stdin
  pipe via isatty(0)), closing P8-03 at the same time. Logout and
  login reuse the same app/auth_flow infrastructure.
- Tests: test_domain_send.c — happy path w/ message id, generic
  updateShort envelope, RPC error propagation, null/empty/oversized
  input guards, and a wire-inspection test that checks the message
  text appears in the transmitted buffer.
- Total: 1934 unit + 131 functional tests green, valgrind clean.
