# Portability

tg-cli is built, tested and released for **Linux (Debian) only**. A release for,
or support of, macOS, iOS, Android or Windows is not a goal (owner's decision of
2026-10-10, `policies/platform-scope-model.md`). This file lists what binds the
code to Linux, so that the day a platform becomes a target the work is a known
list. It is a list of what is waiting, not a plan, and not a claim of support.

| Dependency | Where | Would need |
|---|---|---|
| `termios.h` raw mode, `ioctl(TIOCGWINSZ)`, `SIGWINCH` | `src/platform/posix/terminal.c` | a console backend; `src/platform/windows/terminal.c` exists but is untested. TG-11 moves the terminal layer to libtuikit, which then owns this question |
| `wcwidth(3)` / locale | terminal layer | a bundled width table (`src/core/wcwidth.c`); libtuikit has its own |
| `$HOME`, XDG `~/.config` and `~/.cache` | `src/platform/posix/path.c` | per-OS config and cache directories; `src/platform/windows/path.c` is untested |
| POSIX sockets, `poll` | `src/platform/posix/socket.c` | Winsock; `src/platform/windows/socket.c` is untested |
| File locking with `flock`/`fcntl` | `src/app/session_store.c` (`#if !defined(_WIN32)`) | `LockFileEx`, already written in the `_WIN32` branch, untested |
| UTF-8 argv | `src/platform/posix/argv_utf8.c` | wide-character command line on Windows |
| `__attribute__((cleanup))` RAII | `src/core/raii.h` | GCC or Clang only; MSVC is out of scope |
| Bash `manage.sh`, `apt`, Valgrind, LCOV | `manage.sh`, `ci/` | none: the build and test host is Linux regardless |

## Existing platform-compatibility code is preserved

`src/platform/windows/*`, the `_WIN32` branch of `src/app/session_store.c` and
the `if(WIN32)` source selection in `CMakeLists.txt` predate the platform
decision. By the owner's decision (2026-10-10) **they are kept**: not deleted,
not refactored away. They are not built, not tested and not a supported
platform, and no claim is made for them. New code follows the general policy
(`policies/platform-scope-model.md`): no new ports.

## Review of the platform `#ifdef`s (2026-10-10)

The policy asks that a platform `#ifdef` is kept only where it isolates one
small piece of real platform code, and that it is documented here.

| File | `#ifdef` | What it isolates | Verdict |
|---|---|---|---|
| `src/app/session_store.c` (l.36, 124, 160-195, 342) | `_WIN32` | the advisory lock on the session file: POSIX `fcntl`/`flock` against `LockFileEx` | one small, isolated piece; documented above; **kept** (owner's rule: existing compatibility code is preserved) |
| `tests/unit/test_session_store.c` (l.357, 463) | `_WIN32` | two concurrency tests that need POSIX `fork` | follows the one above; **kept** |

No other source file has a platform `#ifdef`. Platform differences elsewhere are
chosen by CMake (`if(WIN32)` in `CMakeLists.txt`), which is the project's rule.
No document or man page claims support for a platform other than Linux; the
file-ticket history under `issues/` is a record of past work and is not
rewritten.
