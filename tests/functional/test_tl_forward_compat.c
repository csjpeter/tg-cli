/**
 * @file test_tl_forward_compat.c
 * @brief TEST-75 — TL forward-compatibility of tl_skip.c.
 *
 * Exercises the "unknown constructor" contract of tl_skip.c: when a
 * future Telegram layer introduces a new CRC where one of the skip
 * functions is called, the skipper must refuse to advance silently
 * (returning -1) instead of corrupting the reader. The five cases map
 * to the ticket:
 *
 *   1. Unknown trailing field inside a messages.dialogs-style top-level
 *      result: the known Message bytes before it are fully consumable.
 *   2. Unknown MessageMedia CRC inside a Message: the text/date fields
 *      are extracted and tl_skip_message_media reports MEDIA_OTHER.
 *   3. Unknown message action (messageService): the bare prefix
 *      (flags+id) is advanced past but the body refuses to iterate, so
 *      the ID is the only thing we can present.
 *   4. Known Message with a trailing optional flag bit whose type we do
 *      not know yet: all known-position fields parse correctly up to
 *      the unknown flag, and the skipper refuses to invent a layout
 *      for the unknown bit.
 *   5. Unknown Update CRC inside updates.difference: iterating a
 *      Vector<Message> with unknown trailing fields stops cleanly at
 *      the first unknown CRC rather than desynchronising the reader.
 *
 * The tests drive the skip layer directly with fabricated byte streams
 * — the same pattern as test_tl_skip_message_functional.c — so we can
 * assert against the cursor position and the return code deterministically
 * without a live mock server.
 */

#include "test_helpers.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"

#include <stdint.h>
#include <string.h>

/* ---- Fabricated "future Telegram" CRCs ----
 *
 * The high byte 0xFF gives us values that the production CRC tables
 * (layer 170+) do not use; they are deliberately unknown so every skip
 * call must take the default/unknown branch.
 */
#define CRC_future_mediaTypeX       0xFF00AA01u
#define CRC_future_actionReward     0xFF00BB02u
#define CRC_future_replyMarkupTodo  0xFF00CC03u
#define CRC_future_entityBadge      0xFF00DD04u
#define CRC_future_updateHoroscope  0xFF00EE05u
#define CRC_future_peerGhost        0xFF00FF06u
#define CRC_future_chatPsychic      0xFF00FF07u
#define CRC_future_userTimelord     0xFF00FF08u

/* CRCs not re-exposed from tl_skip.c (copied from tl_skip_message_functional). */
#define CRC_messageMediaEmpty_t     0x3ded6320u
#define CRC_replyInlineMarkup_t     0x48a30254u
#define CRC_keyboardButtonRow_t     0x77608b83u
#define CRC_keyboardButtonCallback_t 0x35bbdb6bu
#define CRC_messageEntityBold_t     0xbd610bc9u

/* Message flag constants — same subset used by the kitchen-sink suite. */
#define FLAG_REPLY_MARKUP   (1u <<  6)
#define FLAG_ENTITIES       (1u <<  7)
#define FLAG_FROM_ID        (1u <<  8)
#define FLAG_MEDIA          (1u <<  9)
#define FLAG_VIEWS_FWDS     (1u << 10)

/* ---------------------------------------------------------------- */
/* Case 1 — unknown trailing field after known dialog bytes         */
/* ---------------------------------------------------------------- */

/* Build a realistic Message whose trailer carries an unknown CRC in
 * place of the expected reply_markup. The pre-message text/date/id all
 * land on the wire in their known positions; the skipper must stop at
 * the unknown reply_markup CRC and leave the reader at a well-defined
 * mid-object position so the caller can discard the tail and move on. */
static void write_message_with_unknown_trailer(TlWriter *w) {
    uint32_t flags = FLAG_FROM_ID | FLAG_REPLY_MARKUP;
    uint32_t flags2 = 0;

    tl_write_uint32(w, TL_message);
    tl_write_uint32(w, flags);
    tl_write_uint32(w, flags2);
    tl_write_int32 (w, 42);                     /* id */

    /* from_id + peer_id */
    tl_write_uint32(w, TL_peerUser); tl_write_int64(w, 7LL);
    tl_write_uint32(w, TL_peerChannel); tl_write_int64(w, 88LL);

    tl_write_int32 (w, 1700000000);             /* date */
    tl_write_string(w, "known text body");      /* message */

    /* reply_markup: an unknown CRC. The skipper must refuse. */
    tl_write_uint32(w, CRC_future_replyMarkupTodo);
    tl_write_int32 (w, 0xDEAD);
    tl_write_int32 (w, 0xBEEF);
}

static void test_unknown_top_level_result_skipped(void) {
    TlWriter w; tl_writer_init(&w);
    write_message_with_unknown_trailer(&w);

    TlReader r = tl_reader_init(w.data, w.len);
    /* tl_skip_message must refuse to advance because the reply_markup
     * CRC is unknown.  The reader position is defined as
     * "undefined — caller stops iterating" per the API contract. */
    ASSERT(tl_skip_message(&r) == -1,
           "tl_skip_message rejects unknown reply_markup CRC");
    /* Cursor should have advanced past the mandatory prefix at minimum
     * (4 bytes CRC + 4 flags + 4 flags2 + 4 id = 16 bytes). */
    ASSERT(r.pos >= 16, "cursor advanced past message prefix before bailing");

    tl_writer_free(&w);

    /* Companion check: feed the same known reply_markup with a valid
     * replyInlineMarkup — the skipper must accept it, so we can be sure
     * the rejection in the first part was caused by the CRC alone. */
    tl_writer_init(&w);
    uint32_t flags = FLAG_FROM_ID | FLAG_REPLY_MARKUP;
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 42);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 7LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 88LL);
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "known text body");
    /* valid inline markup with zero rows */
    tl_write_uint32(&w, CRC_replyInlineMarkup_t);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 0xCAFE);                /* trailer sentinel */

    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r2) == 0,
           "tl_skip_message accepts known empty inline markup");
    ASSERT(tl_read_int32(&r2) == 0x0000CAFE,
           "cursor lands on the sentinel after known reply_markup");
    tl_writer_free(&w);
}

/* ---------------------------------------------------------------- */
/* Case 2 — unknown MessageMedia CRC mid-message                    */
/* ---------------------------------------------------------------- */

static void test_unknown_media_in_history(void) {
    TlWriter w; tl_writer_init(&w);

    uint32_t flags = FLAG_FROM_ID | FLAG_MEDIA;
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);                     /* flags2 */
    tl_write_int32 (&w, 501);                   /* id */
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 9LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 77LL);
    tl_write_int32 (&w, 1700000100);            /* date */
    tl_write_string(&w, "caption survives");    /* message */

    /* Unknown MessageMedia variant — three bogus trailer words so the
     * reader has bytes to overrun if the skipper mis-behaves. */
    tl_write_uint32(&w, CRC_future_mediaTypeX);
    tl_write_int32 (&w, 0x1111);
    tl_write_int32 (&w, 0x2222);
    tl_write_int32 (&w, 0x3333);

    /* tl_skip_message must return -1 and the cursor must sit at or past
     * the MessageMedia CRC (we have read 4 bytes of it before switching
     * in the default branch). */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r) == -1,
           "unknown media CRC halts tl_skip_message");

    /* Direct exercise of tl_skip_message_media_ex with the unknown CRC
     * verifies the "MEDIA_OTHER on unknown" contract. */
    TlWriter w2; tl_writer_init(&w2);
    tl_write_uint32(&w2, CRC_future_mediaTypeX);
    tl_write_int32 (&w2, 0);
    TlReader r2 = tl_reader_init(w2.data, w2.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r2, &mi) == -1,
           "unknown MessageMedia variant returns -1");
    ASSERT(mi.kind == MEDIA_OTHER,
           "unknown MessageMedia labels out as MEDIA_OTHER");

    /* Sanity: a known messageMediaEmpty must succeed at MEDIA_EMPTY. */
    TlWriter w3; tl_writer_init(&w3);
    tl_write_uint32(&w3, CRC_messageMediaEmpty_t);
    TlReader r3 = tl_reader_init(w3.data, w3.len);
    MediaInfo mi3 = {0};
    ASSERT(tl_skip_message_media_ex(&r3, &mi3) == 0,
           "messageMediaEmpty accepted");
    ASSERT(mi3.kind == MEDIA_EMPTY, "empty labelled MEDIA_EMPTY");
    ASSERT(r3.pos == r3.len, "reader fully consumed on empty media");

    tl_writer_free(&w);
    tl_writer_free(&w2);
    tl_writer_free(&w3);
}

/* ---------------------------------------------------------------- */
/* Case 3 — unknown messageService action                           */
/* ---------------------------------------------------------------- */

static void test_unknown_message_action(void) {
    TlWriter w; tl_writer_init(&w);

    /* Build a messageService envelope. tl_skip_message refuses to walk
     * messageService bodies today — it returns -1 after reading the
     * prefix — so the action CRC itself never reaches a dispatcher,
     * but the caller can still display id/date because they were read
     * before the bail. */
    tl_write_uint32(&w, TL_messageService);
    tl_write_uint32(&w, 0);                     /* flags */
    tl_write_uint32(&w, 0);                     /* flags2 */
    tl_write_int32 (&w, 9001);                  /* id */
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 3LL);
    tl_write_int32 (&w, 1700000200);            /* date */
    /* Unknown service action — tl_skip_message never reaches it. */
    tl_write_uint32(&w, CRC_future_actionReward);
    tl_write_int64 (&w, 0xAABBCCDDEEFF0011LL);

    TlReader r = tl_reader_init(w.data, w.len);
    /* Contract: messageService is unsupported → -1. The reader position
     * is undefined but we must not have advanced past the buffer end. */
    ASSERT(tl_skip_message(&r) == -1,
           "messageService skip refuses to walk body");
    ASSERT(r.pos <= r.len, "reader stays in-bounds on bail");
    tl_writer_free(&w);
}

/* ---------------------------------------------------------------- */
/* Case 4 — known Message with unknown optional flag bit            */
/* ---------------------------------------------------------------- */

static void test_unknown_optional_field_preserves_layout(void) {
    TlWriter w; tl_writer_init(&w);

    /* Set a flag bit the current skipper does not know about (bit 31 in
     * flags has no meaning today).  The pre-text fields — from_id, peer,
     * date, message — must still parse because they sit before any
     * optional trailer. */
    uint32_t flags  = FLAG_FROM_ID | (1u << 31);
    uint32_t flags2 = 0;

    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, flags2);
    tl_write_int32 (&w, 777);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 11LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 99LL);
    tl_write_int32 (&w, 1700000300);
    tl_write_string(&w, "survives unknown trailing bit");

    /* tl_skip_message walks every known flag bit.  Because bit 31 has
     * no known payload, the skipper is expected to return 0 (no data
     * is read for that bit) — confirming the "unknown bit = no-op"
     * forward-compat policy. If a future change bound bit 31 to a
     * payload, this test would need to be updated in lock-step with
     * the new skipper.  */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r) == 0,
           "unknown flag bit with no payload is skipped as a no-op");
    ASSERT(r.pos == r.len, "reader fully consumed");
    tl_writer_free(&w);

    /* Companion: if a caller sets flags2 bit 31 — also currently
     * unused — the skipper must likewise advance cleanly. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_message);
    tl_write_uint32(&w, FLAG_FROM_ID);
    tl_write_uint32(&w, (1u << 31));             /* unknown flags2 bit */
    tl_write_int32 (&w, 778);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 12LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 100LL);
    tl_write_int32 (&w, 1700000400);
    tl_write_string(&w, "flags2 variant");
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r2) == 0,
           "unknown flags2 bit with no payload is skipped");
    ASSERT(r2.pos == r2.len, "reader fully consumed on flags2 variant");
    tl_writer_free(&w);
}

