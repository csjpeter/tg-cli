# TEST-41 — Valgrind not run on functional test suite

## Category
Test

## Severity
Low

## Finding
`manage.sh valgrind` and `valgrind.yml` CI job run Valgrind only on
`build/tests/unit/test-runner`.  The functional test suite
(`functional-test-runner`) — which exercises the full stack including
`mtproto_auth`, `transport`, `api_call`, and domain layers with real OpenSSL —
is never checked under Valgrind.

Memory leaks in auth or RPC teardown paths are more likely to appear in the
functional suite (which exercises multi-call sequences) than in the isolated
unit tests.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/manage.sh:129` — `valgrind ... "$BUILD_DIR/tests/unit/test-runner"` only
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/valgrind.yml:24` — `run: ./manage.sh valgrind`

## Fix
Add `./manage.sh valgrind-functional` that runs:
```bash
build_release
build_functional_runner
valgrind --leak-check=full --error-exitcode=1 \
    "$BUILD_DIR/tests/functional/functional-test-runner"
```
Add a corresponding CI job or extend `valgrind.yml`.
