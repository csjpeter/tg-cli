# TEST-50 — Forward: accept multiple msg_ids OR document single-id limit

## Gap
`cmd_forward` wires a single `args.msg_id` into a 1-element `ids[]`
array, then `domain_forward_messages` declares `ids[], count`.  The
arg parser accepts only one `<msg_id>`.  The help text says
"Forward one message" — but the function signature hints at batching.

Two possible resolutions:
A. Extend arg parser to accept `--ids "1,2,3"` and forward as a
   vector.
B. Keep the single-id contract, but add an FT that verifies the
   batch-capable domain call correctly forwards multiple ids — so a
   scripted caller can use the domain layer directly.

## Scope
Choose A or B and add the matching FT.

Proposed choice: **B** (keep CLI simple; add unit-level FT).
- Amend `test_write_path.c`:
  `test_forward_messages_batch_of_three` — call
  `domain_forward_messages(..., ids={10,11,12}, count=3, ...)` and
  assert the mock sees all three ids in a single RPC.

## Acceptance
- Batch forward FT passes.
- Man page explicitly states the CLI wrapper forwards exactly one id
  per invocation (while the domain call supports N).

## Dependencies
- None.
