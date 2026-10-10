---
name: tg-cli
description: Use when reading or sending Telegram messages from the command line with tg-cli, or when a script or agent needs a chat's history, a search result, the contact list or a media file from a Telegram account. Use tg-cli-ro for looking, tg-cli only for changing (send, edit, delete, forward, send-file, read). Do not use for the interactive terminal UI (tg-tui), and never to type a login code or 2FA password on the user's behalf.
---

# tg-cli

Batch Telegram user-client speaking MTProto 2.0 directly. Output is plain text
on stdout (`--json` where supported), diagnostics on stderr, exit code 0 on
success, 1 on a runtime error (bootstrap, credentials, RPC, network), 2 on an
argument error. Full reference: `man tg-cli`, `man tg-cli-ro`.

## Two programs

- **`tg-cli-ro`** reads only: `me`, `dialogs`, `history`, `search`, `contacts`,
  `user-info`, `watch`, `download`. It is compiled without any write code.
  **Use it whenever you only need to look.**
- **`tg-cli`** has the same reading commands and, in addition, `send`,
  `send-file`, `read`, `edit`, `delete`, `forward`.

## Peers

`@username`, a numeric id (`123456789`, channels `-100…`), or `me` (Saved
Messages).

## Reading

```
tg-cli-ro me
tg-cli-ro dialogs [--limit N] [--archived]
tg-cli-ro history @peer [--limit N] [--offset N] [--no-media]
tg-cli-ro search [@peer] "query" [--limit N]
tg-cli-ro contacts
tg-cli-ro user-info @peer
tg-cli-ro download @peer MSG_ID [--out PATH]
tg-cli-ro watch [--peers X,Y] [--interval SEC]     # runs until Ctrl-C; --json is NDJSON
```

Dates are **Unix epoch seconds**, not text: `date -d @1713456000`.
Add `--json` for parseable output; keep `--quiet` to drop notices.

## Changing

```
tg-cli send @peer "text"
tg-cli send @peer --reply MSG_ID "text"
echo "text" | tg-cli send @peer --stdin
tg-cli send-file @peer ./report.pdf --caption "final"
tg-cli read @peer [--max-id N]
tg-cli edit @peer MSG_ID "new text"
tg-cli delete @peer MSG_ID [--revoke]      # --revoke removes it for everyone
tg-cli forward @from @to MSG_ID
```

Sending, editing and deleting act on a real account and cannot be undone for
the recipient: confirm the peer and text with the user first.

## Credentials and login

`api_id` and `api_hash` come from `TG_CLI_API_ID` / `TG_CLI_API_HASH` or
`~/.config/tg-cli/config.ini` (mode 0600; a world-readable file is refused).
The session lives in `~/.config/tg-cli/session.bin`; `tg-cli --logout` removes it.

- `tg-cli login --api-id N --api-hash HEX [--force]` is the batch form. Without
  those flags it is an interactive wizard and **exits 1 at once when stdin is not
  a terminal**.
- First login needs the code Telegram sends (`--code`) and, with 2FA, the
  password (`--password`). **Do not guess or ask for these in a script: ask the
  user to run the login themselves** (`! tg-cli login`).

## What goes wrong

- `FLOOD_WAIT_X`: rate limit, wait X seconds; exit 1.
- `PEER_ID_INVALID`, `MESSAGE_ID_INVALID`, `MESSAGE_AUTHOR_REQUIRED`: wrong or
  inaccessible peer/message, or not the author (exit 1).
- "No RSA public key configured": the config file lacks `rsa_pem`; see
  `docs/user/setup-my-telegram-org.md`.
- Piping `watch` into `head` ends the process (SIGPIPE is not handled).
- Diagnostics are in `~/.cache/tg-cli/logs/session.log`; message bodies are not
  logged by default.
