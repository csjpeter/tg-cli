/**
 * @file test_tl_skip.c
 * @brief Unit tests for TL object skippers.
 */

#include "test_helpers.h"
#include "tl_skip.h"
#include "tl_serial.h"
#include "tl_registry.h"

#include <stdlib.h>
#include <string.h>

/* CRCs duplicated from tl_skip.c for wire building in tests. */
#define CRC_peerNotifySettings        0xa83b0426u
#define CRC_notificationSoundDefault  0x97e8bebeu
#define CRC_notificationSoundNone     0x6f0c34dfu
#define CRC_notificationSoundLocal    0x830b9ae4u
#define CRC_notificationSoundRingtone 0xff6c8049u
#define CRC_draftMessageEmpty         0x1b0c841au
#define CRC_messageEntityBold         0xbd610bc9u
#define CRC_messageEntityTextUrl      0x76a6d327u
#define CRC_messageFwdHeader          0x4e4df4bbu
#define CRC_messageReplyHeader        0xafbc09dbu
#define CRC_messageMediaEmpty         0x3ded6320u
#define CRC_messageMediaUnsupported   0x9f84f49eu
#define CRC_messageMediaGeo           0x56e0d474u
#define CRC_messageMediaContact       0x70322949u
#define CRC_messageMediaDice          0x3f7ee58bu
#define CRC_messageMediaVenue         0x2ec0533fu
#define CRC_messageMediaPhoto         0x695150d7u
#define CRC_messageMediaDocument      0x4cf4d72du
#define CRC_geoPointEmpty             0x1117dd5fu
#define CRC_geoPoint                  0xb2a2f663u
#define CRC_photo                     0xfb197a65u
#define CRC_photoEmpty                0x2331b22du
#define CRC_documentEmpty             0x36f8c871u
#define CRC_photoSize                 0x75c78e60u
#define CRC_chatPhotoEmpty            0x37c1011cu
#define CRC_chatPhoto                 0x1c6e1c11u
#define CRC_userProfilePhotoEmpty     0x4f11bae1u

static void test_skip_bool(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_bool(&w, 1);
    tl_write_int32(&w, 42); /* sentinel to verify cursor position */
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_bool(&r) == 0, "skip_bool ok");
    ASSERT(tl_read_int32(&r) == 42, "cursor past bool");
    tl_writer_free(&w);
}

static void test_skip_string(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_string(&w, "hello world");
    tl_write_int32(&w, 7);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_string(&r) == 0, "skip_string ok");
    ASSERT(tl_read_int32(&r) == 7, "cursor past string");
    tl_writer_free(&w);
}

static void test_skip_peer(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64(&w, 123456LL);
    tl_write_int32(&w, 99);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_peer(&r) == 0, "skip_peer ok");
    ASSERT(tl_read_int32(&r) == 99, "cursor past peer");
    tl_writer_free(&w);
}

static void test_skip_peer_unknown(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEADBEEF);
    tl_write_int64(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_peer(&r) == -1, "unknown peer rejected");
    tl_writer_free(&w);
}

static void test_skip_notification_sound_default(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_notificationSoundDefault);
    tl_write_int32(&w, 55);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_notification_sound(&r) == 0, "default ok");
    ASSERT(tl_read_int32(&r) == 55, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_notification_sound_ringtone(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_notificationSoundRingtone);
    tl_write_int64(&w, 0x1122334455667788LL);
    tl_write_int32(&w, 88);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_notification_sound(&r) == 0, "ringtone ok");
    ASSERT(tl_read_int32(&r) == 88, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_notification_sound_local(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_notificationSoundLocal);
    tl_write_string(&w, "bell");
    tl_write_string(&w, "bell.mp3");
    tl_write_int32(&w, 33);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_notification_sound(&r) == 0, "local ok");
    ASSERT(tl_read_int32(&r) == 33, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_peer_notify_settings_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_peerNotifySettings);
    tl_write_uint32(&w, 0); /* no flags */
    tl_write_int32(&w, 11);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_peer_notify_settings(&r) == 0, "empty notify ok");
    ASSERT(tl_read_int32(&r) == 11, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_peer_notify_settings_full(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_peerNotifySettings);
    uint32_t flags = (1u << 0)  /* show_previews (bool) */
                    | (1u << 1)  /* silent (bool) */
                    | (1u << 2)  /* mute_until (int) */
                    | (1u << 3)  /* ios_sound (NS) */
                    | (1u << 6); /* stories_muted (bool) */
    tl_write_uint32(&w, flags);
    tl_write_bool(&w, 1);
    tl_write_bool(&w, 0);
    tl_write_int32(&w, 1000);
    tl_write_uint32(&w, CRC_notificationSoundDefault);
    tl_write_bool(&w, 1);
    tl_write_int32(&w, 77);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_peer_notify_settings(&r) == 0, "full notify ok");
    ASSERT(tl_read_int32(&r) == 77, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_draft_message_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_draftMessageEmpty);
    tl_write_uint32(&w, 1); /* flags.0: date present */
    tl_write_int32(&w, 1700000000);
    tl_write_int32(&w, 5);
    TlReader r = tl_reader_init(w.data, w.len);

    ASSERT(tl_skip_draft_message(&r) == 0, "draft empty ok");
    ASSERT(tl_read_int32(&r) == 5, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_message_entity_bold(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageEntityBold);
    tl_write_int32(&w, 5);   /* offset */
    tl_write_int32(&w, 10);  /* length */
    tl_write_int32(&w, 42);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entity(&r) == 0, "bold entity");
    ASSERT(tl_read_int32(&r) == 42, "cursor past entity");
    tl_writer_free(&w);
}