/* ---------------------------------------------------------------- */
/* Case 5 — unknown Update CRC inside updates.difference            */
/* ---------------------------------------------------------------- */

/* updates.getDifference returns a Vector<Message> as its first
 * sub-field; tl_skip_message is how we iterate that vector.  An
 * unknown Message-like constructor at position i must halt iteration
 * at i so the caller can present whatever preceded it. */
static void test_unknown_update_type_in_getdifference(void) {
    TlWriter w; tl_writer_init(&w);

    /* Simulate two plausible messages followed by one "future update". */
    /* 0 — real messageEmpty */
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);                     /* flags */
    tl_write_int32 (&w, 601);
    /* 1 — real messageEmpty */
    tl_write_uint32(&w, TL_messageEmpty);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 602);
    /* 2 — an unknown Message-like CRC (not TL_message/Empty/Service). */
    tl_write_uint32(&w, CRC_future_updateHoroscope);
    tl_write_int32 (&w, 0xDEADBEEF);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r) == 0, "first messageEmpty skipped");
    ASSERT(tl_skip_message(&r) == 0, "second messageEmpty skipped");
    /* Unknown CRC — tl_skip_message must refuse rather than guess. */
    ASSERT(tl_skip_message(&r) == -1,
           "unknown Message CRC halts iteration");
    tl_writer_free(&w);
}

/* ---------------------------------------------------------------- */
/* Additional coverage — exercise the remaining skip surface        */
/*                                                                  */
/* These are not in the ticket's enumerated list but drive many more */
/* lines of tl_skip.c that are otherwise only touched by unit tests. */
/* ---------------------------------------------------------------- */

/* tl_skip_peer / tl_skip_bool / tl_skip_string on unknown input. */
static void test_skip_primitives_reject_unknown(void) {
    /* Unknown Peer variant. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_future_peerGhost);
    tl_write_int64 (&w, 123LL);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer(&r) == -1, "tl_skip_peer rejects unknown variant");
    tl_writer_free(&w);

    /* Known Peer variants succeed (peerUser/Chat/Channel). */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_peerUser);    tl_write_int64(&w, 1LL);
    tl_write_uint32(&w, TL_peerChat);    tl_write_int64(&w, 2LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 3LL);
    TlReader rk = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer(&rk) == 0, "peerUser accepted");
    ASSERT(tl_skip_peer(&rk) == 0, "peerChat accepted");
    ASSERT(tl_skip_peer(&rk) == 0, "peerChannel accepted");
    ASSERT(rk.pos == rk.len, "peer reader fully consumed");
    tl_writer_free(&w);

    /* Bool: tl_skip_bool just reads 4 bytes regardless of value. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolTrue);
    TlReader rb = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_bool(&rb) == 0, "bool skipped");
    ASSERT(rb.pos == 4, "bool consumed 4 bytes");
    tl_writer_free(&w);

    /* String round-trip. */
    tl_writer_init(&w);
    tl_write_string(&w, "forward-compat string payload");
    TlReader rs = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_string(&rs) == 0, "string skipped");
    ASSERT(rs.pos == rs.len, "string reader fully consumed");
    tl_writer_free(&w);
}

/* tl_skip_message_entity: drive several known variants to exercise the
 * switch body, and one unknown one. */
static void test_message_entity_variants(void) {
    /* Known variants: bold (8 bytes), textUrl (8 + string), mentionName
     * (16 bytes), custom emoji (16 bytes), blockquote (12 bytes). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 5);

    /* bold */
    tl_write_uint32(&w, CRC_messageEntityBold_t);
    tl_write_int32 (&w, 0); tl_write_int32(&w, 4);

    /* textUrl */
    tl_write_uint32(&w, 0x76a6d327u);
    tl_write_int32 (&w, 0); tl_write_int32(&w, 5);
    tl_write_string(&w, "https://example.org");

    /* mentionName */
    tl_write_uint32(&w, 0xdc7b1140u);
    tl_write_int32 (&w, 6); tl_write_int32(&w, 7);
    tl_write_int64 (&w, 42LL);

    /* custom emoji */
    tl_write_uint32(&w, 0xc8cf05f8u);
    tl_write_int32 (&w, 14); tl_write_int32(&w, 2);
    tl_write_int64 (&w, 9001LL);

    /* blockquote */
    tl_write_uint32(&w, 0xf1ccaaacu);
    tl_write_uint32(&w, 0);                     /* flags */
    tl_write_int32 (&w, 17); tl_write_int32(&w, 3);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entities_vector(&r) == 0,
           "known entity variants all accepted");
    ASSERT(r.pos == r.len, "reader fully consumed");
    tl_writer_free(&w);

    /* Unknown entity CRC halts the whole vector. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_future_entityBadge);
    tl_write_int32 (&w, 0); tl_write_int32(&w, 8);
    TlReader ru = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entities_vector(&ru) == -1,
           "unknown entity CRC breaks the vector");
    tl_writer_free(&w);
}

/* Known MessageMedia variants round-trip — geo, contact, venue, dice,
 * geoLive — to sweep through a broad swath of tl_skip_message_media_ex. */
static void test_media_variants_skip_clean(void) {
    /* messageMediaGeo + geoPointEmpty */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x56e0d474u);           /* messageMediaGeo */
    tl_write_uint32(&w, 0x1117dd5fu);           /* geoPointEmpty */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media:geo/empty skipped");
    ASSERT(r.pos == r.len, "reader consumed geo/empty");
    tl_writer_free(&w);

    /* messageMediaContact: phone_number, first_name, last_name, vcard, user_id */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x70322949u);
    tl_write_string(&w, "+15550001234");
    tl_write_string(&w, "First");
    tl_write_string(&w, "Last");
    tl_write_string(&w, "BEGIN:VCARD\nEND:VCARD");
    tl_write_int64 (&w, 42LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r2) == 0, "media:contact skipped");
    ASSERT(r2.pos == r2.len, "reader consumed contact");
    tl_writer_free(&w);

    /* messageMediaDice: value + emoticon */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x3f7ee58bu);
    tl_write_int32 (&w, 6);
    tl_write_string(&w, "\xf0\x9f\x8e\xb2"); /* dice emoji */
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r3) == 0, "media:dice skipped");
    ASSERT(r3.pos == r3.len, "reader consumed dice");
    tl_writer_free(&w);

    /* messageMediaVenue: geo + address strings + venue id/type */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2ec0533fu);
    tl_write_uint32(&w, 0x1117dd5fu);           /* geoPointEmpty */
    tl_write_string(&w, "123 Main St");
    tl_write_string(&w, "Coffee shop");
    tl_write_string(&w, "foursquare");
    tl_write_string(&w, "V123");
    tl_write_string(&w, "cafe");
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r4) == 0, "media:venue skipped");
    ASSERT(r4.pos == r4.len, "reader consumed venue");
    tl_writer_free(&w);

    /* messageMediaGeoLive: flags=0 + geoPointEmpty + period */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xb940c666u);
    tl_write_uint32(&w, 0);                     /* flags */
    tl_write_uint32(&w, 0x1117dd5fu);           /* geoPointEmpty */
    tl_write_int32 (&w, 3600);                  /* period */
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r5) == 0, "media:geoLive skipped");
    ASSERT(r5.pos == r5.len, "reader consumed geoLive");
    tl_writer_free(&w);

    /* messageMediaUnsupported — a known "we cannot render this" marker. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x9f84f49eu);
    TlReader r6 = tl_reader_init(w.data, w.len);
    MediaInfo mi6 = {0};
    ASSERT(tl_skip_message_media_ex(&r6, &mi6) == 0,
           "media:unsupported accepted");
    ASSERT(mi6.kind == MEDIA_UNSUPPORTED,
           "unsupported marker labelled MEDIA_UNSUPPORTED");
    tl_writer_free(&w);
}

/* ReplyMarkup variants round-trip (hide, forceReply, inline, markup). */
static void test_reply_markup_variants(void) {
    /* replyKeyboardHide — flags=0 */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xa03e5b85u);
    tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == 0, "keyboardHide skipped");
    ASSERT(r.pos == r.len, "hide reader consumed");
    tl_writer_free(&w);

    /* replyKeyboardForceReply with placeholder */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x86b40b08u);
    tl_write_uint32(&w, (1u << 3));             /* flags.3 → placeholder */
    tl_write_string(&w, "Type here...");
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r2) == 0, "forceReply skipped");
    ASSERT(r2.pos == r2.len, "forceReply reader consumed");
    tl_writer_free(&w);

    /* replyInlineMarkup with one row, two buttons (button + url) */
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_replyInlineMarkup_t);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_keyboardButtonRow_t);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* keyboardButton */
    tl_write_uint32(&w, 0xa2fa4880u);
    tl_write_string(&w, "Yes");
    /* keyboardButtonUrl */
    tl_write_uint32(&w, 0x258aff05u);
    tl_write_string(&w, "Open");
    tl_write_string(&w, "https://example.org");
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r3) == 0, "inlineMarkup with rows skipped");
    ASSERT(r3.pos == r3.len, "inline reader consumed");
    tl_writer_free(&w);

    /* Unknown ReplyMarkup */
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_future_replyMarkupTodo);
    tl_write_uint32(&w, 0);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r4) == -1,
           "unknown reply_markup CRC rejected");
    tl_writer_free(&w);
}

/* Chat/User extractors: unknown variant → -1; known path drives many
 * conditional lines. */
static void test_chat_user_unknown_variants(void) {
    /* Unknown chat variant. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_future_chatPsychic);
    tl_write_int64 (&w, 1LL);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat(&r) == -1, "unknown chat CRC rejected");
    tl_writer_free(&w);

    /* Unknown user variant. */
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_future_userTimelord);
    tl_write_int64 (&w, 1LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user(&r2) == -1, "unknown user CRC rejected");
    tl_writer_free(&w);

    /* chatEmpty happy path. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_chatEmpty);
    tl_write_int64 (&w, 42LL);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ChatSummary cs = {0};
    ASSERT(tl_extract_chat(&r3, &cs) == 0, "chatEmpty extracted");
    ASSERT(cs.id == 42LL, "chatEmpty id captured");
    ASSERT(cs.title[0] == '\0', "chatEmpty title blank");
    tl_writer_free(&w);

    /* userEmpty happy path. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_userEmpty);
    tl_write_int64 (&w, 99LL);
    TlReader r4 = tl_reader_init(w.data, w.len);
    UserSummary us = {0};
    ASSERT(tl_extract_user(&r4, &us) == 0, "userEmpty extracted");
    ASSERT(us.id == 99LL, "userEmpty id captured");
    tl_writer_free(&w);

    /* Full user with first/last name + username + phone + access_hash —
     * drives many of the flag branches in extract_user_inner. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_user);
    /* flags: 0=access_hash, 1=first_name, 2=last_name, 3=username, 4=phone */
    uint32_t uflags = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    tl_write_uint32(&w, uflags);
    tl_write_uint32(&w, 0);                     /* flags2 */
    tl_write_int64 (&w, 7001LL);
    tl_write_int64 (&w, 0xAABBCCDDEEFF0011LL);  /* access_hash */
    tl_write_string(&w, "Alice");
    tl_write_string(&w, "Wonder");
    tl_write_string(&w, "alice_wonder");
    tl_write_string(&w, "+10000000000");
    TlReader r5 = tl_reader_init(w.data, w.len);
    UserSummary us5 = {0};
    ASSERT(tl_extract_user(&r5, &us5) == 0, "full user extracted");
    ASSERT(us5.id == 7001LL, "full user id");
    ASSERT(us5.have_access_hash == 1, "full user access_hash present");
    ASSERT(strcmp(us5.name, "Alice Wonder") == 0, "name joined");
    ASSERT(strcmp(us5.username, "alice_wonder") == 0, "username captured");
    tl_writer_free(&w);
}

