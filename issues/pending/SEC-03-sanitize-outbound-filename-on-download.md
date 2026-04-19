# SEC-03 — Sanitize server-supplied filenames on download (path traversal)

## Gap
`cmd_download` in `tg_cli_ro.c:783-797` composes the output path as
`<cache>/downloads/<entry.media_info.document_filename>` when the
caller does not supply `--out`.  `document_filename` comes straight
from the server and may contain:

- `../` sequences → escapes the downloads dir.
- Absolute paths like `/etc/passwd` → overwrites system files.
- NUL bytes → silent truncation.
- Very long names → file system refuses to create.

Current code does not sanitize.  A malicious peer sending a photo
with `filename=../../../../../../tmp/pwn` could write outside the
intended dir.

## Scope
1. Add `domain_safe_filename(const char *in, char *out, size_t cap)`
   in `src/domain/read/media.c`:
   - Strip directory separators `/` and `\`.
   - Reject `..` segments.
   - Collapse NUL → truncate at first NUL.
   - Clamp length to 200 bytes (leaving room for cache prefix).
   - Replace any control byte < 0x20 with `_`.
   - If result is empty, fall back to `doc-<id>`.
2. Apply at the composition site in `cmd_download` and the TUI
   `do_download`.
3. Add `tests/functional/test_download_path_sanitize.c`:
   - Mock sends filename `../../../tmp/pwn`.
   - Call `cmd_download` with no `--out`.
   - Assert the saved path stays under `<cache>/downloads/`.
   - Assert the sanitized filename survives in the media index.

## Acceptance
- All sanitization cases exercised.
- `--out` path is NOT sanitized (user is trusted to pick their path).
- `man tg-cli-ro` SECURITY section mentions the filename sanitization.
