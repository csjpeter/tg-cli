# FEAT-29 — tg-cli-ro history --offset: reject negative values explicitly

## Gap
`cmd_history` uses `int offset = args->offset > 0 ? args->offset : 0;`
which silently coerces negative offsets to 0.  The arg parser accepts
any integer (including -100).  The man page does not state that
negative values are silently accepted and clamped — users may expect
an error or a "scroll back from tail" semantics.

TEST-46 flags this in its acceptance list.

## Scope
1. Make `arg_parse` reject `--offset < 0` with a clear error, OR
2. Accept negatives and document them as "from the tail" with the
   corresponding domain semantics.

Proposed: reject.  Negative `--offset` has no server-side meaning
in `messages.getHistory` where `offset_id` is a message-id marker.

## Acceptance
- `tg-cli-ro history self --offset -5` exits non-zero with stderr
  "offset must be >= 0".
- Positive values unchanged.
- TEST-46 case 5 passes.