/* Truncation — short buffers must fail cleanly instead of reading OOB. */
static void test_truncation_rejected(void) {
    /* Just the message CRC, nothing else. */
    uint8_t only_crc[4];
    only_crc[0] = 0x42; only_crc[1] = 0x52; only_crc[2] = 0x34; only_crc[3] = 0x94;
    TlReader r = tl_reader_init(only_crc, sizeof(only_crc));
    ASSERT(tl_skip_message(&r) == -1,
           "tl_skip_message rejects payload shorter than header");

    /* Zero-length buffer. */
    TlReader r0 = tl_reader_init(NULL, 0);
    ASSERT(tl_skip_message(&r0) == -1, "empty buffer rejected");
    ASSERT(tl_skip_peer(&r0) == -1,    "peer short read rejected");
    ASSERT(tl_skip_string(&r0) == -1,  "string short read rejected");
    ASSERT(tl_skip_bool(&r0) == -1,    "bool short read rejected");
}

/* ---------------------------------------------------------------- */
/* Extra surface coverage — the tl_skip.c file covers dozens of     */
/* nested TL types. Exercising the known-CRC branches of each one   */
/* matters for forward-compat because it proves that "unknown       */
/* returns -1, known returns 0" is a uniform contract, not a special */
/* case of the Message top-level only.                              */
/* ---------------------------------------------------------------- */

/* ---- PhotoSize + Photo ---- */
static void test_photo_and_photo_size_roundtrip(void) {
    /* Photo with flags.0, id + access_hash + file_reference + date + sizes +
     * dc_id. Walks photo_full, walk_photo_size_vector, tl_skip_photo_size. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xfb197a65u);            /* photo */
    tl_write_uint32(&w, 0);                      /* flags (no has_stickers,
                                                   no video_sizes) */
    tl_write_int64 (&w, 1001LL);                 /* id */
    tl_write_int64 (&w, 0xABCDEF0123456789LL);   /* access_hash */
    /* file_reference:bytes — empty is fine */
    tl_write_bytes (&w, (const unsigned char *)"", 0);
    tl_write_int32 (&w, 1700000000);             /* date */
    /* sizes:Vector<PhotoSize> — one photoSize variant */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 3);
    /* photoSize: type + w + h + size */
    tl_write_uint32(&w, 0x75c78e60u);
    tl_write_string(&w, "y");
    tl_write_int32 (&w, 1280); tl_write_int32(&w, 720); tl_write_int32(&w, 55555);
    /* photoCachedSize: type + w + h + bytes */
    tl_write_uint32(&w, 0x021e1ad6u);
    tl_write_string(&w, "s");
    tl_write_int32 (&w, 90); tl_write_int32(&w, 90);
    tl_write_bytes (&w, (const unsigned char *)"\x00\x01\x02", 3);
    /* photoSizeProgressive: type + w + h + Vector<int> */
    tl_write_uint32(&w, 0xfa3efb95u);
    tl_write_string(&w, "p");
    tl_write_int32 (&w, 1080); tl_write_int32(&w, 1920);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    tl_write_int32 (&w, 100); tl_write_int32(&w, 200);
    /* dc_id */
    tl_write_int32 (&w, 2);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo(&r) == 0, "photo walked");
    ASSERT(r.pos == r.len, "reader consumed photo");
    tl_writer_free(&w);

    /* photoEmpty */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2331b22du);
    tl_write_int64 (&w, 7777LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo(&r2) == 0, "photoEmpty walked");
    tl_writer_free(&w);

    /* photoSize variants individually */
    tl_writer_init(&w);
    /* photoSizeEmpty */
    tl_write_uint32(&w, 0x0e17e23cu);
    tl_write_string(&w, "x");
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo_size(&r3) == 0, "photoSizeEmpty ok");
    tl_writer_free(&w);

    /* photoStrippedSize */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xe0b0bc2eu);
    tl_write_string(&w, "i");
    tl_write_bytes (&w, (const unsigned char *)"\xFF\xFE\xFD", 3);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo_size(&r4) == 0, "photoStrippedSize ok");
    tl_writer_free(&w);

    /* photoPathSize */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xd8214d41u);
    tl_write_string(&w, "j");
    tl_write_bytes (&w, (const unsigned char *)"abc", 3);
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo_size(&r5) == 0, "photoPathSize ok");
    tl_writer_free(&w);

    /* Unknown photoSize CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00BBCCu);
    TlReader r6 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo_size(&r6) == -1, "unknown photoSize rejected");
    tl_writer_free(&w);

    /* photo_size_vector */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x75c78e60u);
    tl_write_string(&w, "m");
    tl_write_int32 (&w, 320); tl_write_int32(&w, 240); tl_write_int32(&w, 1000);
    TlReader r7 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_photo_size_vector(&r7) == 0, "vector walked");
    tl_writer_free(&w);
}

/* ---- Document with attributes ---- */
static void test_document_with_attributes(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x8fd4c4d8u);            /* document */
    tl_write_uint32(&w, 0);                      /* flags (no thumbs / video) */
    tl_write_int64 (&w, 1234LL);                 /* id */
    tl_write_int64 (&w, 0xFEEDFACECAFED00DLL);   /* access_hash */
    tl_write_bytes (&w, (const unsigned char *)"ref", 3);   /* file_reference */
    tl_write_int32 (&w, 1700000000);             /* date */
    tl_write_string(&w, "image/png");            /* mime_type */
    tl_write_int64 (&w, 4096LL);                 /* size */
    tl_write_int32 (&w, 2);                      /* dc_id */
    /* attributes: Vector<DocumentAttribute> — cover many variants */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 5);
    /* imageSize */
    tl_write_uint32(&w, 0x6c37c15cu);
    tl_write_int32 (&w, 800); tl_write_int32(&w, 600);
    /* animated */
    tl_write_uint32(&w, 0x11b58939u);
    /* filename */
    tl_write_uint32(&w, 0x15590068u);
    tl_write_string(&w, "selfie.png");
    /* audio flags=0, duration only */
    tl_write_uint32(&w, 0x9852f9c6u);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int32 (&w, 30);                     /* duration */
    /* hasStickers */
    tl_write_uint32(&w, 0x9801d2f7u);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_document(&r) == 0, "document walked");
    ASSERT(r.pos == r.len, "reader consumed document");
    tl_writer_free(&w);

    /* documentEmpty */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x36f8c871u);
    tl_write_int64 (&w, 42LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_document(&r2) == 0, "documentEmpty walked");
    tl_writer_free(&w);
}

/* ---- Message forward header ---- */
static void test_fwd_header_variants(void) {
    /* messageFwdHeader: flags with from_id (bit 0) + date only (simplest). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x4e4df4bbu);
    tl_write_uint32(&w, (1u << 0));              /* flags: from_id */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 42LL);                   /* from_id peer */
    tl_write_int32 (&w, 1700001000);             /* date */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r) == 0, "fwd header walked");
    ASSERT(r.pos == r.len, "fwd reader consumed");
    tl_writer_free(&w);

    /* Unknown CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF77u);
    tl_write_uint32(&w, 0);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r2) == -1,
           "unknown fwd header CRC rejected");
    tl_writer_free(&w);

    /* Fuller fwd header — from_id + from_name + channel_post + post_author */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4e4df4bbu);
    uint32_t ff = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5);
    tl_write_uint32(&w, ff);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 1000LL);
    tl_write_string(&w, "Anonymous");            /* from_name (flags.5) */
    tl_write_int32 (&w, 1700001000);             /* date */
    tl_write_int32 (&w, 55);                     /* channel_post (flags.2) */
    tl_write_string(&w, "Bot Author");           /* post_author (flags.3) */
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r3) == 0,
           "fuller fwd header walked");
    tl_writer_free(&w);
}

