# CI-03 — No CI for macOS, Windows, or Android target platforms

## Category
CI / Portability

## Severity
Medium

## Finding
`CLAUDE.md` lists four target platforms (Linux primary, macOS, Windows, Android)
with dedicated platform source files already present:

- `src/platform/posix/terminal.c` (Linux/macOS/Android)
- `src/platform/windows/terminal.c`
- `src/platform/windows/path.c`
- `src/platform/windows/socket.c`

However, all three CI workflows run exclusively on `ubuntu-24.04`.  There is no
`macos-latest` runner, no MinGW cross-compile job, and no Android NDK build.
Portability regressions on non-Linux platforms will go undetected.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/ci.yml:11` — `runs-on: ubuntu-24.04`
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/valgrind.yml:14` — `runs-on: ubuntu-24.04`
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/coverage.yml:21` — `runs-on: ubuntu-24.04`

## Fix
Add a CI matrix or separate jobs:

1. **macOS**: add `runs-on: macos-latest`, install `brew install gcc openssl`, run
   `./manage.sh build && ./manage.sh test`.
2. **Windows (MinGW cross-compile)**: add a job using the `windows-latest` runner
   with `msys2/setup-msys2` action, install `mingw-w64-x86_64-gcc` and
   `mingw-w64-x86_64-openssl`, run CMake with `-G"MSYS Makefiles"`.
3. **Android (NDK smoke build)**: optional but noted — add an NDK cross-compile job
   for `aarch64-linux-android` using the NDK toolchain.
