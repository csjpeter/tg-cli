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