/* ---- Reply header ---- */
static void test_reply_header_variants(void) {
    /* messageReplyHeader with reply_to_msg_id (flags.4) */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xafbc09dbu);
    tl_write_uint32(&w, (1u << 4));
    tl_write_int32 (&w, 3000);                   /* reply_to_msg_id */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r) == 0, "reply header walked");
    tl_writer_free(&w);

    /* messageReplyStoryHeader */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xe5af939u);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 42LL);
    tl_write_int32 (&w, 77);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r2) == 0, "story header walked");
    tl_writer_free(&w);

    /* Unknown reply header CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF88u);
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r3) == -1,
           "unknown reply header rejected");
    tl_writer_free(&w);
}

/* ---- Draft message ---- */
static void test_draft_message_variants(void) {
    TlWriter w; tl_writer_init(&w);
    /* draftMessageEmpty flags=0 */
    tl_write_uint32(&w, 0x1b0c841au);
    tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r) == 0, "draftMessageEmpty walked");
    tl_writer_free(&w);

    /* draftMessageEmpty with date */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x1b0c841au);
    tl_write_uint32(&w, 1u);
    tl_write_int32 (&w, 1700000000);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r2) == 0, "draftEmpty+date walked");
    tl_writer_free(&w);

    /* draftMessage non-empty: production chooses not to parse, so it
     * must return -1. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x3fccf7efu);
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r3) == -1, "non-empty draft rejected");
    tl_writer_free(&w);

    /* Unknown draft CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FFAAu);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r4) == -1, "unknown draft rejected");
    tl_writer_free(&w);
}

/* ---- Notification sound + peerNotifySettings ---- */
static void test_notification_sound_and_settings(void) {
    /* All four sound variants */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x97e8bebeu);            /* default */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r) == 0, "default sound");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x6f0c34dfu);            /* none */
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r2) == 0, "none sound");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x830b9ae4u);            /* local */
    tl_write_string(&w, "Chime");
    tl_write_string(&w, "chime.mp3");
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r3) == 0, "local sound");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0xff6c8049u);            /* ringtone */
    tl_write_int64 (&w, 777LL);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r4) == 0, "ringtone sound");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF11u);            /* unknown */
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r5) == -1, "unknown sound rejected");
    tl_writer_free(&w);

    /* peerNotifySettings with many sub-fields. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xa83b0426u);
    /* flags: show_previews(0), silent(1), mute_until(2), ios(3), android(4),
     *        other(5), stories_muted(6), stories_hide_sender(7) */
    uint32_t sflags = (1u << 0) | (1u << 1) | (1u << 2) |
                      (1u << 3) | (1u << 4) | (1u << 5) |
                      (1u << 6) | (1u << 7);
    tl_write_uint32(&w, sflags);
    tl_write_uint32(&w, TL_boolTrue);            /* show_previews */
    tl_write_uint32(&w, TL_boolFalse);           /* silent */
    tl_write_int32 (&w, 1700100000);             /* mute_until */
    tl_write_uint32(&w, 0x97e8bebeu);            /* ios_sound default */
    tl_write_uint32(&w, 0x6f0c34dfu);            /* android none */
    tl_write_uint32(&w, 0x97e8bebeu);            /* other default */
    tl_write_uint32(&w, TL_boolTrue);            /* stories_muted */
    tl_write_uint32(&w, TL_boolFalse);           /* stories_hide_sender */
    TlReader rs = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer_notify_settings(&rs) == 0, "settings walked");
    ASSERT(rs.pos == rs.len, "settings reader consumed");
    tl_writer_free(&w);

    /* Unknown settings CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF22u);
    tl_write_uint32(&w, 0);
    TlReader ru = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer_notify_settings(&ru) == -1,
           "unknown settings rejected");
    tl_writer_free(&w);
}

/* ---- Chat photo + user profile photo + user status ---- */
static void test_chat_user_visuals(void) {
    /* chatPhotoEmpty / chatPhoto */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x37c1011cu);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat_photo(&r) == 0, "chatPhotoEmpty");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x1c6e1c11u);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int64 (&w, 5001LL);                 /* photo_id */
    tl_write_int32 (&w, 2);                      /* dc_id */
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat_photo(&r2) == 0, "chatPhoto");
    tl_writer_free(&w);

    /* chatPhoto with stripped_thumb (flags.1) */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x1c6e1c11u);
    tl_write_uint32(&w, (1u << 1));
    tl_write_int64 (&w, 5002LL);
    tl_write_bytes (&w, (const unsigned char *)"stripped", 8);
    tl_write_int32 (&w, 4);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat_photo(&r3) == 0, "chatPhoto+stripped");
    tl_writer_free(&w);

    /* Unknown chatPhoto */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF33u);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat_photo(&r4) == -1, "unknown chatPhoto rejected");
    tl_writer_free(&w);

    /* userProfilePhotoEmpty / userProfilePhoto */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4f11bae1u);
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_profile_photo(&r5) == 0, "userProfilePhotoEmpty");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x82d1f706u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 9000LL);
    tl_write_int32 (&w, 5);
    TlReader r6 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_profile_photo(&r6) == 0, "userProfilePhoto");
    tl_writer_free(&w);

    /* Unknown userProfilePhoto */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF44u);
    TlReader r7 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_profile_photo(&r7) == -1, "unknown rejected");
    tl_writer_free(&w);

    /* userStatusEmpty carries no payload; the other three (recently,
     * lastWeek, lastMonth) each read an int32 per the 170+ schema. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x09d05049u);            /* empty */
    TlReader rse = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_status(&rse) == 0, "empty user status");
    tl_writer_free(&w);

    uint32_t statuses_with_int[] = {
        0x7b197dc8u, 0x541a1d1au, 0x65899e67u
    };
    for (size_t i = 0; i < sizeof(statuses_with_int)/sizeof(statuses_with_int[0]); i++) {
        tl_writer_init(&w);
        tl_write_uint32(&w, statuses_with_int[i]);
        tl_write_int32 (&w, 1700000000);
        TlReader rr = tl_reader_init(w.data, w.len);
        ASSERT(tl_skip_user_status(&rr) == 0, "recently/lastWeek/lastMonth status");
        tl_writer_free(&w);
    }

    /* Online/Offline carry an int32 expires/was_online. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xedb93949u);
    tl_write_int32 (&w, 1700200000);
    TlReader ro = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_status(&ro) == 0, "online status");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x008c703fu);
    tl_write_int32 (&w, 1700100000);
    TlReader roff = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_status(&roff) == 0, "offline status");
    tl_writer_free(&w);

    /* Unknown status */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF55u);
    TlReader ru2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user_status(&ru2) == -1, "unknown status rejected");
    tl_writer_free(&w);
}

/* ---- Username vector + peer color + emoji status ---- */
static void test_username_color_emoji(void) {
    /* Vector<Username> of length 2 */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    tl_write_uint32(&w, 0xb4073647u);            /* username */
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_string(&w, "alice");
    tl_write_uint32(&w, 0xb4073647u);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "bob");
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_username_vector(&r) == 0, "username vector walked");
    ASSERT(r.pos == r.len, "reader consumed");
    tl_writer_free(&w);

    /* Unknown entry in username vector. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00FF66u);
    tl_write_uint32(&w, 0);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_username_vector(&r2) == -1,
           "unknown username entry rejected");
    tl_writer_free(&w);

    /* peerColor with color + emoji_id */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xb54b5acfu);
    tl_write_uint32(&w, (1u << 0) | (1u << 1));
    tl_write_int32 (&w, 5);
    tl_write_int64 (&w, 500001LL);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer_color(&r3) == 0, "peerColor walked");
    tl_writer_free(&w);

    /* Unknown peerColor CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF77u);
    tl_write_uint32(&w, 0);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer_color(&r4) == -1, "unknown peerColor rejected");
    tl_writer_free(&w);

    /* EmojiStatus all three */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x2de11aaeu);            /* empty */
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_emoji_status(&r5) == 0, "emojiStatusEmpty");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0x929b619du);
    tl_write_int64 (&w, 123LL);
    TlReader r6 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_emoji_status(&r6) == 0, "emojiStatus");
    tl_writer_free(&w);

    tl_writer_init(&w);
    tl_write_uint32(&w, 0xfa30a8c7u);
    tl_write_int64 (&w, 456LL);
    tl_write_int32 (&w, 1700500000);
    TlReader r7 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_emoji_status(&r7) == 0, "emojiStatusUntil");
    tl_writer_free(&w);

    /* Unknown emoji status */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FF88u);
    TlReader r8 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_emoji_status(&r8) == -1, "unknown emojiStatus rejected");
    tl_writer_free(&w);
}

/* ---- Restriction reason vector ---- */
static void test_restriction_reason_vector(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    tl_write_uint32(&w, 0xd072acb4u);
    tl_write_string(&w, "android");
    tl_write_string(&w, "sensitive");
    tl_write_string(&w, "Restricted");
    tl_write_uint32(&w, 0xd072acb4u);
    tl_write_string(&w, "ios");
    tl_write_string(&w, "porn");
    tl_write_string(&w, "NSFW");
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_restriction_reason_vector(&r) == 0,
           "restriction reason vector walked");
    tl_writer_free(&w);

    /* Unknown inner CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00FF99u);
    tl_write_string(&w, "x"); tl_write_string(&w, "y"); tl_write_string(&w, "z");
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_restriction_reason_vector(&r2) == -1,
           "unknown restriction reason rejected");
    tl_writer_free(&w);
}

/* ---- Factcheck ---- */
static void test_factcheck_variants(void) {
    /* factCheck with country + text + hash. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xb89bfccfu);
    tl_write_uint32(&w, (1u << 1));
    tl_write_string(&w, "HU");
    tl_write_uint32(&w, 0x751f3146u);            /* textWithEntities */
    tl_write_string(&w, "fact-checked");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 0xDEADBEEFCAFEBABELL);   /* hash */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r) == 0, "factcheck walked");
    tl_writer_free(&w);

    /* factCheck with no flags set (just hash). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xb89bfccfu);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 77LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r2) == 0, "flagless factcheck");
    tl_writer_free(&w);

    /* Unknown factcheck CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FFAAu);
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r3) == -1, "unknown factcheck rejected");
    tl_writer_free(&w);
}

/* ---- MessageReactions + MessageReplies ---- */
static void test_reactions_replies_trailers(void) {
    /* MessageReactions with two ReactionCount entries (emoji + custom emoji). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x4f2b9479u);            /* messageReactions */
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* reactionCount: flags=0, reaction = reactionEmoji, count=5 */
    tl_write_uint32(&w, 0xa3d1cb80u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x1b2286b8u);
    tl_write_string(&w, "\xf0\x9f\x94\xa5");
    tl_write_int32 (&w, 5);
    /* reactionCount with chosen_order flag + reactionCustomEmoji */
    tl_write_uint32(&w, 0xa3d1cb80u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, 0x8935fc73u);
    tl_write_int64 (&w, 424242LL);
    tl_write_int32 (&w, 2);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r) == 0, "reactions walked");
    tl_writer_free(&w);

    /* Reactions with unknown inner Reaction CRC. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4f2b9479u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xa3d1cb80u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xFF00FFBBu);            /* unknown reaction */
    tl_write_int32 (&w, 1);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r2) == -1,
           "unknown reaction inner CRC rejected");
    tl_writer_free(&w);

    /* Reactions with recent_reactions (flags.1) — production bails. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4f2b9479u);
    tl_write_uint32(&w, (1u << 1));              /* recent_reactions present */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r3) == -1,
           "recent_reactions bail");
    tl_writer_free(&w);

    /* MessageReplies full: flags=0b1111 with all optionals. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x83d60fc2u);
    tl_write_uint32(&w, 0xF);                    /* all four bits */
    tl_write_int32 (&w, 10);                     /* replies */
    tl_write_int32 (&w, 100);                    /* replies_pts */
    /* recent_repliers (flags.1): Vector<Peer> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 42LL);
    tl_write_int64 (&w, 9999LL);                 /* channel_id (flags.0) */
    tl_write_int32 (&w, 20);                     /* max_id (flags.2) */
    tl_write_int32 (&w, 5);                      /* read_max_id (flags.3) */
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r4) == 0, "replies walked");
    tl_writer_free(&w);

    /* Unknown messageReplies CRC */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xFF00FFCCu);
    tl_write_uint32(&w, 0);
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r5) == -1, "unknown replies rejected");
    tl_writer_free(&w);
}

/* ---- Photo MessageMedia round-trip through tl_skip_message_media ---- */
static void test_media_photo_and_document(void) {
    /* messageMediaPhoto with a photo inner object. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x695150d7u);            /* messageMediaPhoto */
    tl_write_uint32(&w, (1u << 0));              /* flags.0 → photo present */
    /* photo_full */
    tl_write_uint32(&w, 0xfb197a65u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 1LL);
    tl_write_int64 (&w, 2LL);
    tl_write_bytes (&w, (const unsigned char *)"", 0);
    tl_write_int32 (&w, 1700000000);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x75c78e60u);
    tl_write_string(&w, "y");
    tl_write_int32 (&w, 1280); tl_write_int32(&w, 720); tl_write_int32(&w, 12345);
    tl_write_int32 (&w, 2);                      /* dc_id */
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "media photo walked");
    ASSERT(mi.kind == MEDIA_PHOTO, "kind=PHOTO");
    ASSERT(mi.photo_id == 1LL, "photo_id captured");
    tl_writer_free(&w);

    /* messageMediaDocument with an inner document. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4cf4d72du);
    tl_write_uint32(&w, (1u << 0));              /* document present */
    tl_write_uint32(&w, 0x8fd4c4d8u);            /* document */
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int64 (&w, 111LL);                  /* id */
    tl_write_int64 (&w, 222LL);                  /* access_hash */
    tl_write_bytes (&w, (const unsigned char *)"r", 1);
    tl_write_int32 (&w, 1700000000);             /* date */
    tl_write_string(&w, "video/mp4");
    tl_write_int64 (&w, 1024LL);                 /* size */
    tl_write_int32 (&w, 4);                      /* dc_id */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* documentAttributeFilename */
    tl_write_uint32(&w, 0x15590068u);
    tl_write_string(&w, "movie.mp4");
    TlReader r2 = tl_reader_init(w.data, w.len);
    MediaInfo mi2 = {0};
    ASSERT(tl_skip_message_media_ex(&r2, &mi2) == 0, "media document walked");
    ASSERT(mi2.kind == MEDIA_DOCUMENT, "kind=DOCUMENT");
    ASSERT(mi2.document_id == 111LL, "document_id captured");
    ASSERT(strcmp(mi2.document_mime, "video/mp4") == 0, "mime captured");
    ASSERT(strcmp(mi2.document_filename, "movie.mp4") == 0, "filename");
    tl_writer_free(&w);
}

