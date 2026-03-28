# tg-cli

A terminal-based, **read-only** Telegram client written in C.
Designed primarily for AI agents that need to read Telegram chat contents for decision support — without any ability to send, delete, or otherwise modify data.
A write-capable companion client may be developed in the future.

---

> **Disclaimer:** This software is provided as-is, without warranty of any kind.
> The author accepts no responsibility for any damage, data loss, or unintended
> consequences resulting from the use or malfunction of this program.

---

## Table of Contents

- [Security](#security)
- [Installation](#installation)
- [Configuration](#configuration)
- [Interactive Mode](#interactive-mode)
- [CLI Batch Mode](#cli-batch-mode)

---

## Security

- **Read-only:** `tg-cli` only calls Telegram Bot API `getUpdates`, `getChat`, `getChatHistory` (or equivalent) endpoints. It never sends, deletes, or modifies messages or chats.
- **Credentials at rest:** The configuration file is written with `0600` permissions, readable only by the owning user. The bot token is stored in plaintext — keep the file private.
- **Transport:** Connections use HTTPS (TLS 1.2+) via libcurl. `SSL_NO_VERIFY=1` disables certificate verification — never use it in production.
- **Local cache:** Fetched messages are stored in `~/.cache/tg-cli/` with directory permissions `0700`. No external service has access to the cache.
- **Logs:** Session diagnostics are written to `~/.cache/tg-cli/logs/session.log`. Logs may include API server responses; rotate or delete them as needed.
- **Memory safety:** The codebase is tested with AddressSanitizer and Valgrind on every CI run to eliminate memory leaks and buffer overflows.

---

## Installation

```bash
./manage.sh deps    # Install system dependencies (Ubuntu 24.04 / Rocky 9)
./manage.sh build   # Build → bin/tg-cli
```

Run it:

```bash
bin/tg-cli
```

---

## Configuration

On the first run, `tg-cli` starts an interactive setup that asks for:

- **Bot Token** — your Telegram Bot API token (obtained from @BotFather)

The configuration is saved to `~/.config/tg-cli/config.ini` and reused on subsequent runs.

To reconfigure, delete or edit that file manually:

```bash
rm ~/.config/tg-cli/config.ini
bin/tg-cli
```

> **One account at a time.** To switch bots, replace or edit the config file.

### Config file format

```ini
API_BASE=https://api.telegram.org
TOKEN=123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11
SSL_NO_VERIFY=0
```

---

## Interactive Mode

When run without arguments in a terminal, `tg-cli` opens a full-screen interactive TUI starting with the recent messages in your monitored chats. Navigation uses keyboard shortcuts — no mouse required.

> **Note:** Interactive mode is under development. The initial release focuses on batch mode.

### Message List View

```
1-20 of 42 message(s) in General.

  ID     From                Text                      Date
  ═════  ══════════════════  ════════════════════════  ════════════════
▶ 42     Alice               Re: Meeting tomorrow      2026-03-28 09:14
  30     Bob                 Invoice attached          2026-03-27 18:01
  ...

  ↑↓=step  PgDn/PgUp=page  Enter=open  q=quit  [1/42]
```

### Chat List View

Opened by pressing `Backspace` in the message list.

```
Chats (5)

  ├── General
  │   ├── Project Alpha
  │   └── Team Updates
  └── Saved Messages

  ↑↓=step  PgDn/PgUp=page  Enter=select  t=flat  Backspace/ESC=quit  [1/5]
```

### Message Reader View

Opened by pressing `Enter` on a message in the list.

```
From:    Alice
Chat:    General
Date:    Fri, 28 Mar 2026 09:14:00 +0100
────────────────────────────────────────
Hi,

Just confirming the meeting is on for 10:00.

── [1/3] PgDn/↓=scroll  PgUp/↑=back  Backspace=list  ESC=quit ──
```

### Media handling

Media content (photos, documents, etc.) is downloaded to the local cache. In the message view, media is shown as a file path or local URL that can be opened in a browser:

```
[photo: ~/.cache/tg-cli/media/photo_42.jpg]
[document: ~/.cache/tg-cli/media/report.pdf]
```

---

## CLI Batch Mode

Pass `--batch` or pipe output to a file/command to disable the interactive TUI. All commands print plain text suitable for scripting.

### list

```
tg-cli list [--chat <id>] [--limit <n>] [--offset <n>] [--batch]
```

Lists recent messages in a chat.

### show

```
tg-cli show <message_id>
```

Displays the full content of a message.

### chats

```
tg-cli chats [--tree]
```

Lists all chats the bot has access to.

### help

```
tg-cli help [command]
```

Shows usage information.
