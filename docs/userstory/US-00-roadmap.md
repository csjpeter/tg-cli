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
- **2012 unit tests** passing (ASAN)
- **131 functional tests** passing (real OpenSSL; SHA-512, PBKDF2,
  BN primitives, IGE, MTProto crypto round-trips)
- Valgrind: 0 leaks, 0 errors
- Zero warnings under `-Wall -Wextra -Werror -pedantic`
- Core+infra coverage: ~89% (TUI excluded)

## Known v1 limitations (follow-ups, not blockers)
- Rare MessageMedia variants (Poll, Story, Game, Invoice, Giveaway,
  PaidMedia) still halt iteration — needs per-variant skippers.
  WebPage (URL previews) now parses through for the common
  text-only case; cached_page and attributes flags still bail.
- File upload capped at `UPLOAD_MAX_SIZE = 10 MiB` — the
  `upload.saveBigFilePart` path (>10 MiB, media DCs) and
  cross-DC download of photos on other DCs are follow-ups
  under P4-04.
- Only documents are uploaded; photo upload (scaled InputMedia) is
  future work.
- Only photos are downloaded — Document download is a follow-up.

## Backlog (post-MVP polish)
1. **Big files / media DCs** — `upload.saveBigFilePart` and
   transparent transport migration to DC-2/DC-4 for media.
2. **Remaining MessageMedia / trailing flag skippers** — Poll,
   Reactions, Replies, ReplyMarkup, RestrictionReason, FactCheck
   (unlocks more real-world channels during iteration).
3. **Document download** — analogous to the photo path but using
   `inputDocumentFileLocation` + the document's thumb_size.
4. **Curses TUI (US-11 v2)** — pane-based live redraw.

## Current focus
MVP feature set is complete; any further work is polish and
follow-ups. If nothing new is requested, the recommended next step
is **extending tl_skip coverage** (phase 3c flags + rare MessageMedia
variants) because that directly unlocks more real chats from the
history / search / watch surface.
