# TEST-75 — Functional coverage for TL forward compatibility

## Gap
`src/core/tl_skip.c` is 13.6 % functionally covered. The skip
table handles most TL types but has never been driven by an
unknown-CRC payload in a functional test — only unit coverage
exercises individual skip entries.

## Scope
Mock-server extension: `mt_server_reply_with_unknown_type(crc,
envelope_kind)` that wraps a fabricated 4-byte unknown CRC plus
trailing padding inside one of: result, history message, media,
service action.

New suite `tests/functional/test_tl_forward_compat.c`:

1. `test_unknown_top_level_result_skipped` — `messages.getDialogs`
   returns a response with an unknown trailing field; known dialog
   entries still print.
2. `test_unknown_media_in_history` — unknown CRC where
   `MessageMedia` is expected; history line prints "[unknown
   media]" and the text field is preserved.
3. `test_unknown_message_action` — unknown service action; message
   row shows id + date, action labelled "[unknown action]".
4. `test_unknown_optional_field_preserves_layout` — known
   `message` type with a new optional bit set and a trailing value
   we do not understand; all known fields parsed correctly.
5. `test_unknown_update_type_in_getdifference` — watch flow sees
   an unknown Update CRC, log-warns and continues polling.

## Acceptance
- 5 tests pass under ASAN.
- Functional coverage of `tl_skip.c` ≥ 40 %.
- No regression in the unit-level tl_skip suite.

## Dependencies
US-24 (the story). ADR-0007 (mock server).
