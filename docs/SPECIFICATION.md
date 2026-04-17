# tg-cli — Project Specification

> Living document. Captures the user's feature requests and design
> constraints as stated during development. Keep compact and evergreen.

Last updated: 2026-04-17

---

## 1. Mission

An unofficial Telegram user-client suite that speaks **MTProto 2.0** directly,
in plain C, with minimal runtime dependencies (C stdlib, POSIX, libssl).
Aimed at terminal users who want scriptable access to their Telegram account.

## 2. Deliverables (binaries)

| Binary | Mode | Capabilities | Status |
|--------|------|--------------|--------|
| `tg-cli-ro` | **Batch, read-only** (forever) | list/read dialogs, history, self info, watch updates, photo download | ✅ MVP shipped |
| `tg-tui` | **Interactive TUI** | read + write (send, reply, edit, delete, forward, read, upload) | ✅ MVP shipped |
| `tg-cli` | **Batch, read+write** | everything tg-cli-ro does + send / reply / edit / delete / forward / read / send-file | ✅ MVP shipped |

`tg-cli-ro` is hard-guaranteed read-only — it must **never** issue API calls
that mutate server state (no `messages.sendMessage`, no `messages.readHistory`,
no edit/delete/forward).

## 3. Read-only feature set (MVP — applies to tg-cli-ro and tg-tui v1)

| ID | Feature | Telegram API |
|----|---------|--------------|
| F-01 | List dialogs (rooms, channels, groups, DMs) | `messages.getDialogs` |
| F-02 | Show own profile / identity | `users.getUsers(inputUserSelf)` |
| F-03 | Read channel/chat message history | `messages.getHistory` |
| F-04 | Watch incoming messages (near-real-time) | `updates.getDifference` loop |
| F-05 | Search messages (global or in peer) | `messages.search` |
| F-06 | User info by username / id | `users.getFullUser`, `contacts.resolveUsername` |
| F-07 | Contact list | `contacts.getContacts` |
| F-08 | Download media referenced in messages | `upload.getFile` (chunked) |

All F-0x output respects:
- `--json` — machine-readable JSON (tg-cli-ro, tg-cli).
- `--quiet` — suppress informational/diagnostic text.
- Unicode terminal output (UTF-8); no escape injection.

## 4. Write feature set (tg-tui & future tg-cli)

| ID | Feature | Telegram API |
|----|---------|--------------|
| W-01 | Send message (text) | `messages.sendMessage` |
| W-02 | Mark chat as read | `messages.readHistory` |
| W-03 | Edit / delete / forward / reply | `messages.edit*`/`messages.delete*`/… |
| W-04 | Upload media | `upload.saveFilePart` + `messages.sendMedia` |

## 5. Runtime expectations

- **Auth:** phone number + SMS code (+2FA SRP via account.getPassword +
  auth.checkPassword when the server requests it). Persistent auth key
  bundled in `~/.config/tg-cli/session.bin` (mode 0600) together with
  `dc_id` and `server_salt`.
- **Config:** `~/.config/tg-cli/config.ini` holds api_id, api_hash, current DC.
- **Cache:** `~/.cache/tg-cli/` for messages and downloaded media; bounded
  by `cache_evict_stale`.
- **Logs:** `~/.cache/tg-cli/logs/session.log` — diagnostic only.
- **DC migration:** `PHONE_MIGRATE_X` → reconnect to the indicated DC.
- **Real-time:** `updates.getDifference` polling; long-poll if the protocol
  permits, otherwise periodic pull with configurable interval.

## 6. Non-functional requirements

- **Platforms:** Linux (primary), macOS, Windows (MinGW-w64), Android (NDK).
- **Toolchain:** GCC or Clang (MSVC explicitly out of scope).
- **Dependencies:** C stdlib, POSIX, libssl (OpenSSL). No libcurl, no TDLib.
- **Testing:** unit (mock IO) → functional (real crypto) → integration
  (fake server). Core+infra line coverage ≥ 90% (TUI tracked separately).
- **Memory:** zero leaks under Valgrind, zero ASAN errors.
- **Security:** auth key file 0600; no secrets in logs; no injection into
  TL payloads.

## 7. Architectural constraints (restated)

- CLEAN layered architecture: `main → domain → infrastructure → core/platform`.
- No `#ifdef` for platform selection outside `src/platform/` implementation
  files — CMake selects backends.
- All system IO wrapped in thin headers for DIP (see ADR-0004).
- `__attribute__((cleanup(...)))` for RAII (no manual `free` when avoidable).
- English in repo (code, docs, commits); Hungarian only in live chat.

## 8. Out of scope (now)

- Calls (voice/video), payments, stickers/masks creation.
- Secret chats (end-to-end) — MTProto 2.0 "cloud" chats only.
- Bot API compatibility.
- GUI.

## 9. Living backlog

Feature IDs above are mapped to `issues/*` tickets and user stories under
`docs/userstory/`. Current status snapshot: see `docs/userstory/US-00-roadmap.md`.