static void test_skip_message_entity_text_url(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageEntityTextUrl);
    tl_write_int32(&w, 0);
    tl_write_int32(&w, 4);
    tl_write_string(&w, "https://example.com");
    tl_write_int32(&w, 42);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entity(&r) == 0, "text_url entity");
    ASSERT(tl_read_int32(&r) == 42, "cursor past entity");
    tl_writer_free(&w);
}

static void test_skip_entities_vector(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, CRC_messageEntityBold);
    tl_write_int32(&w, 0); tl_write_int32(&w, 3);
    tl_write_uint32(&w, CRC_messageEntityBold);
    tl_write_int32(&w, 10); tl_write_int32(&w, 5);
    tl_write_int32(&w, 99);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entities_vector(&r) == 0, "entities vector");
    ASSERT(tl_read_int32(&r) == 99, "cursor past vector");
    tl_writer_free(&w);
}

static void test_skip_fwd_header_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageFwdHeader);
    tl_write_uint32(&w, 0); /* no flags */
    tl_write_int32(&w, 1700000000); /* date */
    tl_write_int32(&w, 21);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r) == 0, "fwd header minimal");
    ASSERT(tl_read_int32(&r) == 21, "cursor past fwd_header");
    tl_writer_free(&w);
}

static void test_skip_fwd_header_with_channel(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageFwdHeader);
    uint32_t flags = (1u << 0)    /* from_id */
                   | (1u << 2)    /* channel_post */
                   | (1u << 3);   /* post_author */
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, TL_peerChannel);
    tl_write_int64(&w, -1001234567890LL);
    tl_write_int32(&w, 1700000000); /* date */
    tl_write_int32(&w, 42);         /* channel_post */
    tl_write_string(&w, "author");
    tl_write_int32(&w, 77);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r) == 0, "fwd header with channel");
    ASSERT(tl_read_int32(&r) == 77, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_reply_header_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReplyHeader);
    tl_write_uint32(&w, (1u << 4)); /* reply_to_msg_id */
    tl_write_int32(&w, 1234);
    tl_write_int32(&w, 88);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r) == 0, "reply header minimal");
    ASSERT(tl_read_int32(&r) == 88, "cursor past");
    tl_writer_free(&w);
}

/* ---- Error / short-buffer coverage ---- */

static void test_skip_bool_short(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0); /* only 4 bytes */
    TlReader r = tl_reader_init(w.data, 2); /* truncated */
    ASSERT(tl_skip_bool(&r) == -1, "short bool rejected");
    tl_writer_free(&w);
}

static void test_skip_notification_sound_unknown(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEADBEEF);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_notification_sound(&r) == -1, "unknown NS rejected");
    tl_writer_free(&w);
}

static void test_skip_peer_notify_settings_wrong_crc(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xBADF00D);
    tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_peer_notify_settings(&r) == -1, "wrong CRC rejected");
    tl_writer_free(&w);
}

static void test_skip_draft_message_nonempty(void) {
    /* Non-empty draftMessage (0x3fccf7ef) is not parseable yet — must bail. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x3fccf7efu);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r) == -1, "non-empty draft bails");
    tl_writer_free(&w);
}

static void test_skip_draft_message_unknown(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xCAFEBABE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_draft_message(&r) == -1, "unknown draft rejected");
    tl_writer_free(&w);
}

static void test_skip_message_entity_unknown(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEADCAFE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entity(&r) == -1, "unknown entity rejected");
    tl_writer_free(&w);
}

static void test_skip_entities_vector_wrong_header(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xBADBADBA); /* not TL_vector */
    tl_write_uint32(&w, 0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_entities_vector(&r) == -1, "non-vector rejected");
    tl_writer_free(&w);
}

