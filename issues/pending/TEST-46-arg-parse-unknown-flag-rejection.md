# TEST-46 — Functional test: arg parser rejects unknown flags / missing values

## Gap
`arg_parse.c` handles many flags, but there is no FT verifying that:

- An unknown flag like `--frobnicate` produces ARG_ERROR (`-1`).
- A flag that takes a value but is placed at argv[-1]
  (e.g. `tg-cli-ro dialogs --limit`) is rejected.
- A subcommand typo like `dialog` (singular) is rejected with a
  useful message instead of silently falling through to `CMD_NONE`.
- Conflicting flags (e.g. `--batch` + prompt-only flow) behave
  consistently.

These are trivial in source but every regression against them
breaks user scripts.

## Scope
Create `tests/functional/test_arg_parse_rejections.c`:
- Drive `arg_parse` directly with crafted `argv` arrays (no child
  process needed).
- Cases:
  1. `["tg-cli-ro", "--frobnicate"]` → ARG_ERROR.
  2. `["tg-cli-ro", "dialogs", "--limit"]` → ARG_ERROR.
  3. `["tg-cli-ro", "dialogz"]` → ARG_ERROR (unknown subcommand).
  4. `["tg-cli-ro", "--config"]` (no value) → ARG_ERROR.
  5. `["tg-cli-ro", "history", "--offset", "-5"]` → ARG_ERROR
     (negative offset rejected, or sat to 0 — whichever is the
     documented contract; decide and pin).
  6. `["tg-cli-ro", "watch", "--interval", "1"]` → ARG_ERROR
     (< 2 rejected per doc).
  7. `["tg-cli-ro", "watch", "--interval", "10000"]` → ARG_ERROR
     (> 3600 rejected per doc).

## Acceptance
- All 7 cases pass.
- Error message written to stderr is non-empty for each.
- Exit code is consistent (1 for usage errors).