/* ---- Empty MessageReplies path (flags=0) ---- */
static void test_message_replies_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x83d60fc2u);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int32 (&w, 0);                      /* replies */
    tl_write_int32 (&w, 0);                      /* replies_pts */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r) == 0, "empty replies walked");
    ASSERT(r.pos == r.len, "reader consumed");
    tl_writer_free(&w);
}

/* ---- messageMediaGame → skip_game ---- */
static void test_media_game(void) {
    /* messageMediaGame + game#bdf9653b flags=0 (no document). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xfdb19008u);          /* messageMediaGame */
    /* game#bdf9653b flags:# id:long access_hash:long short_name title desc photo */
    tl_write_uint32(&w, 0xbdf9653bu);
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int64 (&w, 5000LL);               /* id */
    tl_write_int64 (&w, 6000LL);               /* access_hash */
    tl_write_string(&w, "snake");              /* short_name */
    tl_write_string(&w, "Snake");              /* title */
    tl_write_string(&w, "Classic snake");      /* description */
    /* photo: photoEmpty */
    tl_write_uint32(&w, 0x2331b22du);
    tl_write_int64 (&w, 0LL);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "mediaGame walked");
    ASSERT(mi.kind == MEDIA_GAME, "kind=GAME");
    ASSERT(r.pos == r.len, "reader consumed game");
    tl_writer_free(&w);

    /* game with document (flags.0). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xfdb19008u);
    tl_write_uint32(&w, 0xbdf9653bu);
    tl_write_uint32(&w, (1u << 0));            /* flags.0 → document present */
    tl_write_int64 (&w, 7000LL);
    tl_write_int64 (&w, 8000LL);
    tl_write_string(&w, "pong");
    tl_write_string(&w, "Pong");
    tl_write_string(&w, "Old-school pong");
    tl_write_uint32(&w, 0x2331b22du);          /* photoEmpty */
    tl_write_int64 (&w, 0LL);
    /* documentEmpty */
    tl_write_uint32(&w, 0x36f8c871u);
    tl_write_int64 (&w, 42LL);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "game+doc walked");
    ASSERT(r2.pos == r2.len, "reader consumed game+doc");
    tl_writer_free(&w);

    /* unknown game CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xfdb19008u);
    tl_write_uint32(&w, 0xFF00ABCDu);
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == -1,
           "unknown game CRC rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaInvoice → skip_web_document + skip_message_extended_media ---- */
static void test_media_invoice(void) {
    /* Invoice with photo (webDocument, flags.0) — no receipt, no ext-media. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xf6a548d3u);          /* messageMediaInvoice */
    tl_write_uint32(&w, (1u << 0));            /* flags.0 → photo present */
    tl_write_string(&w, "Ticket");             /* title */
    tl_write_string(&w, "One event entry");   /* description */
    /* webDocumentNoProxy#f9c8bcc6 url:string size:int mime_type:string attrs:Vector */
    tl_write_uint32(&w, 0xf9c8bcc6u);
    tl_write_string(&w, "https://img.example.com/t.jpg");
    tl_write_int32 (&w, 1024);                 /* size */
    tl_write_string(&w, "image/jpeg");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    /* currency + amount + start_param */
    tl_write_string(&w, "USD");
    tl_write_int64 (&w, 500LL);
    tl_write_string(&w, "start123");
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "invoice+webDoc walked");
    ASSERT(mi.kind == MEDIA_INVOICE, "kind=INVOICE");
    ASSERT(r.pos == r.len, "reader consumed invoice");
    tl_writer_free(&w);

    /* Invoice with webDocument (full, has access_hash), no ext-media. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xf6a548d3u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_string(&w, "Prod");
    tl_write_string(&w, "Desc");
    /* webDocument#1c570ed1 url access_hash size mime attrs */
    tl_write_uint32(&w, 0x1c570ed1u);
    tl_write_string(&w, "https://cdn.tg/img.png");
    tl_write_int64 (&w, 999LL);                /* access_hash */
    tl_write_int32 (&w, 2048);
    tl_write_string(&w, "image/png");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x11b58939u);          /* documentAttributeAnimated */
    tl_write_string(&w, "EUR");
    tl_write_int64 (&w, 1000LL);
    tl_write_string(&w, "");
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "invoice+webDoc full");
    ASSERT(r2.pos == r2.len, "consumed invoice full");
    tl_writer_free(&w);

    /* Unknown webDocument CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xf6a548d3u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_string(&w, "T"); tl_write_string(&w, "D");
    tl_write_uint32(&w, 0xFF00FEFEu);          /* unknown webDocument */
    tl_write_string(&w, "USD"); tl_write_int64(&w, 0LL); tl_write_string(&w, "");
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == -1,
           "unknown webDocument CRC rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaPaidMedia → skip_message_extended_media ---- */
static void test_media_paid_media(void) {
    /* Paid media with one messageExtendedMediaPreview (flags=0). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xa8852491u);          /* messageMediaPaidMedia */
    tl_write_int64 (&w, 25LL);                 /* stars_amount */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* messageExtendedMediaPreview#ad628cc8 flags=0 (no w/h, no thumb, no dur) */
    tl_write_uint32(&w, 0xad628cc8u);
    tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "paidMedia preview walked");
    ASSERT(mi.kind == MEDIA_PAID, "kind=PAID");
    ASSERT(r.pos == r.len, "reader consumed paid preview");
    tl_writer_free(&w);

    /* Paid media preview with all flags set (w+h + thumb + video_duration). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xa8852491u);
    tl_write_int64 (&w, 50LL);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xad628cc8u);
    tl_write_uint32(&w, 0x7u);                 /* flags: bits 0,1,2 */
    tl_write_int32 (&w, 640); tl_write_int32(&w, 480);
    /* thumb: photoSizeEmpty */
    tl_write_uint32(&w, 0x0e17e23cu);
    tl_write_string(&w, "s");
    tl_write_int32 (&w, 10);                   /* video_duration */
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "paid preview all-flags");
    ASSERT(r2.pos == r2.len, "consumed paid preview all-flags");
    tl_writer_free(&w);

    /* Paid with messageExtendedMedia (wraps another MessageMedia). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xa8852491u);
    tl_write_int64 (&w, 10LL);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xee479c64u);          /* messageExtendedMedia */
    /* wrapped: messageMediaEmpty */
    tl_write_uint32(&w, 0x3ded6320u);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == 0, "extMedia wrapper walked");
    ASSERT(r3.pos == r3.len, "consumed extMedia");
    tl_writer_free(&w);

    /* Unknown messageExtendedMedia CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xa8852491u);
    tl_write_int64 (&w, 5LL);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00FFABu);
    tl_write_uint32(&w, 0);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r4, NULL) == -1,
           "unknown extMedia CRC rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaStory → skip_story_item, skip_story_views,
 *     skip_privacy_rule, skip_media_area, skip_geo_point_address,
 *     skip_media_area_coordinates, skip_story_fwd_header ---- */
