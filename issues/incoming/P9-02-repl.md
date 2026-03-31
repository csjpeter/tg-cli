# REPL loop (interactive mode)

## Description
Interactive shell: command prompt, room switching, send/receive messages.

## Commands
- `/open <peer>` — switch room
- `/list` — dialogs
- `/history [N]` — last N messages
- `/send <text>` — send message (or plain text if a room is open)
- `/contacts` — contact list
- `/info <peer>` — profile
- `/quit` — exit

## Estimate
~300 lines

## Dependencies
- P9-01 (pending) — readline (sor szerkesztés, history)
- P5-01 (pending) — /list (dialogs)
- P5-02 (pending) — /history (üzenet lekérés)
- P5-03 (pending) — /send (üzenet küldés)
- P7-01 (pending) — /contacts (névjegyzék)
- P3-02 (pending) — bejelentkezés szükséges
