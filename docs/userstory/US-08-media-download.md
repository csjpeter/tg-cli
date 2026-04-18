# US-08 — Media download with local path display

Applies to: `tg-cli-ro`, `tg-tui`.

**Status:** done — chunked `upload.getFile` for photos and documents
(incl. stickers/customEmoji with thumbs + video_thumbs vectors);
FILE_MIGRATE_X reroutes to the target DC (P6-01, LIM-02, P10-03).

## Story
As a user I want photos, videos, documents referenced in messages to be
downloaded into a local cache and shown as paths I can open in my viewer
of choice.

## Scope
- `upload.getFile` chunked download into `~/.cache/tg-cli/media/`
- Messages show `[photo: /path/to/file.jpg]` etc.
- Eviction policy keeps cache bounded (reuses `cache_store.cache_evict_stale`).

## Acceptance
- `history <peer>` renders media references as filesystem paths.
- Re-running the command reuses cached files (no re-download).
- A `--no-media` flag skips downloads.

## Dependencies
US-06 · P6-01 · P6-03 · F-08.
