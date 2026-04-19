# BUILD-02 — No mechanism to run a single test in isolation

## Category
Build / DX

## Severity
Low

## Finding
`CLAUDE.md` documents this limitation: "There is no mechanism to run a single
test in isolation; all unit tests run together via `build/tests/unit/test-runner`."

The test runner (`tests/unit/test_runner.c`) accepts no `argc`/`argv` and has
no filter logic.  With 50+ unit test files and a growing functional suite, the
inability to target a single test slows iteration significantly.

`manage.sh` has no `test <name>` subcommand variant.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/CLAUDE.md` — explicit statement that no single-test isolation exists
- `/home/csjpeter/ai-projects/tg-cli/tests/unit/test_runner.c` — no `argc` handling

## Fix
1. Modify `test_runner.c` to accept an optional `argv[1]` substring filter: only
   call `RUN_TEST(fn)` when `argv[1]` is a prefix of the registered function name.
2. Expose via `manage.sh`: `./manage.sh test test_arg_parse` passes `test_arg_parse`
   to the runner.
3. Apply the same pattern to `functional-test-runner`.
