# US-14 — File + photo upload (small, big, auto-dispatch)

Applies to: `tg-tui`, `tg-cli`. **Never** `tg-cli-ro`.

**Status:** done — shipped as P6-02 + LIM-01.

## Story
As a user I want to attach a local file to a chat as either a document
(PDF, mp4, …) or, when the file is an image, as a proper Telegram
photo that renders inline in every client.

## Scope
- Chunked `upload.saveFilePart` for files ≤ 10 MiB → `InputFile`
- Chunked `upload.saveBigFilePart` for files > 10 MiB → `InputFileBig`
  (capped at `UPLOAD_MAX_SIZE = 1.5 GiB`)
- Final RPC:
  - `messages.sendMedia` + `inputMediaUploadedDocument`
    (filename + caption + mime heuristics)
  - `messages.sendMedia` + `inputMediaUploadedPhoto` when
    `domain_path_is_image(path)` matches `.jpg/.jpeg/.png/.webp/.gif`
    (case-insensitive)

## UX
```
tg-cli send-file <peer> <path> [--caption TEXT]
tg-cli upload    <peer> <path> [--caption TEXT]    # alias
```
TUI: `upload <peer> <path> [caption]` REPL verb. Auto-dispatch picks
photo vs document — no separate `send-photo` flag needed.

## Acceptance
- File ≤ 10 MiB: uploads via small-file path; wire carries
  `inputFile#f52ff27f`.
- File > 10 MiB: uploads via big-file path; wire carries
  `inputFileBig#fa4f0bb5`.
- `.png` auto-dispatches to `inputMediaUploadedPhoto#1e287d04`.
- Null / missing path surfaces a clear error with non-zero exit.

## Dependencies
US-12 (RPC plumbing) · P6-02 · LIM-01.