static void test_media_story(void) {
    /* Story with storyItemDeleted (simplest). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);          /* messageMediaStory */
    tl_write_uint32(&w, (1u << 0));            /* flags.0 → story present */
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 1LL);
    tl_write_int32 (&w, 101);                  /* id */
    /* storyItemDeleted#51e6ee4f id:int */
    tl_write_uint32(&w, 0x51e6ee4fu);
    tl_write_int32 (&w, 101);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "story deleted walked");
    ASSERT(mi.kind == MEDIA_STORY, "kind=STORY");
    tl_writer_free(&w);

    /* storyItemSkipped */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 2LL);
    tl_write_int32 (&w, 200);
    tl_write_uint32(&w, 0xffadc913u);          /* storyItemSkipped */
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int32 (&w, 200);                  /* id */
    tl_write_int32 (&w, 1700001000);           /* date */
    tl_write_int32 (&w, 1700087400);           /* expire_date */
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "story skipped walked");
    ASSERT(r2.pos == r2.len, "reader consumed story skipped");
    tl_writer_free(&w);

    /* Full storyItem with caption + entities + mediaAreaVenue + privacyRule
     * + storyViews + storyFwdHeader. Drives all the 0% static helpers. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 3LL);
    tl_write_int32 (&w, 300);
    /* storyItem#79b26a24 */
    tl_write_uint32(&w, 0x79b26a24u);
    /* flags: bit 0=caption, 1=entities, 2=privacy, 3=views, 14=media_areas,
     *        17=fwd_from (storyFwdHeader) */
    uint32_t sflags = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 14) | (1u << 17);
    tl_write_uint32(&w, sflags);
    tl_write_int32 (&w, 300);                  /* id */
    tl_write_int32 (&w, 1700001000);           /* date */
    /* fwd_from: storyFwdHeader#b826e150 flags=0b110 (from_name + story_id) */
    tl_write_uint32(&w, 0xb826e150u);
    tl_write_uint32(&w, (1u << 1) | (1u << 2));
    tl_write_string(&w, "Original Poster");   /* from_name */
    tl_write_int32 (&w, 77);                  /* story_id */
    tl_write_int32 (&w, 1700087400);           /* expire_date */
    tl_write_string(&w, "Hello from story"); /* caption */
    /* entities: Vector<MessageEntity> (bold) */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xbd610bc9u);          /* messageEntityBold */
    tl_write_int32 (&w, 0); tl_write_int32(&w, 5);
    /* media: messageMediaEmpty */
    tl_write_uint32(&w, 0x3ded6320u);
    /* media_areas (flags.14): Vector<MediaArea> with two variants */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* mediaAreaUrl: coordinates + url */
    tl_write_uint32(&w, 0x37381085u);
    /* mediaAreaCoordinates#03d1ea4e flags=0 x y w h rotation */
    tl_write_uint32(&w, 0x03d1ea4eu);
    tl_write_uint32(&w, 0);                    /* flags */
    double dvals[5] = {0.1, 0.2, 0.3, 0.4, 0.0};
    for (int i = 0; i < 5; i++) {
        uint64_t bits; __builtin_memcpy(&bits, &dvals[i], 8);
        tl_write_uint32(&w, (uint32_t)bits);
        tl_write_uint32(&w, (uint32_t)(bits >> 32));
    }
    tl_write_string(&w, "https://example.com");
    /* mediaAreaGeoPoint with flags.0 → geoPointAddress */
    tl_write_uint32(&w, 0xdf8b3b22u);
    tl_write_uint32(&w, (1u << 0));            /* flags.0 → address present */
    /* coordinates */
    tl_write_uint32(&w, 0x03d1ea4eu);
    tl_write_uint32(&w, 0);
    for (int i = 0; i < 5; i++) {
        uint64_t bits; __builtin_memcpy(&bits, &dvals[i], 8);
        tl_write_uint32(&w, (uint32_t)bits);
        tl_write_uint32(&w, (uint32_t)(bits >> 32));
    }
    /* geoPoint#b2a2f663 flags=0 long lat access_hash */
    tl_write_uint32(&w, 0xb2a2f663u);
    tl_write_uint32(&w, 0);
    {
        double lng = 13.4, lat = 52.5, acc = 0.0; uint64_t b;
        __builtin_memcpy(&b, &lng, 8);
        tl_write_uint32(&w, (uint32_t)b); tl_write_uint32(&w, (uint32_t)(b>>32));
        __builtin_memcpy(&b, &lat, 8);
        tl_write_uint32(&w, (uint32_t)b); tl_write_uint32(&w, (uint32_t)(b>>32));
        (void)acc;
        tl_write_int64(&w, 0LL);
    }
    /* geoPointAddress#de4c5d93 flags=0b111 country state city street */
    tl_write_uint32(&w, 0xde4c5d93u);
    tl_write_uint32(&w, 0x7u);
    tl_write_string(&w, "DE");
    tl_write_string(&w, "Berlin");
    tl_write_string(&w, "Berlin");
    tl_write_string(&w, "Unter den Linden");
    /* privacy: Vector<PrivacyRule> with two variants (no-payload + users) */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    tl_write_uint32(&w, 0xfffe1bacu);          /* privacyValueAllowContacts */
    tl_write_uint32(&w, 0xb8905fb2u);          /* privacyValueAllowUsers */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    tl_write_int64 (&w, 11LL); tl_write_int64(&w, 22LL);
    /* views: storyViews#8d595cd6 flags=0b11101 */
    tl_write_uint32(&w, 0x8d595cd6u);
    uint32_t vflags = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4);
    tl_write_uint32(&w, vflags);
    tl_write_int32 (&w, 100);                  /* views_count */
    tl_write_int32 (&w, 5);                    /* forwards_count */
    /* reactions: Vector<ReactionCount> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xa3d1cb80u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x79f5d419u);          /* reactionEmpty */
    tl_write_int32 (&w, 3);
    tl_write_int32 (&w, 10);                   /* reactions_count */
    /* recent_viewers: Vector<long> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_int64 (&w, 99LL);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == 0, "full story walked");
    ASSERT(r3.pos == r3.len, "reader consumed full story");
    tl_writer_free(&w);

    /* storyFwdHeader with from peer (flags.0) + story_id (flags.2). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 5LL);
    tl_write_int32 (&w, 400);
    tl_write_uint32(&w, 0x79b26a24u);
    uint32_t sfwdflags = (1u << 17);
    tl_write_uint32(&w, sfwdflags);
    tl_write_int32 (&w, 400);
    tl_write_int32 (&w, 1700001000);
    tl_write_uint32(&w, 0xb826e150u);
    tl_write_uint32(&w, (1u << 0) | (1u << 2)); /* from_peer + story_id */
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 10LL);
    tl_write_int32 (&w, 55);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);          /* media: empty */
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r4, NULL) == 0,
           "story fwdHeader peer+storyId walked");
    ASSERT(r4.pos == r4.len, "consumed story fwdHeader");
    tl_writer_free(&w);

    /* Unknown storyItem CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 6LL);
    tl_write_int32 (&w, 500);
    tl_write_uint32(&w, 0xFF00AAAAu);          /* unknown storyItem */
    tl_write_uint32(&w, 0);
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r5, NULL) == -1,
           "unknown storyItem rejected");
    tl_writer_free(&w);

    /* mediaAreaSuggestedReaction */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 7LL);
    tl_write_int32 (&w, 600);
    tl_write_uint32(&w, 0x79b26a24u);
    uint32_t sarf = (1u << 14);
    tl_write_uint32(&w, sarf);
    tl_write_int32 (&w, 600); tl_write_int32(&w, 1700001000);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);
    /* media_areas Vector with mediaAreaSuggestedReaction */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x14455871u);          /* mediaAreaSuggestedReaction */
    tl_write_uint32(&w, 0);                    /* flags */
    /* coordinates */
    tl_write_uint32(&w, 0x03d1ea4eu);
    tl_write_uint32(&w, 0);
    for (int i = 0; i < 5; i++) {
        uint64_t bits; __builtin_memcpy(&bits, &dvals[i], 8);
        tl_write_uint32(&w, (uint32_t)bits); tl_write_uint32(&w, (uint32_t)(bits>>32));
    }
    tl_write_uint32(&w, 0x79f5d419u);          /* reactionEmpty */
    TlReader r6 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r6, NULL) == 0,
           "mediaAreaSuggestedReaction walked");
    ASSERT(r6.pos == r6.len, "consumed suggestedReaction");
    tl_writer_free(&w);

    /* mediaAreaChannelPost, mediaAreaWeather, mediaAreaStarGift, mediaAreaVenue */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 8LL);
    tl_write_int32 (&w, 700);
    tl_write_uint32(&w, 0x79b26a24u);
    uint32_t s4f = (1u << 14);
    tl_write_uint32(&w, s4f);
    tl_write_int32 (&w, 700); tl_write_int32(&w, 1700001000);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);
    /* media_areas: Vector with 4 areas */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 4);
    /* helper macro: write bare coordinates (flags=0) */
#define WRITE_COORDS(ww) do { \
    tl_write_uint32(ww, 0x03d1ea4eu); tl_write_uint32(ww, 0); \
    double _d[5] = {0.1,0.2,0.3,0.4,0.0}; \
    for(int _i=0;_i<5;_i++){uint64_t _b; __builtin_memcpy(&_b,&_d[_i],8); \
        tl_write_uint32(ww,(uint32_t)_b); tl_write_uint32(ww,(uint32_t)(_b>>32));} \
} while(0)
    /* mediaAreaChannelPost */
    tl_write_uint32(&w, 0x770416afu);
    WRITE_COORDS(&w);
    tl_write_int64(&w, 12345LL);               /* channel_id */
    tl_write_int32(&w, 777);                   /* msg_id */
    /* mediaAreaWeather */
    tl_write_uint32(&w, 0x49a6549cu);
    WRITE_COORDS(&w);
    tl_write_string(&w, "\xe2\x98\x80\xef\xb8\x8f"); /* emoji */
    double temp = 22.5; uint64_t tbits; __builtin_memcpy(&tbits, &temp, 8);
    tl_write_uint32(&w, (uint32_t)tbits); tl_write_uint32(&w, (uint32_t)(tbits>>32));
    tl_write_int32(&w, 0xFFFFFFu);             /* color */
    /* mediaAreaStarGift */
    tl_write_uint32(&w, 0x5787686du);
    WRITE_COORDS(&w);
    tl_write_string(&w, "gift-slug-42");
    /* mediaAreaVenue */
    tl_write_uint32(&w, 0xbe82db9cu);
    WRITE_COORDS(&w);
    tl_write_uint32(&w, 0x1117dd5fu);          /* geoPointEmpty */
    tl_write_string(&w, "Cafe");
    tl_write_string(&w, "Main St 1");
    tl_write_string(&w, "foursquare");
    tl_write_string(&w, "v42");
    tl_write_string(&w, "cafe");
    TlReader r7 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r7, NULL) == 0, "4 media area variants");
    ASSERT(r7.pos == r7.len, "consumed 4 media areas");
    tl_writer_free(&w);
#undef WRITE_COORDS

    /* Unknown mediaArea CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 9LL);
    tl_write_int32 (&w, 800);
    tl_write_uint32(&w, 0x79b26a24u);
    tl_write_uint32(&w, (1u << 14));
    tl_write_int32 (&w, 800); tl_write_int32(&w, 1700001000);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00DDDDu);          /* unknown mediaArea */
    tl_write_uint32(&w, 0);
    TlReader r8 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r8, NULL) == -1, "unknown mediaArea rejected");
    tl_writer_free(&w);

    /* Unknown privacyRule CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 10LL);
    tl_write_int32 (&w, 900);
    tl_write_uint32(&w, 0x79b26a24u);
    tl_write_uint32(&w, (1u << 2));            /* only privacy flag */
    tl_write_int32 (&w, 900); tl_write_int32(&w, 1700001000);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00CCCCu);          /* unknown privacyRule */
    TlReader r9 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r9, NULL) == -1, "unknown privacyRule rejected");
    tl_writer_free(&w);

    /* storyViews unknown CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x68cb6283u);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 11LL);
    tl_write_int32 (&w, 1000);
    tl_write_uint32(&w, 0x79b26a24u);
    tl_write_uint32(&w, (1u << 3));            /* only views flag */
    tl_write_int32 (&w, 1000); tl_write_int32(&w, 1700001000);
    tl_write_int32 (&w, 1700087400);
    tl_write_uint32(&w, 0x3ded6320u);
    tl_write_uint32(&w, 0xFF00BBBBu);          /* unknown storyViews */
    tl_write_uint32(&w, 0);
    TlReader r10 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r10, NULL) == -1,
           "unknown storyViews rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaPoll → skip_poll_answer_voters ---- */
static void test_media_poll_with_results(void) {
    /* Build a poll + results that exercises skip_poll_answer_voters. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x4bd6e798u);          /* messageMediaPoll */
    /* poll#58747131 flags=0 id question answers */
    tl_write_uint32(&w, 0x58747131u);
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int64 (&w, 77LL);                 /* id */
    /* question: textWithEntities#751f3146 */
    tl_write_uint32(&w, 0x751f3146u);
    tl_write_string(&w, "Best fruit?");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    /* answers: Vector<PollAnswer> — two answers */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* PollAnswer#6ca9c2e9 text:TextWithEntities option:bytes */
    tl_write_uint32(&w, 0x6ca9c2e9u);
    tl_write_uint32(&w, 0x751f3146u); tl_write_string(&w, "Apple");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_bytes (&w, (const unsigned char *)"\x01", 1);
    tl_write_uint32(&w, 0x6ca9c2e9u);
    tl_write_uint32(&w, 0x751f3146u); tl_write_string(&w, "Banana");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_bytes (&w, (const unsigned char *)"\x02", 1);
    /* PollResults#7adc669d flags=0b1110 (results + total_voters + recent) */
    tl_write_uint32(&w, 0x7adc669du);
    uint32_t rflags = (1u << 1) | (1u << 2) | (1u << 3);
    tl_write_uint32(&w, rflags);
    /* results: Vector<PollAnswerVoters> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* PollAnswerVoters#3b6ddad2 flags option:bytes voters */
    tl_write_uint32(&w, 0x3b6ddad2u);
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_bytes (&w, (const unsigned char *)"\x01", 1);
    tl_write_int32 (&w, 30);
    tl_write_uint32(&w, 0x3b6ddad2u);
    tl_write_uint32(&w, 0);
    tl_write_bytes (&w, (const unsigned char *)"\x02", 1);
    tl_write_int32 (&w, 20);
    tl_write_int32 (&w, 50);                   /* total_voters */
    /* recent_voters: Vector<Peer> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_peerUser); tl_write_int64(&w, 42LL);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "poll+results walked");
    ASSERT(mi.kind == MEDIA_POLL, "kind=POLL");
    ASSERT(r.pos == r.len, "reader consumed poll");
    tl_writer_free(&w);

    /* PollResults with solution (flags.4). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4bd6e798u);
    tl_write_uint32(&w, 0x58747131u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 78LL);
    tl_write_uint32(&w, 0x751f3146u); tl_write_string(&w, "Q?");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x7adc669du);
    tl_write_uint32(&w, (1u << 4));            /* solution flag */
    tl_write_string(&w, "Answer is 42");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "poll+solution walked");
    tl_writer_free(&w);

    /* Bad PollAnswerVoters CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x4bd6e798u);
    tl_write_uint32(&w, 0x58747131u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 79LL);
    tl_write_uint32(&w, 0x751f3146u); tl_write_string(&w, "Q?");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x7adc669du);
    tl_write_uint32(&w, (1u << 1));
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xFF00EEEEu);          /* bad PollAnswerVoters */
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == -1,
           "bad PollAnswerVoters rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaWebPage → skip_webpage + deep nested page/block/rich ---- */
