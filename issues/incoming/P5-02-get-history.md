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