static void test_skip_fwd_header_wrong_crc(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x12345678);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_fwd_header(&r) == -1, "bad fwd crc rejected");
    tl_writer_free(&w);
}

static void test_skip_reply_header_wrong_crc(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0x87654321);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r) == -1, "bad reply crc rejected");
    tl_writer_free(&w);
}

static void test_skip_reply_header_reply_media_bail(void) {
    /* flags.8 set → reply_media is present but we haven't implemented the
     * MessageMedia skipper, so must bail. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xafbc09dbu);
    tl_write_uint32(&w, (1u << 8));
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reply_header(&r) == -1, "reply_media bail");
    tl_writer_free(&w);
}

/* ---- MessageMedia skipper tests ---- */

static void test_skip_media_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaEmpty);
    tl_write_int32(&w, 101);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media empty");
    ASSERT(tl_read_int32(&r) == 101, "cursor past media empty");
    tl_writer_free(&w);
}

static void test_skip_media_unsupported(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaUnsupported);
    tl_write_int32(&w, 202);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media unsupported");
    ASSERT(tl_read_int32(&r) == 202, "cursor past media unsupported");
    tl_writer_free(&w);
}

static void test_skip_media_geo_empty_point(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGeo);
    tl_write_uint32(&w, CRC_geoPointEmpty);
    tl_write_int32(&w, 303);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "geo empty");
    ASSERT(tl_read_int32(&r) == 303, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_geo(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGeo);
    tl_write_uint32(&w, CRC_geoPoint);
    tl_write_uint32(&w, 0);        /* flags */
    tl_write_double(&w, 19.12);    /* long */
    tl_write_double(&w, 47.49);    /* lat */
    tl_write_int64(&w, 0xCAFE);    /* access_hash */
    tl_write_int32(&w, 404);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "geo full");
    ASSERT(tl_read_int32(&r) == 404, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_contact(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaContact);
    tl_write_string(&w, "+15551234567");
    tl_write_string(&w, "Alice");
    tl_write_string(&w, "Smith");
    tl_write_string(&w, "");      /* vcard */
    tl_write_int64(&w, 42LL);
    tl_write_int32(&w, 505);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "contact");
    ASSERT(tl_read_int32(&r) == 505, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_dice(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDice);
    tl_write_int32(&w, 6);
    tl_write_string(&w, "🎲");
    tl_write_int32(&w, 606);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "dice");
    ASSERT(tl_read_int32(&r) == 606, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_venue(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaVenue);
    tl_write_uint32(&w, CRC_geoPointEmpty);
    tl_write_string(&w, "Title");
    tl_write_string(&w, "Address");
    tl_write_string(&w, "Provider");
    tl_write_string(&w, "VenueID");
    tl_write_string(&w, "Type");
    tl_write_int32(&w, 707);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "venue");
    ASSERT(tl_read_int32(&r) == 707, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_photo_empty_photo(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPhoto);
    tl_write_uint32(&w, (1u << 0));   /* flags: photo present */
    tl_write_uint32(&w, CRC_photoEmpty);
    tl_write_int64(&w, 1234567890LL);
    tl_write_int32(&w, 808);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media-photo with photoEmpty");
    ASSERT(tl_read_int32(&r) == 808, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_photo_with_sizes(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPhoto);
    tl_write_uint32(&w, (1u << 0));        /* photo flag */
    /* photo#fb197a65 flags:# id:long access_hash:long file_reference:bytes
     *                date:int sizes:Vector<PhotoSize> dc_id:int */
    tl_write_uint32(&w, CRC_photo);
    tl_write_uint32(&w, 0);                 /* photo flags */
    tl_write_int64(&w, 111LL);              /* id */
    tl_write_int64(&w, 222LL);              /* access_hash */
    uint8_t fr[8] = {1,2,3,4,5,6,7,8};
    tl_write_bytes(&w, fr, 8);              /* file_reference */
    tl_write_int32(&w, 1700000000);         /* date */
    /* sizes vector: one photoSize */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_photoSize);
    tl_write_string(&w, "x");
    tl_write_int32(&w, 640); tl_write_int32(&w, 480); tl_write_int32(&w, 12345);
    tl_write_int32(&w, 2);                  /* dc_id */
    tl_write_int32(&w, 909);                /* sentinel */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media-photo with photoSize");
    ASSERT(tl_read_int32(&r) == 909, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_document_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument);
    tl_write_uint32(&w, (1u << 0));        /* document present */
    tl_write_uint32(&w, CRC_documentEmpty);
    tl_write_int64(&w, 99999LL);
    tl_write_int32(&w, 1010);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media(&r) == 0, "media-document empty");
    ASSERT(tl_read_int32(&r) == 1010, "cursor past");
    tl_writer_free(&w);
}