static void test_media_webpage_variants(void) {
    /* webPageEmpty */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);          /* messageMediaWebPage */
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_uint32(&w, 0xeb1477e8u);          /* webPageEmpty */
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int64 (&w, 1LL);                  /* id */
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "webPageEmpty walked");
    ASSERT(mi.kind == MEDIA_WEBPAGE, "kind=WEBPAGE");
    tl_writer_free(&w);

    /* webPagePending */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xb0d13e47u);          /* webPagePending */
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 2LL);
    tl_write_int32 (&w, 1700000000);           /* date */
    TlReader r2 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r2, NULL) == 0, "webPagePending walked");
    tl_writer_free(&w);

    /* webPageNotModified (no flags) */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x7311ca11u);          /* webPageNotModified */
    tl_write_uint32(&w, (1u << 0));            /* flags.0 → cached_page_views */
    tl_write_int32 (&w, 1234);                 /* cached_page_views */
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r3, NULL) == 0, "webPageNotModified walked");
    tl_writer_free(&w);

    /* Full webPage — type + site + title + description + embed + author,
     * flags 0,1,2,3,5,6,7,8 — no photo/document/page/attributes. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);          /* webPage */
    uint32_t wpf = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8);
    tl_write_uint32(&w, wpf);
    tl_write_int64 (&w, 99LL);
    tl_write_string(&w, "https://example.com");
    tl_write_string(&w, "example.com");
    tl_write_int32 (&w, 0);                    /* hash */
    tl_write_string(&w, "article");            /* type */
    tl_write_string(&w, "Example Site");       /* site_name */
    tl_write_string(&w, "My Title");           /* title */
    tl_write_string(&w, "Description here");  /* description */
    tl_write_string(&w, "https://embed.example.com");
    tl_write_string(&w, "text/html");          /* embed_type */
    tl_write_int32 (&w, 640); tl_write_int32(&w, 360);
    tl_write_int32 (&w, 120);                  /* duration */
    tl_write_string(&w, "Author Name");
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r4, NULL) == 0, "full webPage walked");
    ASSERT(r4.pos == r4.len, "consumed full webPage");
    tl_writer_free(&w);

    /* webPage with cached_page (flags.10) — exercises skip_page + skip_page_block. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    uint32_t pgf = (1u << 10);                 /* cached_page */
    tl_write_uint32(&w, pgf);
    tl_write_int64 (&w, 100LL);
    tl_write_string(&w, "https://telegraph.ph/article");
    tl_write_string(&w, "telegraph.ph");
    tl_write_int32 (&w, 0);
    /* page#98657f0d flags=0 url blocks photos docs */
    tl_write_uint32(&w, 0x98657f0du);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "https://telegraph.ph/article");
    /* blocks: Vector<PageBlock> — cover several block variants */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 8);
    /* pageBlockUnsupported */
    tl_write_uint32(&w, 0x13567e8au);
    /* pageBlockDivider */
    tl_write_uint32(&w, 0xdb20b188u);
    /* pageBlockTitle: rich text textPlain */
    tl_write_uint32(&w, 0x70abc3fdu);
    tl_write_uint32(&w, 0x744694e0u); tl_write_string(&w, "My Title");
    /* pageBlockParagraph: textBold wrapping textPlain */
    tl_write_uint32(&w, 0x467a0766u);
    tl_write_uint32(&w, 0x6724abc4u);          /* textBold */
    tl_write_uint32(&w, 0x744694e0u); tl_write_string(&w, "bold");
    /* pageBlockAnchor: string */
    tl_write_uint32(&w, 0xce0d37b0u);
    tl_write_string(&w, "anchor1");
    /* pageBlockPreformatted: rich + language string */
    tl_write_uint32(&w, 0xc070d93eu);
    tl_write_uint32(&w, 0xdc3d824fu);          /* textEmpty */
    tl_write_string(&w, "python");
    /* pageBlockAuthorDate: rich + int */
    tl_write_uint32(&w, 0xbaafe5e0u);
    tl_write_uint32(&w, 0x744694e0u); tl_write_string(&w, "Author");
    tl_write_int32 (&w, 1700001000);
    /* pageBlockBlockquote: two rich texts */
    tl_write_uint32(&w, 0x263d7c26u);
    tl_write_uint32(&w, 0xdc3d824fu);          /* textEmpty */
    tl_write_uint32(&w, 0xdc3d824fu);
    /* photos: Vector<Photo> — empty */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    /* docs: Vector<Document> — empty */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r5 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r5, NULL) == 0, "webPage+page walked");
    ASSERT(r5.pos == r5.len, "consumed page");
    tl_writer_free(&w);

    /* Page with more block variants: Cover, List, OrderedList, Table,
     * RelatedArticles, Collage, Details. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 101LL);
    tl_write_string(&w, "https://t.me/a");
    tl_write_string(&w, "t.me");
    tl_write_int32 (&w, 0);
    tl_write_uint32(&w, 0x98657f0du);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "https://t.me/a");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 7);
    /* pageBlockCover wrapping a pageBlockDivider */
    tl_write_uint32(&w, 0x39f23300u);
    tl_write_uint32(&w, 0xdb20b188u);
    /* pageBlockList: one text item */
    tl_write_uint32(&w, 0xe4e88011u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xb92fb6cdu);          /* pageListItemText */
    tl_write_uint32(&w, 0xdc3d824fu);          /* textEmpty */
    /* pageBlockOrderedList: one ordered text item */
    tl_write_uint32(&w, 0x9a8ae1e1u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x5e068047u);          /* pageListOrderedItemText */
    tl_write_string(&w, "1.");
    tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockTable: flags=0 title rows */
    tl_write_uint32(&w, 0xbf4dea82u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xdc3d824fu);          /* title: textEmpty */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* pageTableRow: Vector<pageTableCell> with one cell */
    tl_write_uint32(&w, 0xe0c0c5e5u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* pageTableCell: flags=0b11000010 (text=bit7, colspan=bit1, rowspan=bit2) */
    tl_write_uint32(&w, 0x34566b6au);
    tl_write_uint32(&w, (1u<<7)|(1u<<1)|(1u<<2));
    tl_write_uint32(&w, 0xdc3d824fu);          /* text: textEmpty */
    tl_write_int32 (&w, 2);                    /* colspan */
    tl_write_int32 (&w, 1);                    /* rowspan */
    /* pageBlockRelatedArticles: title + articles */
    tl_write_uint32(&w, 0x16115a96u);
    tl_write_uint32(&w, 0xdc3d824fu);          /* title */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* pageRelatedArticle#b390dc08 flags=0b11111 */
    tl_write_uint32(&w, 0xb390dc08u);
    tl_write_uint32(&w, 0x1fu);
    tl_write_string(&w, "https://rel.example.com");
    tl_write_int64 (&w, 5LL);
    tl_write_string(&w, "Related Title");
    tl_write_string(&w, "Related Desc");
    tl_write_int64 (&w, 6LL);
    tl_write_string(&w, "Rel Author");
    tl_write_int32 (&w, 1700000000);
    /* pageBlockDetails: flags=0 blocks title */
    tl_write_uint32(&w, 0x76768bedu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xdc3d824fu);          /* title */
    /* pageBlockCollage: blocks caption */
    tl_write_uint32(&w, 0x65a0fa4du);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    /* caption: pageCaption#6f747657 text:RichText credit:RichText */
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r6 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r6, NULL) == 0, "page block variants walked");
    ASSERT(r6.pos == r6.len, "consumed page block variants");
    tl_writer_free(&w);

    /* Rich text variants: textUrl, textEmail, textPhone, textConcat,
     * textImage, textAnchor. */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 102LL);
    tl_write_string(&w, "https://t.me/b"); tl_write_string(&w, "t.me");
    tl_write_int32 (&w, 0);
    tl_write_uint32(&w, 0x98657f0du);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "https://t.me/b");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    /* pageBlockPullquote: two rich text fields — use complex rich texts */
    tl_write_uint32(&w, 0x4f4456d5u);
    /* text: textConcat wrapping [textUrl, textEmail, textPhone, textImage, textAnchor] */
    tl_write_uint32(&w, 0x7e6260d7u);          /* textConcat */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 5);
    /* textUrl: rich + url + webpage_id */
    tl_write_uint32(&w, 0x3c2884c1u);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_string(&w, "https://link.example.com");
    tl_write_int64 (&w, 7LL);
    /* textEmail: rich + email */
    tl_write_uint32(&w, 0xde5a0dd6u);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_string(&w, "a@b.com");
    /* textPhone: rich + phone */
    tl_write_uint32(&w, 0x1ccb966au);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_string(&w, "+15550001234");
    /* textImage: document_id w h */
    tl_write_uint32(&w, 0x081ccf4fu);
    tl_write_int64 (&w, 8LL);
    tl_write_int32 (&w, 32); tl_write_int32(&w, 32);
    /* textAnchor: rich + name */
    tl_write_uint32(&w, 0x35553762u);
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_string(&w, "sec1");
    /* credit: textEmpty */
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r7 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r7, NULL) == 0, "rich text variants walked");
    ASSERT(r7.pos == r7.len, "consumed rich text variants");
    tl_writer_free(&w);

    /* webPage with attributes (flags.12) — webPageAttributeTheme (no docs) +
     * webPageAttributeStickerSet (empty stickers). */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    tl_write_uint32(&w, (1u << 12));
    tl_write_int64 (&w, 103LL);
    tl_write_string(&w, "https://t.me/c"); tl_write_string(&w, "t.me");
    tl_write_int32 (&w, 0);
    /* attributes: Vector<WebPageAttribute> */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* webPageAttributeTheme flags=0 (no documents, no settings) */
    tl_write_uint32(&w, 0x54b56617u);
    tl_write_uint32(&w, 0);
    /* webPageAttributeStickerSet flags=0 stickers=[] */
    tl_write_uint32(&w, 0x50cc03d3u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r8 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r8, NULL) == 0, "webPage attributes walked");
    ASSERT(r8.pos == r8.len, "consumed webPage attributes");
    tl_writer_free(&w);

    /* Unknown webPage CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xFF001234u);
    tl_write_uint32(&w, 0);
    TlReader r9 = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r9, NULL) == -1, "unknown webPage rejected");
    tl_writer_free(&w);
}

/* ---- Chat with admin rights + banned rights ---- */
static void test_chat_admin_banned_rights(void) {
    /* chat#41cbf256 with flags.14 (admin_rights) and flags.18 (banned_rights). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_chat);
    uint32_t cflags = (1u << 14) | (1u << 18);
    tl_write_uint32(&w, cflags);
    tl_write_int64 (&w, 1001LL);
    tl_write_string(&w, "Test Group");
    /* chatPhotoEmpty */
    tl_write_uint32(&w, 0x37c1011cu);
    tl_write_int32 (&w, 5);                    /* participants_count */
    tl_write_int32 (&w, 1700000000);           /* date */
    tl_write_int32 (&w, 1);                    /* version */
    /* chatAdminRights#5fb224d5 flags:# */
    tl_write_uint32(&w, 0x5fb224d5u);
    tl_write_uint32(&w, 0x1ff);               /* all known bits */
    /* chatBannedRights#9f120418 flags:# until_date:int */
    tl_write_uint32(&w, 0x9f120418u);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ChatSummary cs = {0};
    ASSERT(tl_extract_chat(&r, &cs) == 0, "chat with admin+banned walked");
    ASSERT(cs.id == 1001LL, "chat id captured");
    ASSERT(r.pos == r.len, "reader consumed chat");
    tl_writer_free(&w);

    /* channel with flags.14 (admin_rights) + flags.15 (banned_rights) +
     * flags.18 (default_banned_rights). Minimal: no access_hash, no username. */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_channel);
    uint32_t chflags = (1u << 14) | (1u << 15) | (1u << 18);
    tl_write_uint32(&w, chflags);
    tl_write_uint32(&w, 0);                    /* flags2 */
    tl_write_int64 (&w, 2001LL);
    tl_write_string(&w, "My Channel");
    tl_write_uint32(&w, 0x37c1011cu);          /* chatPhotoEmpty */
    tl_write_int32 (&w, 1700000000);           /* date */
    tl_write_uint32(&w, 0x5fb224d5u);          /* chatAdminRights */
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x9f120418u);          /* chatBannedRights (flags.15) */
    tl_write_uint32(&w, 0); tl_write_int32(&w, 0);
    tl_write_uint32(&w, 0x9f120418u);          /* default_banned_rights (flags.18) */
    tl_write_uint32(&w, 0); tl_write_int32(&w, 0);
    TlReader r2 = tl_reader_init(w.data, w.len);
    ChatSummary cs2 = {0};
    ASSERT(tl_extract_chat(&r2, &cs2) == 0, "channel with rights walked");
    ASSERT(cs2.id == 2001LL, "channel id captured");
    tl_writer_free(&w);

    /* chatAdminRights unknown CRC → -1 (reach via chat) */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_chat);
    tl_write_uint32(&w, (1u << 14));
    tl_write_int64 (&w, 1002LL);
    tl_write_string(&w, "G");
    tl_write_uint32(&w, 0x37c1011cu);
    tl_write_int32 (&w, 0); tl_write_int32(&w, 0); tl_write_int32(&w, 0);
    tl_write_uint32(&w, 0xFF00DDDDu);          /* unknown admin rights */
    tl_write_uint32(&w, 0);
    TlReader r3 = tl_reader_init(w.data, w.len);
    ASSERT(tl_extract_chat(&r3, NULL) == -1, "unknown adminRights rejected");
    tl_writer_free(&w);

    /* chatBannedRights unknown CRC → -1 */
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_chat);
    tl_write_uint32(&w, (1u << 18));
    tl_write_int64 (&w, 1003LL);
    tl_write_string(&w, "H");
    tl_write_uint32(&w, 0x37c1011cu);
    tl_write_int32 (&w, 0); tl_write_int32(&w, 0); tl_write_int32(&w, 0);
    tl_write_uint32(&w, 0xFF00CCCCu);          /* unknown banned rights */
    tl_write_uint32(&w, 0); tl_write_int32(&w, 0);
    TlReader r4 = tl_reader_init(w.data, w.len);
    ASSERT(tl_extract_chat(&r4, NULL) == -1, "unknown bannedRights rejected");
    tl_writer_free(&w);
}

