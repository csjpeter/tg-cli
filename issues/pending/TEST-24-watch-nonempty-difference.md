# TEST-24 — functional test: watch delivers actual new messages

## Gap
`tests/functional/test_read_path.c` contains only one
`updates.getDifference` test:
```c
test_updates_difference_empty  /* TL_updates_differenceEmpty → 0 messages */
```
The non-empty paths (`TL_updates_difference#00f49d63` and
`TL_updates_differenceSlice`) are parsed in
`src/domain/read/updates.c:205` but have **zero** functional test
coverage. A regression in the new-message extraction loop (text,
date, id, `complex` flag) would not be caught.

US-07 acceptance: "Streams one line per incoming message to stdout."
This is the core promise of `watch` and is untested at the
functional level.

## Scope
1. Add `test_updates_difference_with_messages` to `test_read_path.c`:
   - Responder emits `TL_updates_difference` with a `Vector<Message>`
     containing one message with known `id`, `date`, and `message` text.
   - Assert `diff.new_messages_count == 1`.
   - Assert `diff.new_messages[0].id`, `.date`, `.text` match.
2. Add `test_updates_differenceSlice` (same content, different
   constructor CRC) to verify the slice branch is also exercised.

## Acceptance
- Both new tests pass under ASAN and Valgrind.
- Coverage report shows `updates.c` difference-parsing branches covered.
