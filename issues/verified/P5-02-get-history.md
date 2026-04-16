# messages.getHistory (read messages)

## Description
Message history for a given chat, in chronological order.

## Batch
`tg-cli history <peer> [-n 50] [--json] [--offset-id N]`

## Output
msg_id | date | from | text | [attachment_type attachment_name]

## Estimate
~200 lines

## Dependencies
- P5-01 (pending) — peer azonosítás (dialog listából)
- P3-02 (pending) — bejelentkezés szükséges

## Verified — 2026-04-16 (v1)
- `src/domain/read/history.c` + `tg-cli-ro history <peer>`.
- Supports self / @username / channel via `resolve_peer_arg()`.
- Simple-flag messages yield id + date + text; complex flags
  (fwd_from, reply_to, media, entities) are marked `complex=1`
  with no text — tracked as **P5-09 complex message parse**.
- **v1 limitation:** one entry per call (same reason as P5-01).
