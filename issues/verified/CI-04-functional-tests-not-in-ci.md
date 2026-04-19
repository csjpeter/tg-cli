# CI-04 — Functional test suite never runs in CI (only in coverage workflow)

## Category
CI

## Severity
High

## Finding
The main CI workflow (`ci.yml`) that runs on every push and PR only exercises
the unit test suite.  The functional test suite (`functional-test-runner`) is
only executed as a side-effect of the `coverage.yml` workflow, which runs only
on pushes to `main` (not on PRs).

This means a PR that breaks the mock-server-backed functional tests (login flow,
read/write path, TL parsing end-to-end) will pass CI green and merge.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/ci.yml` — no `functional-test-runner` step
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/coverage.yml:30` — `run: ./manage.sh coverage`
  (which does run the functional runner, but only on `push` to `main`, not PRs)

## Fix
Add a step to `ci.yml` that:
1. Configures a Debug build.
2. Builds `functional-test-runner`.
3. Executes it.

A new `manage.sh` command (e.g. `functional-test`) would make this one line.
See also CI-01 for ASAN on the functional runner.
