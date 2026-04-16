# upload.saveFilePart + messages.sendMedia (file upload)

## Batch
`tg-cli send-file <peer> file.pdf [--caption "text"]`

## Steps
1. `upload.saveFilePart(file_id, part, bytes)` — chunked upload
2. `messages.sendMedia(peer, inputMediaUploadedDocument, ...)` — send

## Estimate
~300 lines

## Dependencies
- P5-01 (pending) — peer azonosítás (messages.sendMedia célpont)
- P3-02 (pending) — bejelentkezés szükséges
- P4-04 (pending, soft) — nagy fájloknál DC migration szükséges lehet

## Completion notes (2026-04-16)
- src/domain/write/upload.{h,c}: domain_send_file reads the file in
  512 KiB chunks, pushes each via upload.saveFilePart#b304a621 (expects
  boolTrue), then constructs a messages.sendMedia with
  InputMediaUploadedDocument + DocumentAttributeFilename + caption.
  Random file_id / random_id come from crypto_rand_bytes.
- v1 caps files at UPLOAD_MAX_SIZE (10 MiB) — the saveBigFilePart
  path + DC migration for media DCs remain follow-ups (P4-04).
- arg_parse learned `send-file <peer> <path> [--caption TEXT]` and a
  new CMD_SEND_FILE enum. tg-cli cmd_send_file dispatches it.
- tg-tui adds an `upload <peer> <path> [caption]` REPL verb.
- tg-cli-ro stays read-only — upload.c never linked into it.
- Tests: test_domain_upload.c (5 cases) — small-file happy path
  (two-frame response: boolTrue then updateShort), missing file,
  empty file, null args, and a wire-inspection test that checks
  upload.saveFilePart CRC + filename + mime type appear on the wire.
- 1965 unit + 131 functional tests green, valgrind clean.
