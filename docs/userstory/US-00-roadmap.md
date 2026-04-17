# US-00 — Product Roadmap & Current Focus

## Vision
Three binaries sharing one MTProto 2.0 codebase:
`tg-cli-ro` (batch, read-only) · `tg-tui` (interactive) · `tg-cli` (batch r/w).
See `docs/SPECIFICATION.md` and `docs/adr/0005-three-binary-architecture.md`.

## Status: v1 MVP complete

All incoming tickets in `issues/` are resolved and live in
`issues/verified/`. The three binaries ship with full authentication
(including 2FA) and the read + write feature surface the user
originally requested.

### Read features (all three binaries)
- `me`, `user-info`, `contacts`, `dialogs` (with titles + @usernames),
  `history` (self + @peer, text + media kind + photo_id/document_id),
  `search` (global + peer), `watch` (poll loop with text), `download`
  (chunked `upload.getFile` photo download via P6-01).

### Write features (tg-tui + tg-cli)
- `send` (+ `--reply` / stdin pipe — P5-03 + P8-03)
- `read` (messages/channels.readHistory dispatch — P5-04)
- `edit`, `delete [--revoke]`, `forward` (P5-06)
- `send-file` / `upload` — chunked `upload.saveFilePart` +
  `messages.sendMedia` with filename + caption (P6-02)

`tg-cli-ro` is compile-time read-only: it never links
`tg-domain-write`, so no mutation call is reachable from that binary
even if an arg-parse bug were exploited (ADR-0005).

### Authentication
- Phone + SMS/app code login with DC migration
  (PHONE/USER/NETWORK_MIGRATE_X)
- Persisted session (`--logout` clears it)
- `bad_server_salt` retry, service-frame draining (bad_msg /
  new_session / ack / pong)
- **2FA / SRP (P3-03)**: account.getPassword + SRP math
  (`S = base^a * base^(u*x) mod p` identity, no huge exponent
  materialization) + auth.checkPassword. Uses new
  `crypto_sha512`, `crypto_pbkdf2_hmac_sha512`, `crypto_bn_mod_mul`,
  `crypto_bn_mod_add`, `crypto_bn_mod_sub`, `crypto_bn_ucmp`
  wrappers — all linkable against either real OpenSSL or the test
  mock.

### Protocol parse coverage (P5-07 phases 1–3c + P5-09 v1)
- tl_skip helpers for Bool, string, Peer, NotificationSound,
  PeerNotifySettings, DraftMessage(empty), MessageEntity
  (20 variants), Vector<MessageEntity>, MessageFwdHeader,
  MessageReplyHeader, PhotoSize (6 variants), Vector<PhotoSize>,
  Photo, Document, MessageMedia (Empty, Unsupported, Geo, Contact,
  Venue, GeoLive, Dice, Photo, Document), ChatPhoto,
  UserProfilePhoto, UserStatus, Vector<RestrictionReason>,
  Vector<Username>, PeerColor, EmojiStatus, Chat (5 variants),
  User (user/userEmpty), Message.
- Extractors: `tl_extract_chat` (id + title),
  `tl_extract_user` (id + name + username),
  `tl_skip_message_media_ex` (kind + photo_id / document_id /
  dc_id + access_hash + file_reference + largest thumb type).
- Phase 3c trailer skippers: `tl_skip_reply_markup` (4 ReplyMarkup
  variants + ~12 KeyboardButton), `tl_skip_message_reactions` (
  `results:Vector<ReactionCount>` + 4 Reaction variants),
  `tl_skip_message_replies` (messageReplies#83d60fc2 including the
  recent_repliers Vector<Peer>), `tl_skip_factcheck` (factCheck#b89bfccf
  including TextWithEntities). Every Message trailer flag now has a
  skipper.

### Security + robustness
14 QA fixes including MITM `new_nonce_hash1` verification (QA-12),
OOM guards on EVP contexts, strict-aliasing fix in
`rpc_send_encrypted`, 4-byte abridged 3-byte prefix, logger
idempotent, config bzero, `crypto_rand_bytes` bounds,
`pq_factorize` UINT32_MAX guard.

## Quality
- **2136 unit tests** passing (ASAN)
- **150 functional tests** passing (real OpenSSL; SHA-512, PBKDF2,
  BN primitives, IGE, MTProto crypto round-trips, full SRP
  client↔server math roundtrip, kitchen-sink Message iteration)
- Valgrind: 0 leaks, 0 errors
- Zero warnings under `-Wall -Wextra -Werror -pedantic`
- Core+infra coverage: ~89% (TUI excluded)

## Known v1 limitations (follow-ups, not blockers)
- Remaining MessageMedia stoppers are now restricted to the heaviest
  variants: full inline `storyItem#79b26a24` (carries StoryFwdHeader,
  MediaArea, PrivacyRule, StoryViews, Reaction — none walked yet)
  and `webPage` with `cached_page` or `attributes`. Everything else
  — Invoice with WebDocument photo or extended_media, Story with
  `storyItemDeleted`/`storyItemSkipped`, Poll, Giveaway, Game
  (photo-only + with Document), PaidMedia, Photo, Document, Geo,
  Contact, Venue, Dice, WebPage (text-only) — iterates cleanly.
- File upload now handles both small files (<10 MiB via
  `upload.saveFilePart` + `InputFile`) and big files
  (`upload.saveBigFilePart` + `InputFileBig`, capped at
  `UPLOAD_MAX_SIZE = 1.5 GiB`). Cross-DC media routing (uploading
  to / downloading from a non-home DC) is still follow-up under
  P4-04.
- Only documents are uploaded; photo upload (scaled InputMedia) is
  future work.
- Document download now works for the common "plain document" case
  (PDF, mp4, mp3, etc). Documents with Sticker / CustomEmoji attributes
  or populated thumbs/video_thumbs vectors still fall back to the
  complex-iteration bail.

## Backlog (post-MVP polish)
1. **Cross-DC media routing** ✅ complete
   - P10-01 ✅ multi-DC session store (v2 file, up to 5 DCs)
   - P10-02 ✅ DcSession primitive (fast/slow handshake path)
   - P10-03 ✅ FILE_MIGRATE_X → open DcSession → retry on `upload.getFile`
     download path (photo + document)
   - P10-04 ✅ `auth.exportAuthorization` / `auth.importAuthorization`
     — freshly handshaked foreign sessions now import authorization
     from the home DC before the retry; cached sessions skip this
     (server-side authorization stays bound to the auth_key). Wired
     into `domain_download_media_cross_dc`, available as a primitive
     (`dc_session_ensure_authorized`) for the upload path too.
   - P10-05 ✅ Upload path migration — `domain_send_file` detects
     NETWORK/FILE_MIGRATE_X on `upload.saveFilePart` /
     `saveBigFilePart`, opens + authorizes the target DC, regenerates
     the file_id, and retries the full upload there.
     `messages.sendMedia` stays on the home DC (references the
     foreign-uploaded file_id). Cross-DC routing is now complete
     across all media I/O.
2. **Remaining MessageMedia skippers** — full inline
   `storyItem#79b26a24` body (StoryFwdHeader + MediaArea +
   PrivacyRule + StoryViews + Reaction skippers). Everything else
   on the MessageMedia surface is now handled.
3. **Curses TUI (US-11 v2)** — pane-based live redraw.

## Current focus
MVP feature set is complete; any further work is polish and
follow-ups. If nothing new is requested, the recommended next step
is **extending tl_skip coverage** (phase 3c flags + rare MessageMedia
variants) because that directly unlocks more real chats from the
history / search / watch surface.