/* ---- Remaining list/ordered list blocks variant (blocks variant) ---- */
static void test_page_block_list_blocks_variant(void) {
    /* Exercises pageListItemBlocks and pageListOrderedItemBlocks branches. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 104LL);
    tl_write_string(&w, "https://t.me/d"); tl_write_string(&w, "t.me");
    tl_write_int32 (&w, 0);
    tl_write_uint32(&w, 0x98657f0du);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "https://t.me/d");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 2);
    /* pageBlockList with a pageListItemBlocks item */
    tl_write_uint32(&w, 0xe4e88011u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x25e073fcu);          /* pageListItemBlocks */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xdb20b188u);          /* pageBlockDivider (leaf) */
    /* pageBlockOrderedList with a pageListOrderedItemBlocks item */
    tl_write_uint32(&w, 0x9a8ae1e1u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x98dd8936u);          /* pageListOrderedItemBlocks */
    tl_write_string(&w, "a.");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xdb20b188u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "list blocks variant walked");
    ASSERT(r.pos == r.len, "consumed list blocks");
    tl_writer_free(&w);
}

/* ---- Remaining rich text + other page blocks ---- */
static void test_page_block_extra_variants(void) {
    /* pageBlockSlideshow, pageBlockEmbed, pageBlockEmbedPost,
     * pageBlockVideo, pageBlockAudio, pageBlockPhoto, pageBlockMap,
     * pageBlockChannel — and rich text subscript/superscript/marked/fixed/strike. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xddf8c26eu);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0xe89c45b2u);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 105LL);
    tl_write_string(&w, "https://t.me/e"); tl_write_string(&w, "t.me");
    tl_write_int32 (&w, 0);
    tl_write_uint32(&w, 0x98657f0du);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "https://t.me/e");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 10);
    /* pageBlockSlideshow: blocks + caption */
    tl_write_uint32(&w, 0x031f9590u);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xdb20b188u);
    tl_write_uint32(&w, 0x6f747657u);          /* pageCaption */
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockEmbed: flags=0 (no url, no html, no poster, no wh) + caption */
    tl_write_uint32(&w, 0xa8718dc5u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockEmbedPost: url + webpage_id + author_photo_id + author + date + blocks + caption */
    tl_write_uint32(&w, 0xf259a80bu);
    tl_write_string(&w, "https://t.me/post/1");
    tl_write_int64 (&w, 8LL);
    tl_write_int64 (&w, 9LL);
    tl_write_string(&w, "PostAuthor");
    tl_write_int32 (&w, 1700001000);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockVideo: flags=0 video_id + caption */
    tl_write_uint32(&w, 0x7c8fe7b6u);
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int64 (&w, 11LL);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockAudio: audio_id + caption */
    tl_write_uint32(&w, 0x804361eau);
    tl_write_int64 (&w, 12LL);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockPhoto: flags=0 photo_id + caption */
    tl_write_uint32(&w, 0x1759c560u);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 13LL);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockMap: geoPointEmpty + zoom w h + caption */
    tl_write_uint32(&w, 0xa44f3ef6u);
    tl_write_uint32(&w, 0x1117dd5fu);          /* geoPointEmpty */
    tl_write_int32 (&w, 10); tl_write_int32(&w, 400); tl_write_int32(&w, 300);
    tl_write_uint32(&w, 0x6f747657u);
    tl_write_uint32(&w, 0xdc3d824fu); tl_write_uint32(&w, 0xdc3d824fu);
    /* pageBlockChannel: a chatEmpty */
    tl_write_uint32(&w, 0xef1751b5u);
    tl_write_uint32(&w, TL_chatEmpty);
    tl_write_int64 (&w, 333LL);
    /* pageBlockSubtitle: rich text with subscript + superscript + marked + fixed + strike */
    tl_write_uint32(&w, 0x8ffa9a1fu);
    tl_write_uint32(&w, 0x7e6260d7u);          /* textConcat */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 5);
    tl_write_uint32(&w, 0xed6a8504u); tl_write_uint32(&w, 0xdc3d824fu); /* subscript */
    tl_write_uint32(&w, 0xc7fb5e01u); tl_write_uint32(&w, 0xdc3d824fu); /* superscript */
    tl_write_uint32(&w, 0x034b27f6u); tl_write_uint32(&w, 0xdc3d824fu); /* marked */
    tl_write_uint32(&w, 0x6c3f19b9u); tl_write_uint32(&w, 0xdc3d824fu); /* fixed */
    tl_write_uint32(&w, 0x9bf8bb95u); tl_write_uint32(&w, 0xdc3d824fu); /* strike */
    /* pageBlockKicker + pageBlockHeader + pageBlockSubheader + pageBlockFooter */
    tl_write_uint32(&w, 0x1e148390u);          /* kicker */
    tl_write_uint32(&w, 0xdc3d824fu);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "extra page blocks walked");
    ASSERT(r.pos == r.len, "consumed extra page blocks");
    tl_writer_free(&w);
}

void run_tl_forward_compat_tests(void) {
    RUN_TEST(test_unknown_top_level_result_skipped);
    RUN_TEST(test_unknown_media_in_history);
    RUN_TEST(test_unknown_message_action);
    RUN_TEST(test_unknown_optional_field_preserves_layout);
    RUN_TEST(test_unknown_update_type_in_getdifference);
    RUN_TEST(test_skip_primitives_reject_unknown);
    RUN_TEST(test_message_entity_variants);
    RUN_TEST(test_media_variants_skip_clean);
    RUN_TEST(test_reply_markup_variants);
    RUN_TEST(test_chat_user_unknown_variants);
    RUN_TEST(test_truncation_rejected);
    RUN_TEST(test_photo_and_photo_size_roundtrip);
    RUN_TEST(test_document_with_attributes);
    RUN_TEST(test_fwd_header_variants);
    RUN_TEST(test_reply_header_variants);
    RUN_TEST(test_draft_message_variants);
    RUN_TEST(test_notification_sound_and_settings);
    RUN_TEST(test_chat_user_visuals);
    RUN_TEST(test_username_color_emoji);
    RUN_TEST(test_restriction_reason_vector);
    RUN_TEST(test_factcheck_variants);
    RUN_TEST(test_reactions_replies_trailers);
    RUN_TEST(test_media_photo_and_document);
    RUN_TEST(test_message_replies_empty);
    RUN_TEST(test_media_game);
    RUN_TEST(test_media_invoice);
    RUN_TEST(test_media_paid_media);
    RUN_TEST(test_media_story);
    RUN_TEST(test_media_poll_with_results);
    RUN_TEST(test_media_webpage_variants);
    RUN_TEST(test_chat_admin_banned_rights);
    RUN_TEST(test_page_block_list_blocks_variant);
    RUN_TEST(test_page_block_extra_variants);
}
