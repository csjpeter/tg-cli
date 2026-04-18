# US-15 — Cross-DC media routing (upload + download)

Applies to: all three binaries.

**Status:** done — shipped as P10-01..P10-05.

## Story
As a user I want every upload and download to "just work" even when
Telegram has placed the target file on a different data centre from my
home DC, without any manual DC juggling.

## Scope
- Multi-DC session store (v2 file on disk, up to 5 DCs)
- `DcSession` primitive: fast path (cached auth_key) + slow path
  (fresh DH handshake + `auth.exportAuthorization` → `auth.importAuthorization`)
- Download path: `upload.getFile` → `FILE_MIGRATE_X` → open target DC
  → authorize → retry
- Upload path: `upload.saveFilePart` / `saveBigFilePart` →
  `NETWORK_MIGRATE_X` / `FILE_MIGRATE_X` → open target DC → authorize
  → regenerate `file_id` → retry the whole upload there.
  `messages.sendMedia` stays on the home DC and references the
  foreign-uploaded `file_id`.

## UX
Invisible to the user — manifests only as "it works" instead of a
cryptic error when hitting a non-home-DC file.

## Acceptance
- Fresh DC session handshake completes under ≈ 1.5 s on a normal link.
- Cached foreign sessions skip the full handshake on every subsequent
  request.
- `dc_session_ensure_authorized` reused by both upload and download
  paths — no duplication.

## Dependencies
US-08 (download) · US-14 (upload) · P10-01..P10-05.
