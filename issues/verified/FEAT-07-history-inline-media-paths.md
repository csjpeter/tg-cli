# FEAT-07 — render `[photo: /path]` inline in history output

## Gap
US-08 acceptance:
> `history <peer>` renders media references as filesystem paths.
> Re-running the command reuses cached files (no re-download).

Current history output shows `(media)` or `(complex)` markers only.
The photo-download primitives exist (`domain_download_media_cross_dc`)
but are not wired into the history render path.

## Scope
1. After parsing a history batch, iterate messages whose media is a
   photo, skip if `--no-media` (FEAT-05).
2. Download to `~/.cache/tg-cli/media/photo-<photo_id>.jpg` via
   `cache_store`; reuse existing file if mtime is recent.
3. Print `[photo: <abs_path>]` inline in the message line (after the
   text) or as its own line in `--json` output.
4. Document downloads as follow-up (depends on FEAT-06).

## Acceptance
- `tg-cli-ro history @peer` prints an absolute path for photo-bearing
  messages.
- Running twice triggers zero network calls the second time (cache
  hit).
