# FEAT-26 — Detect video/audio attributes when uploading as document

## Gap
`domain_send_file` in `src/domain/write/upload.c` currently uploads
any non-image file as a generic `InputDocument` with no attributes.
Telegram clients then render it as a plain file icon even for `.mp4`,
`.mp3`, `.webm`, `.ogg`.  Users expect:

- `.mp4`/`.mov`/`.webm` → `DocumentAttributeVideo{duration, w, h,
  supports_streaming}`.
- `.mp3`/`.ogg`/`.flac` → `DocumentAttributeAudio{duration, performer,
  title}`.
- `.png`/`.jpg` → already handled as `sendPhoto` via `domain_path_is_image`.

Without these, videos lack inline preview and audio lacks track
metadata.

## Scope
1. Add a minimal MIME/attribute detector based on file-magic bytes
   + extension:
   - `is_video_mime(path)` → mime + `DocumentAttributeVideo`.
   - `is_audio_mime(path)` → mime + `DocumentAttributeAudio`.
2. For video: parse the MP4 `mvhd` box or WebM segment info to read
   duration + dimensions.  If parsing fails, still set mime +
   `supports_streaming=false`.
3. For audio: parse ID3v2 header (if present) for title/performer;
   duration from `mpegts` timestamp or best-effort 0.
4. Wire results into `domain_send_file` as additional TL attributes.
5. Pair with TEST-51 (already queued) to verify.

## Acceptance
- Upload of a 5-byte MP4 stub sets mime "video/mp4" and attribute
  vector contains `DocumentAttributeVideo`.
- Upload of an MP3 file sets mime "audio/mpeg".
- Upload of `.txt` still works as before (generic document).

## Dependencies
- Must land before TEST-51 is verified (TEST-51 will xfail until).