/* ---- Chat / User skipper tests ---- */

static void test_skip_chat_forbidden(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_chatForbidden);
    tl_write_int64(&w, 12345LL);
    tl_write_string(&w, "Forbidden Chat");
    tl_write_int32(&w, 1111);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat(&r) == 0, "chatForbidden skip");
    ASSERT(tl_read_int32(&r) == 1111, "cursor past chatForbidden");
    tl_writer_free(&w);
}

static void test_skip_channel_forbidden(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_channelForbidden);
    tl_write_uint32(&w, 0);                       /* flags: no until_date */
    tl_write_int64(&w, -1001234567LL);            /* id */
    tl_write_int64(&w, 0xCAFEBABELL);             /* access_hash */
    tl_write_string(&w, "Forbidden Channel");
    tl_write_int32(&w, 2222);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat(&r) == 0, "channelForbidden skip");
    ASSERT(tl_read_int32(&r) == 2222, "cursor past channelForbidden");
    tl_writer_free(&w);
}

static void test_skip_chat_plain(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_chat);
    tl_write_uint32(&w, 0);                        /* flags (no optionals) */
    tl_write_int64(&w, 42LL);                      /* id */
    tl_write_string(&w, "Group Name");
    tl_write_uint32(&w, CRC_chatPhotoEmpty);       /* photo */
    tl_write_int32(&w, 10);                        /* participants_count */
    tl_write_int32(&w, 1700000000);                /* date */
    tl_write_int32(&w, 3);                         /* version */
    tl_write_int32(&w, 3333);                      /* sentinel */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat(&r) == 0, "chat skip");
    ASSERT(tl_read_int32(&r) == 3333, "cursor past chat");
    tl_writer_free(&w);
}

static void test_skip_channel_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_channel);
    tl_write_uint32(&w, 0);                        /* flags */
    tl_write_uint32(&w, 0);                        /* flags2 */
    tl_write_int64(&w, -1009876543LL);             /* id */
    tl_write_string(&w, "Chan");
    tl_write_uint32(&w, CRC_chatPhotoEmpty);       /* photo */
    tl_write_int32(&w, 1700000000);                /* date */
    tl_write_int32(&w, 4444);                      /* sentinel */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_chat(&r) == 0, "channel skip");
    ASSERT(tl_read_int32(&r) == 4444, "cursor past channel");
    tl_writer_free(&w);
}

static void test_skip_user_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_userEmpty);
    tl_write_int64(&w, 777LL);
    tl_write_int32(&w, 5555);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user(&r) == 0, "userEmpty skip");
    ASSERT(tl_read_int32(&r) == 5555, "cursor past userEmpty");
    tl_writer_free(&w);
}

static void test_skip_user_full(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_user);
    uint32_t flags = (1u << 1)   /* first_name */
                   | (1u << 2)   /* last_name */
                   | (1u << 3);  /* username */
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);                /* flags2 */
    tl_write_int64(&w, 999LL);             /* id */
    tl_write_string(&w, "Alice");
    tl_write_string(&w, "Smith");
    tl_write_string(&w, "asmith");
    tl_write_int32(&w, 6666);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_user(&r) == 0, "user skip");
    ASSERT(tl_read_int32(&r) == 6666, "cursor past user");
    tl_writer_free(&w);
}

static void test_extract_chat_forbidden(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_chatForbidden);
    tl_write_int64(&w, 0xABCDEF00LL);
    tl_write_string(&w, "Secret Room");
    tl_write_int32(&w, 7777);
    TlReader r = tl_reader_init(w.data, w.len);
    ChatSummary cs;
    ASSERT(tl_extract_chat(&r, &cs) == 0, "extract chatForbidden");
    ASSERT(cs.id == 0xABCDEF00LL, "chat id");
    ASSERT(strcmp(cs.title, "Secret Room") == 0, "chat title");
    ASSERT(tl_read_int32(&r) == 7777, "cursor past extract");
    tl_writer_free(&w);
}

