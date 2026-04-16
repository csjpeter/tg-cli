# US-01 — Increase core+infra test coverage >90%

## Story
As a developer, I want the core and infrastructure modules to have >90% line
coverage so that silent regressions in error paths cannot slip through.

## Final state (2026-04-16)
overall: 87.8% (was 84.5%) · core w/o readline: 95.6% · infra: 89.9%
core+infra w/o readline: **92.1%** ✓ (goal met)
core+infra incl. readline: 84.4% — blocked by US-02 (TUI testability)

## Gaps closed
| Module | Before → After | Work done |
|--------|----------------|-----------|
| `transport.c` | 61% → **100%** | 3-byte prefix, partial send/recv, NULL args |
| `cache_store.c` | 58% → 90% | `cache_evict_stale`, mkdir failures |
| `auth_session.c` | 70% → 87% | unexpected CRC, FlashCall type, api_call failures |
| `arg_parse.c` | 79% → **99%** | numeric parse failures, print helpers |

Infrastructure for future tests: `mock_socket` gained injectable
create/connect/send/recv failure counters.

## Strategy
1. Extend `tests/mocks/socket.c` with injectable send/recv failure counters
   (single call site change, no public API churn).
2. Add error-path unit tests for each module above via mocks/crafted inputs.
3. Re-run `./manage.sh coverage` — confirm core+infra ≥ 90%.
4. Note `readline.c` (18%) and `platform/posix/terminal.c` (20%) stay below
   target — tracked separately by **US-02** because they need a PTY/TTY
   abstraction.

## Acceptance ✓
- `./manage.sh test` — 1626/1626 pass, 0 ASAN errors.
- `./manage.sh valgrind` — 0 leaks, 0 errors.
- `./manage.sh coverage` — core+infra (TUI excluded) = 92.1%.

## Related issues
QA-25 (coverage-below-target).
