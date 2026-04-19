# Developer Guide: Coverage

`./manage.sh coverage` runs a **two-pass lcov capture**: once for the unit
suite, once for the functional suite, then merges them into a combined
report. The two separate tracefiles are preserved so we can publish two
badges — combined and functional-only — and so readers can see what the
functional suite adds on top of unit coverage.

## Running Locally

```bash
./manage.sh coverage
```

Outputs:

```
build/coverage-unit.info           lcov tracefile — unit suite only
build/coverage-functional.info     lcov tracefile — functional suite only
build/coverage.info                unit ∪ functional
build/coverage_report/index.html   HTML — combined
build/coverage_functional_report/index.html  HTML — functional only
```

Open either `index.html` in a browser for a per-file breakdown.

## Why Two Passes

Both suites are linked against the same production libraries
(`tg-proto`, `tg-domain-read`, `tg-domain-write`, `tg-app`), so running
them back-to-back with a single `.gcda` accumulation hides the individual
contributions. The workflow:

1. Wipe all `.gcda` files.
2. Run `build/tests/unit/test-runner`. Capture → `coverage-unit.info`.
3. Wipe all `.gcda` files again.
4. Run `build/tests/functional/functional-test-runner`. Capture →
   `coverage-functional.info`.
5. `lcov --add-tracefile unit --add-tracefile functional` →
   `coverage.info` (union: a line is covered if *either* suite touched it).

This makes "what does the functional suite cover on its own?" answerable
at a glance — handy when auditing US-17 acceptance.

## Current Numbers

As of 2026-04-19:

| Tracefile | Lines | Coverage |
|-----------|-------|----------|
| `coverage-unit.info` | ~15 300 / ~17 685 | ~86.5 % |
| `coverage-functional.info` | ~4 044 / ~7 718 (shared source only) | ~52 % |
| `coverage.info` (combined) | ~15 588 / ~17 685 | **~88.1 %** |

Functional coverage is intentionally lower: it skips pure crypto /
parsing primitives that have their own known-answer tests, and focuses
on the RPC + domain layer as seen through a real encrypted wire.

## GitHub Actions + Pages

The `Coverage` workflow (`.github/workflows/coverage.yml`) runs on every
push to `main`. It:

1. Runs `./manage.sh coverage` in a fresh runner.
2. Feeds both tracefiles through `lcov_cobertura` → Cobertura XML.
3. Calls `genbadge coverage -i <xml> -o <svg>` twice:
   - `coverage_report/coverage-badge.svg` — combined percent.
   - `coverage_report/coverage-functional-badge.svg` — functional-only
     percent, with the badge label "functional coverage".
4. Nests the functional report under `coverage_report/functional/` so a
   single Pages artifact serves both reports:

   ```
   coverage_report/
     index.html                        ← combined report
     coverage-badge.svg                ← combined badge
     coverage-functional-badge.svg     ← functional-only badge
     functional/
       index.html                      ← functional-only report
       coverage-badge.svg              ← copy of the functional badge
   ```

5. Publishes the artifact via `actions/deploy-pages@v4`.

`README.md` links both badges — the combined one to
`https://csjpeter.github.io/tg-cli/` and the functional one to
`https://csjpeter.github.io/tg-cli/functional/`.

## Reading a Coverage Drop

If a PR drops either number, the usual culprits:

1. **New untested code path** — add a unit or functional test as
   appropriate. Domain-layer additions should get a functional test so
   they're exercised end-to-end through real crypto.
2. **Refactor that moved a branch into an untested file** — check the
   per-file diff in the HTML report.
3. **Functional-only drop** — a new domain function that was covered by
   unit tests only. Write a functional test for it (see
   [`mock-server.md`](mock-server.md)).

The CI gate is currently advisory — no hard threshold fails the build —
but let's keep both numbers trending up, not down.

## Requirements by Layer

See [`testing.md`](testing.md#coverage-requirements) for the per-layer
targets. The short version: `src/core/` + `src/infrastructure/` combined
must stay at **≥ 90 %** lines. Everything else is best-effort.
