# Media info display

## Description
Recognize MessageMedia objects in messages.getHistory responses and display file name, size, type in history output.

## Types
- messageMediaPhoto → "[Photo: {size}]"
- messageMediaDocument → "[File: {filename} ({size})]"
- messageMediaGeo → "[Location: {lat},{lon}]"
- messageMediaContact → "[Contact: {name} {phone}]"
- messageMediaWebPage → "[Link: {url}]"

## Estimate
~100 lines

## Dependencies
- P5-02 (pending) — messages.getHistory válasz parszolása (MessageMedia objektumok)

## Verified — 2026-04-16 (v1 — kind + ids)
- `src/core/tl_skip.h` exposes `MediaKind` enum +
  `MediaInfo { kind, photo_id, document_id, dc_id }` plus
  `tl_skip_message_media_ex(r, *out)` which fills MediaInfo while
  skipping. Original `tl_skip_message_media` wraps the new one
  with NULL out.
- `HistoryEntry` carries `media`, `media_id`, `media_dc` fields.
  history.c / search.c / updates.c parsers populate them.
- `tg-cli-ro history` output now shows `[photo:<id>]` /
  `[doc:<id>]` / `[geo]` / `[venue]` prefixes before message text.
  JSON gains `media` + `media_id`.
- Regression test: `test_history_media_photo_info` verifies the
  photo_id from a messageMediaPhoto with photoEmpty reaches the
  returned HistoryEntry.

## Follow-up
Full `upload.getFile` chunked download is still P6-01.
