# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Cost / token discipline (from usage review 2026-04-17)

Observed patterns and their guardrails for every future session:

1. **Minimise sub-agent spawning.** The usage report shows 100 %
   of spend came from subagent-heavy sessions. Prefer direct tools
   (Read / Grep / Glob / Edit) whenever the work fits in the main
   context. Only spawn an Agent when the search is open-ended,
   genuinely parallel, or would flood the main context with raw
   output. If a subagent is unavoidable, pick the cheapest model
   that does the job (Haiku for bulk reads, Sonnet for simple
   edits) — do not default to the parent model.
2. **Keep sessions shorter.** 97 % of spend came from sessions
   active for 8+ hours. Run `/compact` mid-task once you've closed
   out a logical unit; `/clear` when switching to an unrelated
   area. Treat each feature ticket as its own session if possible.
3. **Stay below the 150k-context danger line.** 83 % of spend was
   at >150k context. Long sessions amortise cache but every new
   turn multiplies that cached size. When the context counter is
   approaching 150k, prefer `/compact` over one more ad-hoc read.
4. **Commit small, often.** Each commit is a natural compaction
   anchor — the test suite is tiny, so there's no reason to batch
   ten features into one session.
5. **Avoid re-reads.** Rely on the harness's file-state tracking;
   don't Read a file again after editing it just to confirm.

## Build Commands

```bash
./manage.sh build      # Release build → bin/tg-cli
./manage.sh debug      # Debug build with Address Sanitizer (ASAN)
./manage.sh test       # Unit tests with ASAN
./manage.sh valgrind   # Unit tests with Valgrind
./manage.sh coverage   # GCOV/LCOV coverage report (>90% goal for core/infra)
./manage.sh clean      # Remove build artifacts
./manage.sh deps       # Install system dependencies (Ubuntu 24.04 / Rocky 9)
```

There is no Makefile — `manage.sh` calls CMake directly. Pass an optional substring filter to run matching suites only: `./manage.sh test <filter>` (e.g. `./manage.sh test test_ige`). Without a filter all tests run.

## Architecture

The project follows a strict layered CLEAN architecture with zero circular dependencies:

```
Entry points   →  src/main/{tg_cli,tg_cli_ro,tg_tui}.c   (three binaries, ADR-0005)
App            →  src/app/           (bootstrap, auth flow, credentials, DC config,
                                      DC session, session store)
TUI            →  src/tui/           (screen, pane, list_view, dialog_pane,
                                      history_pane, status_row, app state machine)
Domain         →  src/domain/read/   (self, dialogs, history, search, user_info,
                                      contacts, updates, media)
                  src/domain/write/  (send, edit, delete, forward, read_history,
                                      upload)
Infrastructure →  src/infrastructure/ (config_store, cache_store, transport,
                                      mtproto_auth, mtproto_rpc, api_call,
                                      auth_session, auth_2fa, auth_transfer)
Core           →  src/core/          (logger, fs_util, raii.h, arg_parse,
                                      readline, tl_serial, tl_skip, tl_registry,
                                      ige_aes, mtproto_crypto, mtproto_session,
                                      crypto wrappers, config)
Platform       →  src/platform/{terminal,path,socket}.h
                    src/platform/posix/    (Linux/macOS/Android)
                    src/platform/windows/  (MinGW-w64)
Vendor         →  src/vendor/tinf    (bundled tiny gzip decoder)
```

Dependency rule: every layer may depend on layers below it; `platform/` sits
alongside `core/` and is depended upon by `infrastructure/`, `domain/`, `app/`,
`tui/`, and `main/`. `tg-cli-ro` is compile-time read-only: it never links
`tg-domain-write` (ADR-0005). No layer may contain `#ifdef` guards for platform
selection — that is the build system's (CMake's) responsibility.

**Data flow:** `src/main/<binary>.c` calls `app_bootstrap()` → logger/paths/config
init → optional phone+SMS+2FA login via `src/app/auth_flow.c` → MTProto handshake
(auth key DH) on first connect → encrypted channel via `mtproto_rpc` + `api_call`
→ Telegram API calls (TL serialized, invokeWithLayer + initConnection wrapped)
→ domain layer parses the response → results to stdout, diagnostics to
`~/.cache/tg-cli/logs/`.

**Config** is stored at `~/.config/tg-cli/config.ini` with mode 0600 (api_id, api_hash, DC info).

## Communication

- **Protocol:** MTProto 2.0 over raw TCP (POSIX sockets) — no HTTP, no libcurl
- **No pre-built API libraries** — entire MTProto stack implemented from scratch
- **TL serialization:** binary object encoding (see `src/core/tl_serial.h/c`)
- **Crypto:** AES-256-IGE + SHA-256 via libssl (see `src/core/crypto.h/c`)
- **Real-time:** `updates.getDifference` over encrypted channel

## RAII Memory Safety

The project uses GNU `__attribute__((cleanup(...)))` for automatic resource deallocation — see `src/core/raii.h`. Macros like `RAII_STRING`, `RAII_FILE`, `RAII_DIR` eliminate manual `free()`/cleanup boilerplate. New resources should use these macros rather than manual cleanup.

## Custom Test Framework

