/**
 * @file test_tl_skip_message_functional.c
 * @brief End-to-end iteration of a heavily-decorated Message object.
 *
 * The unit tests exercise each skipper in isolation. This functional
 * test builds one Message carrying every supported trailer flag at
 * once (media + reply_markup + entities + views + forwards + replies +
 * restriction_reason + reactions + ttl_period + factcheck), feeds it
 * to tl_skip_message, and asserts the reader lands exactly on the
 * bytes that follow. The goal is to catch dispatch-order bugs and
 * cross-skipper interactions that the per-unit tests cannot see.
 */

#include "test_helpers.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"

#include <stdint.h>
#include <string.h>

/* CRCs not re-exposed from tl_skip.c. */
#define CRC_messageMediaEmpty_t       0x3ded6320u
#define CRC_replyInlineMarkup_t       0x48a30254u
#define CRC_keyboardButtonRow_t       0x77608b83u
#define CRC_keyboardButtonCallback_t  0x35bbdb6bu
#define CRC_messageEntityBold_t       0xbd610bc9u
#define CRC_messageReplies_t          0x83d60fc2u
#define CRC_restrictionReason_t       0xd072acb4u
#define CRC_messageReactions_t        0x4f2b9479u
#define CRC_reactionCount_t           0xa3d1cb80u
#define CRC_reactionEmoji_t           0x1b2286b8u
#define CRC_factCheck_t               0xb89bfccfu
#define CRC_textWithEntities_t        0x751f3146u

/* Message flag constants. */
#define FLAG_REPLY_MARKUP   (1u <<  6)
#define FLAG_ENTITIES       (1u <<  7)
#define FLAG_FROM_ID        (1u <<  8)
#define FLAG_MEDIA          (1u <<  9)
#define FLAG_VIEWS_FWDS     (1u << 10)
#define FLAG_EDIT_DATE      (1u << 15)
#define FLAG_POST_AUTHOR    (1u << 16)
#define FLAG_GROUPED_ID     (1u << 17)
#define FLAG_REACTIONS      (1u << 20)
#define FLAG_RESTRICTION    (1u << 22)
#define FLAG_REPLIES        (1u << 23)
#define FLAG_TTL_PERIOD     (1u << 25)
#define FLAG2_FACTCHECK     (1u <<  3)

static void write_message_kitchen_sink(TlWriter *w) {
    uint32_t flags = FLAG_FROM_ID | FLAG_MEDIA | FLAG_REPLY_MARKUP
                   | FLAG_ENTITIES | FLAG_VIEWS_FWDS | FLAG_EDIT_DATE
                   | FLAG_POST_AUTHOR | FLAG_GROUPED_ID | FLAG_REACTIONS
                   | FLAG_RESTRICTION | FLAG_REPLIES | FLAG_TTL_PERIOD;
    uint32_t flags2 = FLAG2_FACTCHECK;

    tl_write_uint32(w, TL_message);
    tl_write_uint32(w, flags);
    tl_write_uint32(w, flags2);
    tl_write_int32 (w, 1001);                 /* message id */

    /* from_id:Peer + peer_id:Peer */
    tl_write_uint32(w, TL_peerUser); tl_write_int64(w, 42LL);
    tl_write_uint32(w, TL_peerChannel); tl_write_int64(w, 5001LL);

    tl_write_int32 (w, 1700000000);           /* date */
    tl_write_string(w, "Kitchen-sink post");  /* message */

    /* media: messageMediaEmpty */
    tl_write_uint32(w, CRC_messageMediaEmpty_t);

    /* reply_markup: replyInlineMarkup + one row with a callback button. */
    tl_write_uint32(w, CRC_replyInlineMarkup_t);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_keyboardButtonRow_t);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_keyboardButtonCallback_t);
    tl_write_uint32(w, 0);                    /* inner flags */
    tl_write_string(w, "Click me");
    tl_write_string(w, "payload");

    /* entities: one bold */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_messageEntityBold_t);
    tl_write_int32 (w, 0); tl_write_int32(w, 7);

    /* views + forwards */
    tl_write_int32 (w, 12345);
    tl_write_int32 (w, 67);

    /* replies: messageReplies flags=0, two int32 */
    tl_write_uint32(w, CRC_messageReplies_t);
    tl_write_uint32(w, 0);
    tl_write_int32 (w, 9);
    tl_write_int32 (w, 200);

    /* edit_date */
    tl_write_int32 (w, 1700001111);

    /* post_author */
    tl_write_string(w, "Anonymous");

    /* grouped_id */
    tl_write_int64 (w, 0xDEADBEEFLL);

    /* reactions: one emoji reaction with 3 voters */
    tl_write_uint32(w, CRC_messageReactions_t);
    tl_write_uint32(w, 0);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_reactionCount_t);
    tl_write_uint32(w, 0);                    /* no chosen_order */
    tl_write_uint32(w, CRC_reactionEmoji_t);
    tl_write_string(w, "\xf0\x9f\x94\xa5"); /* 🔥 */
    tl_write_int32 (w, 3);

    /* restriction_reason: Vector<RestrictionReason>, one entry */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_restrictionReason_t);
    tl_write_string(w, "android");
    tl_write_string(w, "sensitive");
    tl_write_string(w, "Age-restricted");

    /* ttl_period */
    tl_write_int32 (w, 3600);

    /* factcheck: flags.1 with country + TextWithEntities, hash */
    tl_write_uint32(w, CRC_factCheck_t);
    tl_write_uint32(w, (1u << 1));
    tl_write_string(w, "HU");
    tl_write_uint32(w, CRC_textWithEntities_t);
    tl_write_string(w, "verified claim");
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0);
    tl_write_int64 (w, 0xABCDEF0012345678LL);
}

static void test_kitchen_sink_message_iterates_fully(void) {
    TlWriter w; tl_writer_init(&w);
    write_message_kitchen_sink(&w);
    tl_write_int32(&w, 0x0BAD1DEA);           /* sentinel trailer */

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r) == 0,
           "multi-flag Message skipped cleanly");
    ASSERT(tl_read_int32(&r) == 0x0BAD1DEA,
           "cursor landed exactly on sentinel after Message");
    ASSERT(r.pos == r.len, "reader fully consumed");
    tl_writer_free(&w);
}

/* Two Messages in sequence: prove that hitting any trailer does not
 * desynchronise the reader for the next one. */
static void test_two_messages_in_a_row(void) {
    TlWriter w; tl_writer_init(&w);
    write_message_kitchen_sink(&w);
    write_message_kitchen_sink(&w);
    tl_write_int32(&w, 0xCAFEBABE);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message(&r) == 0, "first Message skipped");
    ASSERT(tl_skip_message(&r) == 0, "second Message skipped");
    ASSERT(tl_read_int32(&r) == (int32_t)0xCAFEBABE, "trailer reached");
    tl_writer_free(&w);
}

void run_tl_skip_message_functional_tests(void) {
    RUN_TEST(test_kitchen_sink_message_iterates_fully);
    RUN_TEST(test_two_messages_in_a_row);
}
