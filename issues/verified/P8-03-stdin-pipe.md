# stdin pipe support

## Description
`echo "msg" | tg-cli send <peer>` — when stdin is not a terminal (isatty() == 0), read stdin as message content.

## Estimate
~50 lines

## Dependencies
- P8-01 (pending) — argument parser (send subcommand)
- P5-03 (pending) — messages.sendMessage implementáció

## Completion notes (2026-04-16)
Closed together with P5-03. tg-cli cmd_send falls back to reading stdin
(fread until EOF, strip one trailing newline) whenever the message
argument is absent *and* stdin is not a tty (isatty(0) == 0). The
TTY check ensures interactive runs still error out with a helpful
message rather than hanging forever on fread.