No external test libraries are used. Tests use `ASSERT(condition, message)` and `RUN_TEST(test_func)` macros from `tests/common/test_helpers.h`. Unit tests live in `tests/unit/`. Functional tests live in `tests/functional/` and drive the production domain layer against the in-process mock Telegram server (`tests/mocks/mock_tel_server.{h,c}`) with real OpenSSL on both sides — see [ADR-0006](docs/adr/0006-test-strategy.md), [ADR-0007](docs/adr/0007-mock-telegram-server.md), and [docs/dev/mock-server.md](docs/dev/mock-server.md).

## Language & Standard

C11 (`-std=c11`). Linked against libssl (OpenSSL). All public functions should have Doxygen-style comments.

## Dependency Policy

**Keep external dependencies minimal.**  The project intentionally uses only the C standard library, POSIX, and libssl (OpenSSL).  Before reaching for a new library, exhaust stdlib/POSIX options first.  New runtime dependencies require explicit justification and user approval.

## Portability

**Target platforms: Linux (primary), macOS, Windows, Android.**

Prefer standard C11 / POSIX interfaces.  Where platform-specific APIs are
unavoidable, isolate them behind thin abstraction layers (e.g. a `platform/`
module) so each target only needs to implement a small, well-defined surface.

Known portability gaps that need shims before non-Linux builds work:

| API / feature | macOS | Android (NDK) | Windows |
|---|---|---|---|
| `termios.h` raw mode | ✅ | ✅ | ❌ needs `GetConsoleMode`/`SetConsoleMode` |
| `ioctl TIOCGWINSZ` | ✅ | ✅ | ❌ needs `GetConsoleScreenBufferInfo` |
| `wcwidth(3)` | ✅ | ✅ | ❌ needs bundled implementation |
| `asprintf` | ✅ | ✅ | ❌ needs a thin wrapper (available in MinGW) |
| `__attribute__((cleanup(...)))` (RAII) | ✅ GCC / Apple Clang | ✅ Clang NDK | ✅ MinGW-w64 (GCC) |
| Home dir (`$HOME`) | ✅ | ⚠️ use app data dir | ❌ use `%USERPROFILE%` |
| Cache/config paths (`~/.cache`, `~/.config`) | ✅ | ❌ use app-specific dirs | ❌ use `%APPDATA%` |
| TCP sockets | ✅ POSIX `sys/socket.h` | ✅ POSIX | ✅ Winsock2 (`platform/windows/socket.c`) |

**Compiler policy: GCC (or Clang) on every platform — MSVC is out of scope.**

| Platform | Toolchain |
|----------|-----------|
| Linux | GCC |
| macOS | GCC (Homebrew) or Apple Clang |
| Windows | MinGW-w64 (GCC) |
| Android | NDK Clang |

`__attribute__((cleanup(...)))` is supported by all of the above and is the
canonical RAII mechanism for this project.  MSVC is explicitly not a target.

**Rules for new code:**
- Never add a new POSIX/platform-specific call without noting it in the table above.
- Platform differences are resolved by the **build system, not `#ifdef` macros**.
  See the Platform Abstraction section below.
- Android TUI works only inside a terminal emulator; non-interactive (batch) mode must always be functional.

## Platform Abstraction

Platform-specific behaviour is isolated in `src/platform/`.  The layer exposes
a thin C header with a single canonical interface; each platform provides its
own implementation file.  CMake selects the right source file at configure
time — **no `#ifdef` guards in shared code**.

```
src/platform/
  terminal.h          ← canonical interface (get_cols, set_raw, restore, …)
  posix/terminal.c    ← termios + ioctl  (Linux, macOS, Android)
  windows/terminal.c  ← GetConsoleMode + GetConsoleScreenBufferInfo

  path.h              ← home_dir(), cache_dir(), config_dir()
  posix/path.c        ← $HOME / XDG
  windows/path.c      ← %USERPROFILE% / %APPDATA%
```

**Allowed `#ifdef` use:** only inside a platform implementation file itself
(e.g. to distinguish Linux vs macOS within the POSIX backend), never in
`core/`, `domain/`, `infrastructure/`, or `main.c`.

## Dependency Inversion for Testability

All system and library IO calls are wrapped in thin functions declared in shared
headers.  **No Dependency Injection** (no vtables, no runtime function pointers).
CMake selects which implementation to link at build time.

**Production** links real implementations. **Tests** link mock implementations
from `tests/mocks/`. See [ADR-0004](docs/adr/0004-dependency-inversion.md).

```
src/core/crypto.h        ← crypto_sha256(), crypto_aes_encrypt_block(), ...
src/core/crypto.c        ← OpenSSL implementation
tests/mocks/crypto.c     ← mock with call counters and controlled outputs

src/platform/socket.h    ← sys_socket(), sys_connect(), sys_send(), ...
src/platform/posix/      ← POSIX implementation
tests/mocks/socket.c     ← fake server for integration tests
```

**Rule:** Never call OpenSSL (`SHA256()`, `AES_encrypt()`) or POSIX socket
functions directly from module code. Always go through the `crypto.h` /
`socket.h` wrappers.

## Documentation

```
docs/
  README.md               ← index
  dev/                    ← developer guides (testing, logging, MTProto reference)
  adr/                    ← Architecture Decision Records (CLEAN arch, RAII, test framework, DIP)
```

## Project Memory

Current project status, architectural decisions, and user preferences are tracked in `.claude/memory/`. Read these files at the start of each session for context on what has been done and what comes next.
