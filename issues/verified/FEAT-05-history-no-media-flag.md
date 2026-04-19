# FEAT-05 — `--no-media` flag for history

## Gap
US-08 acceptance:
> A `--no-media` flag skips downloads.

Not parsed anywhere; `history` currently never auto-downloads media,
so either (a) implement FEAT-07 (inline media fetch) and gate on this
flag, or (b) keep history inert and treat `--no-media` as a no-op for
forward compatibility.

## Scope
Coupled with FEAT-07 (inline media). Ticket lands together with it so
history has the promised behaviour when media is not skipped.

1. `arg_parse.c:parse_history` accepts `--no-media`.
2. When absent and FEAT-07 is on, auto-download referenced media into
   the cache; print `[photo: <path>]`.
3. When present, leave the reference in place (`[photo: <photo_id>]`).

## Acceptance
- `history @peer --no-media` does zero `upload.getFile` calls.
- `history @peer` (no flag) downloads referenced photos and prints
  the local path.
