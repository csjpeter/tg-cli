# US-00 — Product Roadmap & Current Focus

## Vision
Three binaries sharing one MTProto 2.0 codebase:
`tg-cli-ro` (batch, read-only) · `tg-tui` (interactive) · `tg-cli` (batch r/w).
See `docs/SPECIFICATION.md` and `docs/adr/0005-three-binary-architecture.md`.

## Working end-to-end
- Phases 1–4 MTProto stack · ADR-0005 three-binary split
- **Login**: phone+code, DC migration (PHONE/USER/NETWORK_MIGRATE_X),
  session persistence, `--logout`, `bad_server_salt` retry,
  service-frame draining (bad_msg / new_session / ack / pong)
- **Read features**:
  - US-05 me · US-09 user-info · P7-01 contacts
  - US-04 dialogs with **titles + @usernames** (P5-08 join)
  - US-06 history (self + @peer) with message text, media kind,
    photo_id/document_id metadata (P6-03)
  - US-10 search (global + peer)
  - US-07 watch poll loop with new message text
  - US-11 tg-tui MVP REPL
- **Protocol parse coverage (P5-07 phase 1/2/3a/3b + P5-09 v1)**:
  - tl_skip for Bool, string, Peer, NotificationSound,
    PeerNotifySettings, DraftMessage(empty), MessageEntity (20
    variants), Vector<MessageEntity>, MessageFwdHeader,
    MessageReplyHeader, PhotoSize (6 variants), Vector<PhotoSize>,
    Photo, Document, MessageMedia (Empty, Unsupported, Geo,
    Contact, Venue, GeoLive, Dice, Photo, Document), ChatPhoto,
    UserProfilePhoto, UserStatus, Vector<RestrictionReason>,
    Vector<Username>, PeerColor, EmojiStatus, Chat (5 variants),
    User (user/userEmpty), Message.
  - Extractors: `tl_extract_chat` (id + title),
    `tl_extract_user` (id + name + username),
    `tl_skip_message_media_ex` (kind + photo_id / doc_id / dc_id).
- **Security + robustness (14 QA fixes)**: MITM hash verification
  (QA-12), OOM guards, alignment, endianness, msg_id randomness,
  abridged 3-byte prefix, logger idempotent, config bzero,
  crypto_rand_bytes bounds, pq_factorize UINT32_MAX, etc.

## Quality
- 1892 unit tests passing
- Valgrind: 0 leaks, 0 errors
- Zero warnings under `-Wall -Wextra -Werror -pedantic`
- Core+infra coverage (TUI excluded): ~89%

## Known v1 limitations
- Messages with `reply_markup`, `reactions`, `replies`,
  `restriction_reason`, `factcheck` still halt iteration for that
  response. Tracked as future phase 3c.
- Rare MessageMedia variants (Poll, Story, Game, Invoice, Giveaway,
  WebPage, PaidMedia) cause iteration halt — partial support would
  need per-variant skippers.
- 2FA accounts can't log in — needs P3-03 (SRP + PBKDF2-HMAC-SHA512).
- No media download yet — P6-01 (extract file_reference +
  access_hash from Photo/Document, chunked upload.getFile).
- No write capabilities (send/edit/delete/read-marker) — P5-03/04/06,
  P6-02, and the `tg-cli` batch binary are future work.

## Backlog by priority
1. **P6-01 file download** — chunked `upload.getFile` + FILE_MIGRATE_X.
   Needs full Photo/Document parsing to extract access_hash and
   file_reference.
2. **P3-03 2FA password (SRP)** — adds `crypto_sha512` +
   `crypto_pbkdf2_hmac_sha512` wrappers, implements SRP math.
3. **Remaining MessageMedia / trailing flag skippers** — Poll,
   Reactions, Replies, ReplyMarkup, RestrictionReason, FactCheck.
4. **`src/domain/write/` + `tg-cli` binary** — P5-03 send-message,
   P5-04 read-history, P5-06 edit/delete/forward/reply, P6-02
   upload, P8-03 stdin pipe (US-12).

## Current focus
v1 read-only MVP is feature-complete end-to-end for the common
case: login → dialogs with titles → history with text + media type
→ search → user-info → contacts → watch. Next best step:
**P6-01 file download** so media can be opened from the filesystem.
