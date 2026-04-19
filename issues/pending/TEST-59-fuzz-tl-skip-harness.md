# TEST-59 — Extend libFuzzer harness to tl_skip + updates parser

## Gap
`tests/fuzz/fuzz_tl_parse.c` (from TEST-40) exercises ~40 tl_serial/
tl_skip entry points.  Two high-risk paths are NOT in the harness:

1. `tl_skip_message` — the message wrapper that re-enters tl_skip
   recursively.  A malformed nested container or a CRC that points
   to a self-referencing type can cause infinite recursion, which
   the current harness won't catch because it doesn't drive that
   entry.
2. `parse_updates_difference` in `src/domain/read/updates.c` — this
   handles `updates.Difference | differenceSlice | differenceEmpty |
   differenceTooLong` plus a nested vector of `message.Message`.
   Crafted input here would let a server crash the client without
   ever touching raw TL.

## Scope
1. Add a second fuzzer binary `fuzz_parse_updates` targeting
   `parse_updates_difference` (or its test-exposed helper).
2. Add `fuzz_tl_skip_message` that feeds a single-message envelope
   into `tl_skip_message`.
3. Add a reasonable recursion guard in `tl_skip` (max 32 frames) and
   add a seed corpus that exercises the guard.

## Acceptance
- Both fuzzers build under `./manage.sh fuzz`.
- Running each fuzzer for 30 s on the existing seed corpus finds
  zero crashes / hangs / OOMs.
- A seed that would previously crash (add one to demonstrate) is
  handled without UB.

## Dependencies
- TEST-40 fuzzer infrastructure verified.