static void test_extract_user(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_user);
    uint32_t flags = (1u << 1) | (1u << 2) | (1u << 3);
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);
    tl_write_int64(&w, 0x1234567890LL);
    tl_write_string(&w, "Alice");
    tl_write_string(&w, "Smith");
    tl_write_string(&w, "asmith");
    tl_write_int32(&w, 8888);
    TlReader r = tl_reader_init(w.data, w.len);
    UserSummary us;
    ASSERT(tl_extract_user(&r, &us) == 0, "extract user");
    ASSERT(us.id == 0x1234567890LL, "user id");
    ASSERT(strcmp(us.name, "Alice Smith") == 0, "user name joined");
    ASSERT(strcmp(us.username, "asmith") == 0, "user username");
    ASSERT(tl_read_int32(&r) == 8888, "cursor past extract user");
    tl_writer_free(&w);
}

/* ---- ReplyMarkup skipper ---- */

#define CRC_replyKeyboardHide            0xa03e5b85u
#define CRC_replyKeyboardForceReply      0x86b40b08u
#define CRC_replyInlineMarkup_test       0x48a30254u
#define CRC_keyboardButtonRow_test       0x77608b83u
#define CRC_keyboardButtonUrl_test       0x258aff05u
#define CRC_keyboardButtonCallback_test  0x35bbdb6bu

static void test_skip_reply_markup_hide(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_replyKeyboardHide);
    tl_write_uint32(&w, 0);                 /* flags */
    tl_write_int32 (&w, 777);                /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == 0, "replyKeyboardHide skipped");
    ASSERT(tl_read_int32(&r) == 777, "cursor past hide");
    tl_writer_free(&w);
}

static void test_skip_reply_markup_force_reply_with_placeholder(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_replyKeyboardForceReply);
    tl_write_uint32(&w, (1u << 3));         /* placeholder flag */
    tl_write_string(&w, "Type here...");
    tl_write_int32 (&w, 42);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == 0, "forceReply w/ placeholder skipped");
    ASSERT(tl_read_int32(&r) == 42, "cursor past forceReply");
    tl_writer_free(&w);
}

static void test_skip_reply_markup_inline_url_callback(void) {
    TlWriter w; tl_writer_init(&w);
    /* replyInlineMarkup with one row, two buttons (url + callback). */
    tl_write_uint32(&w, CRC_replyInlineMarkup_test);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                             /* row count */
    tl_write_uint32(&w, CRC_keyboardButtonRow_test);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);                             /* button count */
    tl_write_uint32(&w, CRC_keyboardButtonUrl_test);
    tl_write_string(&w, "Open");
    tl_write_string(&w, "https://example.com");
    tl_write_uint32(&w, CRC_keyboardButtonCallback_test);
    tl_write_uint32(&w, 0);                             /* inner flags */
    tl_write_string(&w, "Click");
    tl_write_string(&w, "payload-bytes");
    tl_write_int32 (&w, 12345);                         /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == 0, "inline url+callback skipped");
    ASSERT(tl_read_int32(&r) == 12345, "cursor past inline markup");
    tl_writer_free(&w);
}

static void test_skip_reply_markup_unknown_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xdeadbeefu);       /* unknown ReplyMarkup variant */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == -1, "unknown variant rejected");
    tl_writer_free(&w);
}

static void test_skip_reply_markup_unknown_button_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_replyInlineMarkup_test);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_keyboardButtonRow_test);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xfeedfacu);        /* unknown button */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_reply_markup(&r) == -1, "unknown button rejected");
    tl_writer_free(&w);
}

/* ---- MessageReactions skipper ---- */

#define CRC_messageReactions_test 0x4f2b9479u
#define CRC_reactionCount_test    0xa3d1cb80u
#define CRC_reactionEmoji_test    0x1b2286b8u
#define CRC_reactionCustomEmoji_t 0x8935fc73u
#define CRC_reactionEmpty_test    0x79f5d419u

static void test_skip_reactions_empty_results(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReactions_test);
    tl_write_uint32(&w, 0);                  /* flags */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);                  /* empty results */
    tl_write_int32 (&w, 555);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r) == 0, "empty reactions");
    ASSERT(tl_read_int32(&r) == 555, "cursor past reactions");
    tl_writer_free(&w);
}

