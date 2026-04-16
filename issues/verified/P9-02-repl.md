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

## Verified — 2026-04-16 (v1, read-only)
- `src/main/tg_tui.c` REPL now covers the spec's read-only commands:
  `me`, `dialogs`/`list`, `history [<peer>] [N]`, `contacts`,
  `info <@peer>`, `search <query>`, `poll`, `help`, `quit`.
- Leading `/` is accepted (IRC-style: `/list`, `/history`).
- `history` accepts `<peer>` (resolves @username through
  contacts.resolveUsername), `<peer> N`, `N`, or nothing (→ Saved
  Messages).
- Write commands (`/send`, etc.) still deferred to US-12 / `tg-cli`.

## Remaining for v2
- Full-screen TUI (ncurses-style panes) is a larger piece — tracked
  separately once needed.
