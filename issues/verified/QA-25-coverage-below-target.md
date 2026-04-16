# Line coverage below 90% target for core and infrastructure modules

## Description
The project targets >90% line coverage for `src/core/` and `src/infrastructure/`.
Current coverage (as of 2026-03-31) for several modules is significantly below target:

| Module | Lines Hit/Total | Coverage | Gap |
|--------|----------------|----------|-----|
| `api_call.c` | 31/63 | 49.2% | -40.8% |
| `cache_store.c` | 45/77 | 58.4% | -31.6% |
| `mtproto_rpc.c` | 119/181 | 65.7% | -24.3% |
| `transport.c` | 47/62 | 75.8% | -14.2% |
| `config_store.c` | 51/59 | 86.4% | -3.6% |
| `terminal.c` | 30/122 | 24.6% | -65.4% |

Key untested areas:
- `api_call.c`: `api_call_init()` function entirely untested
- `cache_store.c`: eviction logic, error paths
- `mtproto_rpc.c`: encrypted send/recv paths (only unencrypted tested)
- `transport.c`: extended length prefix paths, partial recv
- `terminal.c`: raw mode, key reading, wcwidth (hard to test without TTY mock)

Overall: 83.8% line coverage vs 90% target.

## Severity
LOW — no runtime bug, but reduced confidence in untested code paths.

## Steps
1. Add tests for `api_call_init()` and `api_wrap_query()` edge cases
2. Add tests for `cache_evict()` and cache error paths
3. Add encrypted send/recv tests using mock socket + mock crypto
4. Add transport tests for extended prefix and partial send/recv
5. Consider adding terminal mock for `terminal.c` coverage
6. Track coverage per-module in CI to prevent regression

## Estimate
~300-400 lines of new test code

## Dependencies
QA-08 (integration tests — related but separate scope)

## Verified — 2026-04-16
Core + infrastructure (TUI excluded) line coverage measured with
gcov/lcov:

| Scope | Before | After |
|-------|--------|-------|
| core (w/o readline.c TUI) | 73.8% | 87.8% |
| infrastructure | 81.5% | 89.6% |
| core + infra (w/o readline) | 78.1% | 89.9% |

`readline.c` intentionally excluded — it exercises terminal raw-mode
paths that require a PTY harness; tracked as US-02.
