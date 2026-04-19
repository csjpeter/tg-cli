# TEST-31: arg_parse: --limit 0 and negative --limit accepted silently for dialogs/history

## Location
`src/core/arg_parse.c` – `parse_dialogs()`, `parse_history()`

## Problem
`parse_search()` validates `--limit` in the range [1, 100]:
```c
if (val < 1 || val > 100) { fprintf(stderr, "out of range"); return ARG_ERROR; }
```

`parse_dialogs()` and `parse_history()` do **not** validate — any integer,
including 0 and negative values, is accepted.  Passing `--limit 0` or
`--limit -5` to the Telegram API would result in a server-side error (RPC 400)
that is confusing to the user, or silent empty results.

## Fix direction
Add range validation to `parse_dialogs` and `parse_history`:
```c
if (out->limit < 1 || out->limit > 1000) {
    fprintf(stderr, "tg-cli dialogs: --limit out of range [1, 1000]\n");
    return ARG_ERROR;
}
```

## Missing tests
`tests/unit/test_arg_parse.c` has no tests for `--limit 0` or `--limit -1` for
`dialogs` or `history` subcommands.  Add:
- `test_dialogs_limit_zero_is_error`
- `test_dialogs_limit_negative_is_error`
- `test_history_limit_zero_is_error`
