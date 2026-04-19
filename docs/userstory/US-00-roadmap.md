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
- **2703 unit tests** passing (ASAN)
- **427 functional tests** passing — every US-03..US-16 use case is
  exercised end-to-end through the in-process mock Telegram server
  with real OpenSSL on both sides (US-17 FT-01..FT-07 landed).
- Valgrind: 0 leaks, 0 errors
- Zero warnings under `-Wall -Wextra -Werror -pedantic`
- Combined line coverage ~88 % (functional-only ~52 %); see
  `docs/dev/coverage.md`.

## Known v1 limitations (follow-ups, not blockers)
- MessageMedia iteration is complete for every declared modern
  variant. LIM-01..LIM-04 closed the remaining gaps: photo upload
  via scaled InputMedia (`inputMediaUploadedPhoto`); document
  download for sticker / customEmoji with populated thumbs and
  video_thumbs vectors; the full PageBlock tree inside
  `webPage.cached_page` (Cover, Collage, Slideshow, Details,
  RelatedArticles, Table, Embed, EmbedPost, Photo, Video, Audio,
  Map, Channel, List, OrderedList, Blockquote, Pullquote) on top
  of the simple blocks + 15-variant RichText; and a recursion
  guard on the storyItem → messageMedia → storyItem cycle so an
  adversarial server can't infinite-recurse the skipper. Unknown
  WebPageAttribute variants still bail — they're rare and without
  a schema we have no way to know their wire size. All standard
  MessageMedia variants iterate: Photo, Document (incl. stickers,
  custom emojis, any thumb layout), Geo, Contact, Venue, Dice,
  WebPage (incl. cached_page + attributes), Poll, Invoice (with
  WebDocument photo or MessageExtendedMedia), Story (deleted /
  skipped / full with StoryFwdHeader + MediaArea + PrivacyRule +
  StoryViews + Reaction + inner MessageMedia up to the recursion
  cap), Giveaway, Game (photo-only or with Document), PaidMedia.
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
2. **Remaining MessageMedia skippers** ✅ complete — only the
   IV-article body (`webPage.cached_page`, `webPage.attributes`)
   is still outside the iteration surface.
