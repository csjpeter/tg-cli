[![CI](https://github.com/csjpeter/tg-cli/actions/workflows/ci.yml/badge.svg)](https://github.com/csjpeter/tg-cli/actions/workflows/ci.yml)
[![Valgrind](https://github.com/csjpeter/tg-cli/actions/workflows/valgrind.yml/badge.svg)](https://github.com/csjpeter/tg-cli/actions/workflows/valgrind.yml)
[![Coverage](https://csjpeter.github.io/tg-cli/coverage-badge.svg)](https://csjpeter.github.io/tg-cli/)
[![Functional coverage](https://csjpeter.github.io/tg-cli/coverage-functional-badge.svg)](https://csjpeter.github.io/tg-cli/functional/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

# tg-cli

An **unofficial**, terminal-based Telegram user-client suite written in C11.
Three binaries share one MTProto 2.0 codebase built from scratch
(no TDLib, no HTTP, no JSON library):

| Binary | Mode | Capabilities |
|--------|------|--------------|
| `tg-cli-ro` | batch, read-only **by construction** | list dialogs, read history, self/user info, contacts, search, watch updates, download photos |
| `tg-tui` | interactive REPL | everything `tg-cli-ro` does **plus** send, reply, edit, delete, forward, read-markers, file upload |
| `tg-cli` | batch, read + write | same surface as the TUI from the command line, pipe-friendly (stdin → message body) |

`tg-cli-ro` can never mutate server state — the write-capable domain
library is simply not linked into that target (ADR-0005).

> **Status:** v1 MVP feature-complete. 2703 unit tests + 427
> functional tests green; Valgrind clean. The functional suite drives
> every user-facing flow (login / 2FA / dialogs / history / send /
> edit / delete / forward / read-markers / upload / download) through
> an in-process mock Telegram server with real OpenSSL on both sides.

---

> **Disclaimer:** This software is provided as-is, without warranty of any kind.
> The author accepts no responsibility for any damage, data loss, or unintended
> consequences resulting from the use or malfunction of this program.
> This is an **unofficial** client and is not affiliated with Telegram.

---

## Table of Contents

- [Why MTProto?](#why-mtproto)
- [Architecture](#architecture)
- [Security](#security)
- [Dependencies](#dependencies)
- [Building](#building)
- [Configuration](#configuration)
- [Planned Usage](#planned-usage)
- [Implementation Progress](#implementation-progress)
- [Documentation](#documentation)
- [Legal](#legal)
- [License](#license)

---

## Why MTProto?

The Telegram Bot API (HTTPS JSON) is easy to use but has a fundamental limitation:
**bots cannot fetch chat history.** They only receive real-time messages via `getUpdates`.

tg-cli implements the **MTProto 2.0** protocol directly, which is what the official
Telegram desktop and mobile clients use. This gives access to:

- **Full chat history** via `messages.getHistory`
- **Chat/dialog list** via `messages.getDialogs`
- **Media download** via `upload.getFile`
- **Real-time updates** via `updates.getDifference`
- **User account login** with phone number + SMS code

All without any third-party library — the entire MTProto stack is implemented in C from
the protocol specification.

---

## Architecture

The project follows a strict layered CLEAN architecture:

```
Application    →  src/main.c
Domain         →  src/domain/           (Telegram service logic)
Infrastructure →  src/infrastructure/   (config, cache, transport, RPC)
Core           →  src/core/             (logger, fs_util, TL serial, AES-IGE, MTProto crypto)
Platform       →  src/platform/         (terminal + path abstraction for posix/windows)
```

### MTProto protocol stack (what we build):

```
┌─────────────────────────────────────────────┐
│  Application — list chats, read messages     │
├─────────────────────────────────────────────┤
│  Telegram API (TL RPC)                       │
│  auth.sendCode, messages.getHistory, etc.    │
├─────────────────────────────────────────────┤
│  MTProto session                             │
│  msg_id, seq_no, salt, session_id, updates   │
├─────────────────────────────────────────────┤
│  MTProto encryption                          │
│  AES-256-IGE, SHA-256, auth_key management   │
├─────────────────────────────────────────────┤
│  TL serialization                            │
│  binary object encoding                      │
├─────────────────────────────────────────────┤
│  Transport — TCP (Abridged) via POSIX socket  │
└─────────────────────────────────────────────┘
```

---

## Security

- **Read-only `tg-cli-ro` at link time:** the binary never links
  `tg-domain-write`, so no mutation RPC is reachable even if an
  arg-parse bug were exploited.
- **MTProto 2.0 encryption:** AES-256-IGE + SHA-256 for every
  client-server frame. MITM hardened via `new_nonce_hash1` verification
  during the DH handshake.
- **2FA / SRP:** `account.getPassword` + `auth.checkPassword` with
  PBKDF2-HMAC-SHA-512 and the Telegram SRP variant.
- **Auth key:** 2048-bit key generated via DH exchange, persisted
  together with DC id and server salt in `~/.config/tg-cli/session.bin`
  (mode 0600). `--logout` clears it.
- **Credentials at rest:** `~/.config/tg-cli/config.ini` written
  with `0600` permissions.
- **Local cache:** `~/.cache/tg-cli/` with `0700` directory
  permissions; downloaded media under `downloads/`.
- **Logs:** diagnostics in `~/.cache/tg-cli/logs/session.log`. May
  include API response bodies.
- **Memory safety:** every CI run exercises the full suite under
  AddressSanitizer and Valgrind — zero leaks or errors.

---

## Dependencies

| Dependency | Purpose |
|---|---|
| C11 compiler (GCC/Clang) | Build |
| POSIX sockets | TCP transport (built into the OS) |
| libssl (OpenSSL) | AES, SHA-256, RSA, DH crypto primitives |
| CMake | Build system |

**No third-party Telegram libraries.** No TDLib, no Bot API wrappers, no JSON libraries.
Everything is implemented from scratch.

---

## Building

```bash
./manage.sh deps     # Install system dependencies (Ubuntu 24.04 / Rocky 9)
./manage.sh build    # Release build — produces bin/tg-cli-ro, bin/tg-tui, bin/tg-cli
./manage.sh test     # Unit tests under AddressSanitizer
./manage.sh valgrind # Unit tests under Valgrind
./manage.sh coverage # GCOV/LCOV report (~89% for core + infrastructure)
```

---

## Configuration

On first run, tg-cli will guide you through setup:

1. **Phone number** — your Telegram account phone number
2. **Auth code** — received via SMS or Telegram app
3. **2FA password** — if two-factor authentication is enabled

The auth key and configuration are stored at `~/.config/tg-cli/`.

To reconfigure:

```bash
rm -rf ~/.config/tg-cli/
bin/tg-cli
```

### Prerequisites

You need an **api_id** and **api_hash** from Telegram (free):

1. Log in at https://my.telegram.org
2. Go to "API development tools"
3. Fill out the form

---

## Usage

### Interactive REPL — `tg-tui`

```
bin/tg-tui
```

After login you land in a readline-backed prompt. Try `help` for the
full command list. Highlights:

```
tg> dialogs 20
tg> history @somechannel 50
tg> send @user "hello there"
tg> reply @user 12345 "nice"
tg> edit  @user 12345 "fixed typo"
tg> upload @user ~/docs/report.pdf "final version"
tg> poll
```

### Batch read-only — `tg-cli-ro`

Scriptable, guaranteed non-mutating:

```bash
tg-cli-ro --phone +15551234567 --code 12345 me
tg-cli-ro dialogs --limit 20 --json
tg-cli-ro history @peer --limit 50
tg-cli-ro search "hello world" --json
tg-cli-ro download @peer 12345 --out ~/pic.jpg
tg-cli-ro watch
```

### Batch read + write — `tg-cli`

```bash
tg-cli send @peer "hi there"
echo "msg from stdin" | tg-cli send @peer
tg-cli send @peer --reply 12345 "threaded answer"
tg-cli edit @peer 12345 "corrected"
tg-cli delete @peer 12345 --revoke
tg-cli forward @srcchannel @dstchat 12345
tg-cli send-file @peer report.pdf --caption "final"
tg-cli read @peer
```

### Media handling

Photos attached to messages in history are exposed via `download`.
The default output path is
`~/.cache/tg-cli/downloads/photo-<photo_id>.jpg`; override with `--out`.

---

## Implementation Progress

See [docs/userstory/US-00-roadmap.md](docs/userstory/US-00-roadmap.md)
for the authoritative status. Short version:

### Built and tested
- [x] CLEAN layered architecture (core → infrastructure → domain → app)
- [x] Three binaries (`tg-cli-ro` / `tg-tui` / `tg-cli`) from one codebase
- [x] RAII memory safety via `__attribute__((cleanup))`
- [x] Logger + filesystem helpers + INI config store + cache store
- [x] Platform abstraction (POSIX + MinGW-w64)
- [x] Custom test framework; `./manage.sh test|valgrind|coverage`
- [x] TL serialization + AES-256-IGE + MTProto encryption
- [x] DH auth key exchange (PQ factorization, RSA_PAD, `new_nonce_hash1`)
- [x] Session management + persistence + `bad_server_salt` retry +
      service-frame draining
- [x] Abridged TCP transport with 3-byte length prefix
- [x] RPC framework; `api_call` wraps `invokeWithLayer` + `initConnection`
- [x] Phone + SMS code login with DC migration
- [x] 2FA via SRP (`account.getPassword` + `auth.checkPassword`)
- [x] Read: me, dialogs, history (self + peer), search, user-info,
      contacts, watch, photo download
- [x] Write: send, reply, edit, delete, forward, read-markers,
      file upload (≤10 MiB documents)

### Known follow-ups
- `upload.saveBigFilePart` for files > 10 MiB + media-DC migration
- Per-variant skippers for Poll / Story / Game / Invoice / Giveaway /
  WebPage / PaidMedia media
- ReplyMarkup / Reactions / Replies / RestrictionReason / FactCheck
  trailers on Message objects
- Curses-mode TUI (US-11 v2) with pane-based live redraw

---

## Documentation

```
docs/
├── README.md                  ← index
├── SPECIFICATION.md           ← product spec (living)
├── legal-research.md          ← licensing, ToS compliance, AI/ML clause
├── userstory/
│   ├── US-00-roadmap.md       ← authoritative status / backlog
│   └── US-*.md                ← per-feature user stories
├── dev/
│   ├── mtproto-reference.md   ← MTProto 2.0 protocol notes
│   ├── telegram-bot-api.md    ← Bot API reference (for comparison)
│   ├── logging.md             ← log levels, rotation, traffic capture
│   └── testing.md             ← unit / functional test layout
├── adr/
│   ├── 0001-clean-architecture.md
│   ├── 0002-raii-memory-safety.md
│   ├── 0003-custom-test-framework.md
│   ├── 0004-dependency-inversion.md
│   └── 0005-three-binary-architecture.md
```

---

## Legal

This is an **unofficial** Telegram client. See [docs/legal-research.md](docs/legal-research.md)
for the full legal research including:

- Telegram API Terms of Service compliance
- Server RSA public key provenance (from TDLib, BSL-1.0)
- api_id registration requirements
- AI/ML clause analysis
- GPL/BSL licensing implications

---

## License

[GPL v3](LICENSE)