static void test_skip_reactions_emoji_and_custom(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReactions_test);
    tl_write_uint32(&w, 0);                  /* flags */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);                  /* 2 reaction counts */

    /* #1: emoji reaction, chosen_order set. */
    tl_write_uint32(&w, CRC_reactionCount_test);
    tl_write_uint32(&w, (1u << 0));          /* chosen_order present */
    tl_write_int32 (&w, 1);                  /* chosen_order */
    tl_write_uint32(&w, CRC_reactionEmoji_test);
    tl_write_string(&w, "\xf0\x9f\x91\x8d");  /* 👍 */
    tl_write_int32 (&w, 7);                  /* count */

    /* #2: custom emoji reaction, no chosen_order. */
    tl_write_uint32(&w, CRC_reactionCount_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_reactionCustomEmoji_t);
    tl_write_int64 (&w, 0x1234567890abcdefLL);
    tl_write_int32 (&w, 3);

    tl_write_int32 (&w, 9999);               /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r) == 0, "reactions skipped");
    ASSERT(tl_read_int32(&r) == 9999, "cursor past reactions");
    tl_writer_free(&w);
}

static void test_skip_reactions_recent_reactors_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReactions_test);
    tl_write_uint32(&w, (1u << 1));          /* recent_reactions present */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);                  /* empty results */
    /* recent_reactions would follow — we bail before reading. */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_reactions(&r) == -1, "bails on recent_reactions");
    tl_writer_free(&w);
}

void run_tl_skip_tests(void) {
    RUN_TEST(test_skip_bool);
    RUN_TEST(test_skip_string);
    RUN_TEST(test_skip_peer);
    RUN_TEST(test_skip_peer_unknown);
    RUN_TEST(test_skip_notification_sound_default);
    RUN_TEST(test_skip_notification_sound_ringtone);
    RUN_TEST(test_skip_notification_sound_local);
    RUN_TEST(test_skip_peer_notify_settings_empty);
    RUN_TEST(test_skip_peer_notify_settings_full);
    RUN_TEST(test_skip_draft_message_empty);
    RUN_TEST(test_skip_message_entity_bold);
    RUN_TEST(test_skip_message_entity_text_url);
    RUN_TEST(test_skip_entities_vector);
    RUN_TEST(test_skip_fwd_header_minimal);
    RUN_TEST(test_skip_fwd_header_with_channel);
    RUN_TEST(test_skip_reply_header_minimal);
    RUN_TEST(test_skip_bool_short);
    RUN_TEST(test_skip_notification_sound_unknown);
    RUN_TEST(test_skip_peer_notify_settings_wrong_crc);
    RUN_TEST(test_skip_draft_message_nonempty);
    RUN_TEST(test_skip_draft_message_unknown);
    RUN_TEST(test_skip_message_entity_unknown);
    RUN_TEST(test_skip_entities_vector_wrong_header);
    RUN_TEST(test_skip_fwd_header_wrong_crc);
    RUN_TEST(test_skip_reply_header_wrong_crc);
    RUN_TEST(test_skip_reply_header_reply_media_bail);
    RUN_TEST(test_skip_media_empty);
    RUN_TEST(test_skip_media_unsupported);
    RUN_TEST(test_skip_media_geo_empty_point);
    RUN_TEST(test_skip_media_geo);
    RUN_TEST(test_skip_media_contact);
    RUN_TEST(test_skip_media_dice);
    RUN_TEST(test_skip_media_venue);
    RUN_TEST(test_skip_media_photo_empty_photo);
    RUN_TEST(test_skip_media_photo_with_sizes);
    RUN_TEST(test_skip_media_document_empty);
    RUN_TEST(test_skip_chat_forbidden);
    RUN_TEST(test_skip_channel_forbidden);
    RUN_TEST(test_skip_chat_plain);
    RUN_TEST(test_skip_channel_minimal);
    RUN_TEST(test_skip_user_empty);
    RUN_TEST(test_skip_user_full);
    RUN_TEST(test_extract_chat_forbidden);
    RUN_TEST(test_extract_user);
    RUN_TEST(test_skip_reply_markup_hide);
    RUN_TEST(test_skip_reply_markup_force_reply_with_placeholder);
    RUN_TEST(test_skip_reply_markup_inline_url_callback);
    RUN_TEST(test_skip_reply_markup_unknown_bails);
    RUN_TEST(test_skip_reply_markup_unknown_button_bails);
    RUN_TEST(test_skip_reactions_empty_results);
    RUN_TEST(test_skip_reactions_emoji_and_custom);
    RUN_TEST(test_skip_reactions_recent_reactors_bails);
}
