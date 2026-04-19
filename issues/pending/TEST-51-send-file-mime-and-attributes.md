# TEST-51 — Functional test: send-file sets MIME + Document attributes correctly

## Gap
`domain_send_file` currently leaves MIME detection to `domain_path_is_image`
(extension-based) and does not set audio/video attributes (duration,
width, height, thumbnail).  No FT verifies:

- `.mp4` sent as document gets `video` attributes (duration, w, h).
- `.mp3` sent as document gets `audio` attributes (title, performer).
- `.txt` sent as document has mime "text/plain" (or "application/octet-stream").
- A bare filename with no extension falls back to a sensible MIME.

Users who depend on Telegram clients previewing the uploaded file as
a video/audio rather than a generic document will get a silent
downgrade on regression.

## Scope
Create `tests/functional/test_send_file_mime.c`:
- Prepare 4 tiny test fixtures under `tests/fixtures/` (byte-accurate
  MP4 moov box, a 1-frame MP3, a 5-byte txt, a no-extension file).
- For each, call `domain_send_file`; capture the sent TL payload
  via mock arg capture.
- Assert the `InputMediaUploadedDocument` carries the expected
  `mime_type` and attribute list.

## Acceptance
- 4 sub-tests pass.
- If `domain_send_file` does NOT currently derive attributes, this
  ticket spawns a sibling FEAT to implement the detection before the
  test can pass.  Commit the test first as `expected_fail` if so.

## Dependencies
- May require `FEAT-26 — detect video/audio attributes on upload`.
