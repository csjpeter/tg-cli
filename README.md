# tg-cli

An **unofficial**, terminal-based, **read-only** Telegram user client written in C11.

tg-cli connects to Telegram as a real user (not a bot) via the **MTProto 2.0** protocol,
implemented from scratch with no third-party Telegram libraries.
It provides read-only access to your chats — without any ability to send, delete, or
otherwise modify data.

> **Status:** Early development. The MTProto protocol stack is being built from the
> ground up. See [Implementation Progress](#implementation-progress) for details.

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

- **Read-only:** tg-cli only retrieves data. It never sends, deletes, or modifies messages.
- **MTProto 2.0 encryption:** AES-256-IGE + SHA-256 for all client-server communication.
- **Auth key:** 2048-bit key generated via DH exchange, stored locally with optional password protection.
- **Credentials at rest:** Configuration file written with `0600` permissions.
- **Local cache:** Fetched messages stored in `~/.cache/tg-cli/` with `0700` directory permissions.
- **Logs:** Diagnostics in `~/.cache/tg-cli/logs/session.log`. May include API responses.
- **Memory safety:** Tested with AddressSanitizer and Valgrind on every CI run.

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
./manage.sh deps    # Install system dependencies (Ubuntu 24.04 / Rocky 9)
./manage.sh build   # Release build → bin/tg-cli
./manage.sh test    # Run unit tests with AddressSanitizer
./manage.sh valgrind # Run tests with Valgrind
./manage.sh coverage # Generate coverage report (>90% goal for core/infra)
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

## Planned Usage

### Interactive TUI mode

```
bin/tg-cli
```

Opens a full-screen terminal UI with keyboard navigation (no mouse required).

### Batch mode

```bash
tg-cli list [--chat <id>] [--limit <n>] [--offset <n>] [--batch]
tg-cli show <message_id>
tg-cli chats [--tree]
tg-cli help [command]
```

### Media handling

Media (photos, documents) is downloaded to local cache and displayed as file paths
that can be opened in a browser or image viewer:

```
[photo: ~/.cache/tg-cli/media/photo_42.jpg]
[document: ~/.cache/tg-cli/media/report.pdf]
```

---

## Implementation Progress

### Already built (scaffold)

- [x] Clean layered architecture (core → infrastructure → domain → application)
- [x] RAII memory safety via `__attribute__((cleanup))`
- [x] Logger with rotation and levels
- [x] Filesystem utilities (mkdir_p, permissions)
- [x] Config store (INI file, 0600 permissions)
- [x] Generic cache store (category/key-based)
- [x] Platform abstraction (terminal + path, posix/windows)
- [x] Custom test framework (ASSERT / RUN_TEST macros)
- [x] Build system (manage.sh + CMake)

### MTProto stack (in progress)

- [ ] TL serialization (`tl_serial.h/c`)
- [ ] AES-256-IGE (`ige_aes.h/c`)
- [ ] MTProto encryption (`mtproto_crypto.h/c`)
- [ ] Auth key generation — DH exchange (`mtproto_auth.h/c`)
- [ ] Session management (`mtproto_session.h/c`)
- [ ] TCP transport — Abridged encoding (`tl_transport.h/c`)
- [ ] RPC framework (`mtproto_rpc.h/c`)
- [ ] Telegram API methods (`telegram_api.h/c`)
- [ ] User authentication (phone + SMS + 2FA)
- [ ] Chat list, message history, media download
- [ ] Real-time updates
- [ ] Interactive TUI

---

## Documentation

```
docs/
├── README.md                  ← index
├── legal-research.md          ← licensing, ToS compliance, AI/ML clause analysis
├── dev/
│   ├── mtproto-reference.md   ← MTProto 2.0 protocol details, implementation plan
│   ├── telegram-bot-api.md    ← Bot API reference (for comparison)
│   ├── logging.md             ← log levels, rotation, traffic capture
│   └── testing.md             ← unit tests, coverage requirements
├── adr/
│   ├── 0001-clean-architecture.md
│   ├── 0002-raii-memory-safety.md
│   └── 0003-custom-test-framework.md
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
