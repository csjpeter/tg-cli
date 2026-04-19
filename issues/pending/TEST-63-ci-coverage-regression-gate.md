# TEST-63 — CI: coverage regression gate (≥ 90% target per CLAUDE.md)

## Gap
CLAUDE.md states "GCOV/LCOV coverage report (>90% goal for core/
infra)".  There is a `./manage.sh coverage` command but no CI job
that runs it and fails on regression.  Coverage can drift downward
silently commit by commit.

## Scope
1. Add a `coverage` job to `.github/workflows/ci.yml`:
   - Run `./manage.sh coverage` and extract the per-module %.
   - Compare against a checked-in baseline (`ci/coverage-baseline.json`).
   - Fail if any of `core/`, `infrastructure/` drops by > 1%.
   - Post summary as a PR comment.
2. Pick thresholds: `core >= 90%`, `infrastructure >= 90%`,
   `domain/read >= 85%`, `domain/write >= 85%`.
3. Commit the current baseline JSON.

## Acceptance
- A PR that removes a test fails the coverage gate.
- Baseline file is human-readable and tracked in git.

## Dependencies
- No prereqs — LCOV infra already present.
