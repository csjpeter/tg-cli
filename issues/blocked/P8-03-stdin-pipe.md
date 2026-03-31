# stdin pipe support

## Description
`echo "msg" | tg-cli send <peer>` — when stdin is not a terminal (isatty() == 0), read stdin as message content.

## Estimate
~50 lines

## Dependencies
- P8-01 (pending) — argument parser (send subcommand)
- P5-03 (pending) — messages.sendMessage implementáció