3. **Curses TUI (US-11 v2)** — pane-based live redraw.
   - TUI-01 ✅ `tui/screen.{h,c}` double-buffered screen primitive:
     UTF-8 put/fill/clear into a back grid, diff-based flip that emits
     only changed cells, SGR attr tracking with trailing reset, wide-char
     handling (wcwidth==2 occupies lead + zero-width trailer), cursor
     control (CUP + DECTCEM). Tests run against `open_memstream` so they
     assert on the exact ANSI bytes emitted.
   - TUI-02 ✅ `tui/pane.{h,c}` viewport geometry + three-pane layout
     (dialogs / history / status). `layout_compute` clamps left-width
     to [20, 40] and shrinks it when the terminal is too narrow to keep
     history ≥ 20 cols. `pane_put_str`, `pane_fill`, `pane_clear`
     translate pane-relative coordinates to absolute screen cells and
     clip strictly at the pane edge (no spill past the right border,
     including partial wide-glyph cases). Layered cleanly on top of
     TUI-01 via the new `screen_put_str_n` bounded variant.
   - TUI-03 ✅ `tui/list_view.{h,c}` scroll+selection helper: flat-list
     navigation (up/down, PgUp/PgDn, Home/End) with automatic viewport
     reveal so the selected row is always visible. Pure state, no
     drawing — shared between the dialog pane and the history pane.
     Set-count clamps selection + scroll on shrink; set-count to zero
     resets to selected=-1.
   - TUI-04 ✅ `tui/dialog_pane.{h,c}` dialog list view-model: owns a
     `DialogEntry[DIALOG_PANE_MAX]` snapshot + `ListView`, renders onto
     a pane with one line per dialog (kind prefix + optional unread
     badge + title), highlights the current selection via reverse-video
     when focused, bolds rows with unread messages, shows a placeholder
     when empty, honors scroll_top. `dialog_pane_refresh` wraps
     `domain_get_dialogs`; `dialog_pane_selected` returns the
     highlighted entry (or NULL).
   - TUI-05 ✅ `tui/history_pane.{h,c}` message history view-model:
     `HistoryEntry[HISTORY_PANE_MAX]` snapshot bound to a `HistoryPeer`
     (so we can tell "no peer yet" apart from "peer loaded but empty").
     Renders one row per message with a direction arrow (`>` outgoing,
     `<` incoming), id badge and either the extracted text, `(media)`,
     or `(complex)` marker; complex rows render dimmed. Shows
     `(select a dialog)` before load and `(no messages)` when the peer
     is loaded but empty. `history_pane_load` wraps
     `domain_get_history` with the peer descriptor.
   - TUI-06 ✅ `tui/status_row.{h,c}` + `tui/app.{h,c}` event state
     machine. The status row is a full-width reverse-video line with
     a mode-sensitive hint ("[dialogs] j/k … Enter …" vs
     "[history] j/k …") on the left and an optional right-aligned
     message (loading / last error). `TuiApp` owns the screen, the
     layout and the three panes, and exposes a pure state-machine API:
     `tui_app_handle_key` / `tui_app_handle_char` map TermKey +
     printable chars (q, Q, j, k, h, l, g, G) to high-level
     `TuiEvent` values (REDRAW, OPEN_DIALOG, QUIT). Navigation always
     targets the focused pane; Enter on a dialog emits OPEN_DIALOG and
     pre-emptively moves focus to history so the status hint updates
     before the network call. `tui_app_paint` stages the current UI
     state into the back buffer without flipping — the event loop
     (still to land in tg_tui.c) drives the actual terminal IO.
   - TUI-07 ✅ tg-tui integration: new `--tui` CLI flag enters the
     curses-style loop built on TuiApp. Reads one keypress per
     iteration via `terminal_read_key` / `terminal_last_printable`,
     hands it to the state machine, and on OPEN_DIALOG fetches the
     selected dialog's history with a "loading…" status message.
     Without `--tui` the existing REPL is preserved, so no user
     workflow regresses.
   - TUI-08 ✅ `access_hash` threaded through
     `messages.getDialogs` parse: `ChatSummary` /
     `UserSummary` + `DialogEntry` now carry
     `(access_hash, have_access_hash)`. The TUI can open user and
     channel dialogs directly from the dialog pane instead of the
     previous "cannot open (access_hash missing)" status.
   - TUI-09 ✅ SIGWINCH handler drives `tui_app_resize`.
     New platform entry points `terminal_enable_resize_notifications`
     + `terminal_consume_resize`; POSIX sigaction without SA_RESTART
     so the blocking read(2) returns EINTR on SIGWINCH and the TUI
     loop can re-check geometry between keystrokes. Screen is
     reinitialised, cursor hidden again, list-view selections /
     scroll preserved across the resize.
   - TUI-10 ✅ live updates poll via `updates.getDifference`.
     Added `terminal_wait_key(timeout_ms)` (poll() on STDIN). TUI
     loop idles for 5 s between keystrokes; on each timeout it
     calls `updates.getDifference` and, on non-empty diff, refreshes
     the dialog pane (and the open history pane if any). Initial
     `getState` primes the high-water marks; failures silently skip
     polling so the UI keeps working offline.
   - TUI-11 ✅ webPage cached_page + attributes iteration.
     `skip_webpage` now handles `attributes:flags.12?Vector<WebPageAttribute>`
     (Theme without settings, StickerSet, Story with optional inline
     StoryItem) and `cached_page:flags.10?Page` for simple Instant
     View bodies (Title / Header / Subheader / Kicker / Paragraph /
     Subtitle / Footer / Preformatted / Divider / Anchor /
     AuthorDate blocks with the full 15-variant RichText tree
     including textConcat). Complex IV blocks still bail; unknown
     WebPageAttribute variants still bail. This closes the last
     modern-webPage gap in MessageMedia iteration.

## Current focus
All documented v1 limitations are closed (LIM-01..LIM-04):
photo upload, full Document iteration (incl. stickers / custom
emojis / thumbs), full cached_page PageBlock tree, and a recursion
guard on storyItem → messageMedia.

### Functional-test expansion (US-17, FT-01..07) — done
Every FT ticket is landed; the 427-case functional suite now drives
every US-03..US-16 flow end-to-end through the in-process mock
Telegram server with real OpenSSL on both sides.

- **FT-01** ✅ — use-case inventory + US doc refresh.
- **FT-02** ✅ — scriptable mock Telegram server
  (`tests/mocks/mock_tel_server.{h,c}`): encrypted envelope parsing,
  RPC dispatch registry, canned response builders, one-shot
  `bad_server_salt` injection.
- **FT-03** ✅ — login flow tests (SMS, 2FA, PHONE_MIGRATE,
  `bad_server_salt`, session persist, `--logout`).
- **FT-04** ✅ — read-path tests (dialogs, history, search,
  user-info, contacts, resolve-username, watch).
- **FT-05** ✅ — write-path tests (send, reply, edit, delete,
  forward, read markers).
- **FT-06** ✅ — upload/download tests (small/big file, photo,
  cross-DC FILE_MIGRATE + NETWORK_MIGRATE).
- **FT-07** ✅ — two-pass lcov (unit + functional), GitHub Pages
  hosting with nested `/functional/` report, dedicated README
  coverage badge (combined + functional-only).

### Next focus
All MVP user stories are closed; the backlog is now polish
(see "Backlog (post-MVP polish)" above).
