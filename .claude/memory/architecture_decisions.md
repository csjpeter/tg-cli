---
name: Architecture Decisions
description: Key design decisions with rationale and scope
type: project
---

## MTProto 2.0 over raw TCP (not Bot API)

**Decision:** Implement MTProto 2.0 protocol from scratch over raw TCP (POSIX sockets). No Bot API, no TDLib, no libcurl for core MTProto.
**Why:** Bot API cannot fetch chat history — only real user clients via MTProto can. libcurl is HTTP-only; MTProto is a binary TCP protocol.
**How to apply:** Transport layer uses POSIX sockets directly. Crypto uses libssl (OpenSSL) for primitives only. Everything else (TL serialization, session, RPC) is custom code.

## Dependency Inversion (DIP), not Injection (DI)

**Decision:** All system IO wrapped in thin functions. CMake selects implementation at link time. No vtables, no runtime adapter parameters.
**Why:** DI vtables add unnecessary C boilerplate. DIP is simpler, consistent with existing platform abstraction, and link-time substitution is sufficient.
**How to apply:** `crypto.h` wraps OpenSSL, `socket.h` wraps POSIX sockets, `sysio.h` wraps stdio. Tests link `tests/mocks/` implementations. See ADR-0004.

## Read-only first

**Decision:** First version only reads: list chats, message history, media download. No sending, deleting, or modifying.
**Why:** User's explicit request: read-only first, write capability later.
**How to apply:** Only query-type API calls (`getDialogs`, `getHistory`, `getFile`). No `sendMessage` or similar.

## Media: download + local path display

**Decision:** Media downloads to cache, displayed as local file paths in messages.
**Why:** Terminal client can't display images inline. Local path can be opened in browser/viewer.
**How to apply:** `upload.getFile` → download to `~/.cache/tg-cli/media/`. Display as `[photo: /path/to/file.jpg]`.

## Minimal dependencies

**Decision:** Only C stdlib, POSIX, and libssl (OpenSSL) allowed as runtime dependencies.
**Why:** User's explicit principle: keep dependencies minimal. No libcurl for core MTProto.
**How to apply:** Exhaust stdlib/POSIX before considering new dependencies. New deps need justification and user approval.

## Cross-platform portability

**Decision:** Target Linux (primary), macOS, Windows, Android. Platform differences resolved by CMake, not `#ifdef`.
**Why:** User's explicit requirement for future multi-platform support.
**How to apply:** `src/platform/` with `posix/` and `windows/` implementations. CMake selects. Toolchain: Linux=GCC, macOS=GCC/Clang, Windows=MinGW-w64, Android=NDK. No MSVC.

## manage.sh (no Makefile)

**Decision:** `manage.sh` calls CMake directly, no Makefile.
**Why:** More flexible; Makefile was just a wrapper causing duplication.
**How to apply:** All build/test commands via `./manage.sh <cmd>`.

## Custom test framework

**Decision:** No external test libraries. Use `ASSERT`/`RUN_TEST` macros from `tests/common/test_helpers.h`.
**Why:** Minimal dependency principle. External test frameworks (Unity, cmocka) are unnecessary overhead.
**How to apply:** New tests register in `test_runner.c`. Three levels: unit (mock IO), functional (real IO), integration (fake server).
