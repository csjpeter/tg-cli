# TEST-62 — CI: per-platform smoke runs (macOS, Windows MinGW, Android NDK)

## Gap
`.github/workflows/ci.yml` already has jobs that *build* on macOS,
Windows-MinGW, and Android NDK (CI-03).  None of them *run* the test
suite: the macOS and Android jobs cross-build but never execute
`./manage.sh test`, and the Windows job is build-only.

Platforms without running tests can silently accumulate portability
regressions — wcwidth bugs, Winsock2 recv EOF handling, Android
pathing.

## Scope
1. macOS CI: add `./manage.sh test` step after build.  Filter to
   tests that don't need Linux-specific tooling.
2. Windows MinGW CI: run the unit-test subset (skip tests that
   depend on POSIX signals or `fork`).
3. Android NDK: cross-build is enough for now, but add a simple
   `./manage.sh build && file bin/tg-cli-ro` sanity to confirm the
   binary is an ARM64 ELF.

## Acceptance
- macOS job runs and passes `./manage.sh test`.
- Windows MinGW job runs a subset named `test_*_portable`.
- Android job verifies the binary shape.
- All three jobs fail the build if the filtered test subset fails.

## Dependencies
- FEAT-20 / FEAT-22 already support Windows.
- Some tests may need `#if !defined(_WIN32)` guards for fork(); this
  ticket may spawn smaller fixes.
