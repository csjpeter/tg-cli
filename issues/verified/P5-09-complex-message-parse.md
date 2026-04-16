# Parse messages with complex flags (fwd_from / reply_to / media / entities)

## Description
Current `parse_message_prefix()` in `src/domain/read/history.c` bails with
`entry.complex = 1` whenever the Message's flags indicate any of:
fwd_from, reply_to, media, reply_markup, via_bot_id, entities. For public
channel messages this covers most entries, leaving users with "(complex
— text not parsed)" instead of the actual post.

## Goal
Extract the `message` text (and ideally `date`) even when those flags
are set, by skipping each nested object properly using the skipper
primitives introduced in **P5-07**.

## Steps
1. Replace the blanket `MSG_FLAGS_COMPLEX_MASK` check with per-field
   conditional skipping:
   - flags.2 fwd_from → `tl_skip_message_fwd_header()`
   - flags.3 reply_to → `tl_skip_message_reply_header()`
   - flags.9 media → `tl_skip_message_media()`
   - flags.10 reply_markup → `tl_skip_reply_markup()`
   - flags.11 via_bot_id → `tl_read_int64()` (trivial)
   - flags.13 entities → `tl_skip_message_entities()`
2. After all optionals, read `message:string`.
3. Keep `complex = 1` only for `messageService` and unknown constructors.
4. Unit tests: craft a Message with each flag combination and verify
   text + date extraction.

## Estimate
~200 lines incl. tests.

## Dependencies
P5-07 (skipper primitives must exist first).

## Verified — 2026-04-16 (v1 — common flags)
- fwd_from (flags.2) → tl_skip_message_fwd_header (phase 2)
- reply_to (flags.3) → tl_skip_message_reply_header (phase 2;
  reply_media nested still bails)
- via_bot_id (flags.11) → trivial int64 skip
- entities (flags.7) → tl_skip_message_entities_vector
- media (flags.9) → tl_skip_message_media (phase 3a): Empty,
  Unsupported, Geo, Contact, Venue, GeoLive, Dice, Photo +
  photoEmpty, Document + documentEmpty.

Messages with these flags now yield id + date + text and the
iteration continues to the next Message in the vector.

## Remaining
- reply_markup (flags.6) — ReplyMarkup skipper
- reactions (flags.20) — MessageReactions skipper
- replies (flags.23) — MessageReplies skipper
- restriction_reason (flags.22) — Vector<RestrictionReason>
- factcheck (flags2.3) — FactCheck
- MessageMedia variants: Poll, Story, Game, Invoice, Giveaway,
  PaidMedia, WebPage.

These remain in the MSG_FLAGS_STOP_ITER mask or cause the media
skipper to return -1; tracked as future phase 3c work.
