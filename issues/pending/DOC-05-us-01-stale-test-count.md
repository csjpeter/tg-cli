# DOC-05 — US-01 final-state counts are stale

## Gap
`docs/userstory/US-01-test-coverage.md` acceptance (line 33) says
"1626/1626 pass", coverage block (line 8) says "87.8% overall". Current
reality: 2703 unit + 427 functional tests green, ~88 % combined line
coverage (of which ~92 % on core+infra).

## Scope
Refresh US-01's "Final state" block and "Acceptance" bullets:
- 2703 unit tests (ASAN clean) + 427 functional tests.
- Combined coverage ~88 %; core+infra ≥ 92 %.
- Functional-only coverage ~52 % (link to
  `docs/dev/coverage.md`).

## Acceptance
- US-01 reflects the post-US-17 numbers.
- `grep -rn "1626" docs/` returns zero hits.
