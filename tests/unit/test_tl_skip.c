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
    ASSERT(us.have_access_hash == 0, "no access_hash when flag clear");
    ASSERT(us.access_hash == 0, "access_hash zero when absent");
    ASSERT(strcmp(us.name, "Alice Smith") == 0, "user name joined");
    ASSERT(strcmp(us.username, "asmith") == 0, "user username");
    ASSERT(tl_read_int32(&r) == 8888, "cursor past extract user");
    tl_writer_free(&w);
}

static void test_extract_user_access_hash(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_user);
    uint32_t flags = (1u << 0) | (1u << 1) | (1u << 3); /* access_hash + first_name + username */
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);
    tl_write_int64(&w, 123LL);
    tl_write_int64(&w, 0xDEADBEEFCAFEBABELL); /* access_hash */
    tl_write_string(&w, "Bob");
    tl_write_string(&w, "bob");
    tl_write_int32(&w, 9999);
    TlReader r = tl_reader_init(w.data, w.len);
    UserSummary us;
    ASSERT(tl_extract_user(&r, &us) == 0, "extract user with access_hash");
    ASSERT(us.id == 123LL, "user id");
    ASSERT(us.have_access_hash == 1, "have_access_hash set");
    ASSERT(us.access_hash == (int64_t)0xDEADBEEFCAFEBABELL, "access_hash value");
    ASSERT(strcmp(us.name, "Bob") == 0, "user name");
    ASSERT(strcmp(us.username, "bob") == 0, "user username");
    ASSERT(tl_read_int32(&r) == 9999, "cursor past user");
    tl_writer_free(&w);
}

static void test_extract_channel_access_hash(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_channel);
    uint32_t flags = (1u << 13);               /* access_hash */
    tl_write_uint32(&w, flags);
    tl_write_uint32(&w, 0);                    /* flags2 */
    tl_write_int64(&w, -1001234567LL);         /* id */
    tl_write_int64(&w, 0x1111222233334444LL);  /* access_hash */
    tl_write_string(&w, "My Channel");
    tl_write_uint32(&w, CRC_chatPhotoEmpty);   /* photo */
    tl_write_int32(&w, 1700000000);            /* date */
    tl_write_int32(&w, 5555);                  /* sentinel */
    TlReader r = tl_reader_init(w.data, w.len);
    ChatSummary cs;
    ASSERT(tl_extract_chat(&r, &cs) == 0, "extract channel with access_hash");
    ASSERT(cs.id == -1001234567LL, "channel id");
    ASSERT(cs.have_access_hash == 1, "have_access_hash set");
    ASSERT(cs.access_hash == 0x1111222233334444LL, "access_hash value");
    ASSERT(strcmp(cs.title, "My Channel") == 0, "channel title");
    ASSERT(tl_read_int32(&r) == 5555, "cursor past channel");
    tl_writer_free(&w);
}

static void test_extract_channel_forbidden_access_hash(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_channelForbidden);
    tl_write_uint32(&w, 0);                    /* flags: no until_date */
    tl_write_int64(&w, -1009999999LL);
    tl_write_int64(&w, 0xA5A5A5A5A5A5A5A5LL);  /* access_hash */
    tl_write_string(&w, "Forbidden");
    tl_write_int32(&w, 6666);
    TlReader r = tl_reader_init(w.data, w.len);
    ChatSummary cs;
    ASSERT(tl_extract_chat(&r, &cs) == 0, "extract channelForbidden");
    ASSERT(cs.have_access_hash == 1, "have_access_hash set");
    ASSERT(cs.access_hash == (int64_t)0xA5A5A5A5A5A5A5A5LL, "access_hash value");
    ASSERT(strcmp(cs.title, "Forbidden") == 0, "title extracted");
    ASSERT(tl_read_int32(&r) == 6666, "cursor past channelForbidden");
    tl_writer_free(&w);
}

static void test_extract_chat_no_access_hash(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_chat);
    tl_write_uint32(&w, 0);                    /* flags */
    tl_write_int64(&w, 42LL);
    tl_write_string(&w, "Legacy Group");
    tl_write_uint32(&w, CRC_chatPhotoEmpty);
    tl_write_int32(&w, 5);
    tl_write_int32(&w, 1700000000);
    tl_write_int32(&w, 1);
    tl_write_int32(&w, 7777);
    TlReader r = tl_reader_init(w.data, w.len);
    ChatSummary cs;
    ASSERT(tl_extract_chat(&r, &cs) == 0, "extract legacy chat");
    ASSERT(cs.id == 42LL, "chat id");
    ASSERT(cs.have_access_hash == 0, "legacy chat has no access_hash");
    ASSERT(strcmp(cs.title, "Legacy Group") == 0, "title");
    ASSERT(tl_read_int32(&r) == 7777, "cursor past chat");
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

/* ---- MessageReplies skipper ---- */

#define CRC_messageReplies_test 0x83d60fc2u

static void test_skip_message_replies_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReplies_test);
    tl_write_uint32(&w, 0);                     /* flags = 0 */
    tl_write_int32 (&w, 5);                     /* replies */
    tl_write_int32 (&w, 100);                   /* replies_pts */
    tl_write_int32 (&w, 0xCAFE);                /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r) == 0, "minimal replies");
    ASSERT(tl_read_int32(&r) == 0xCAFE, "cursor past replies");
    tl_writer_free(&w);
}

static void test_skip_message_replies_with_commenters(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageReplies_test);
    tl_write_uint32(&w, (1u << 0) | (1u << 1) | (1u << 2));
    tl_write_int32 (&w, 9);                     /* replies */
    tl_write_int32 (&w, 200);                   /* replies_pts */
    /* recent_repliers:Vector<Peer> = 2 peers */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, TL_peerUser);    tl_write_int64(&w, 111LL);
    tl_write_uint32(&w, TL_peerChannel); tl_write_int64(&w, 222LL);
    tl_write_int64 (&w, 0xDEADBEEFLL);          /* channel_id */
    tl_write_int32 (&w, 2048);                  /* max_id */
    tl_write_int32 (&w, 1337);                  /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r) == 0, "full replies skipped");
    ASSERT(tl_read_int32(&r) == 1337, "cursor past replies");
    tl_writer_free(&w);
}

static void test_skip_message_replies_unknown_crc_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xbadbadbu);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_replies(&r) == -1, "unknown crc rejected");
    tl_writer_free(&w);
}

/* ---- FactCheck skipper ---- */

#define CRC_factCheck_test          0xb89bfccfu
#define CRC_textWithEntities_test   0x751f3146u

static void test_skip_factcheck_need_check_only(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_factCheck_test);
    tl_write_uint32(&w, (1u << 0));              /* only need_check */
    tl_write_int64 (&w, 0x1234567890LL);         /* hash */
    tl_write_int32 (&w, 777);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r) == 0, "factcheck need_check skipped");
    ASSERT(tl_read_int32(&r) == 777, "cursor past factcheck");
    tl_writer_free(&w);
}

static void test_skip_factcheck_with_text(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_factCheck_test);
    tl_write_uint32(&w, (1u << 1));              /* country + text set */
    tl_write_string(&w, "HU");
    tl_write_uint32(&w, CRC_textWithEntities_test);
    tl_write_string(&w, "Fact-checked claim");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xbd610bc9u);           /* messageEntityBold */
    tl_write_int32 (&w, 0);
    tl_write_int32 (&w, 4);
    tl_write_int64 (&w, 0xabcdefLL);             /* hash */
    tl_write_int32 (&w, 0x1234);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r) == 0, "factcheck with text skipped");
    ASSERT(tl_read_int32(&r) == 0x1234, "cursor past factcheck");
    tl_writer_free(&w);
}

/* ---- messageMediaPoll ---- */

#define CRC_messageMediaPoll_t  0x4bd6e798u
#define CRC_poll_t              0x58747131u
#define CRC_pollAnswer_t        0x6ca9c2e9u
#define CRC_pollResults_t       0x7adc669du
#define CRC_textWithEntities_p  0x751f3146u

static void write_textWithEntities(TlWriter *w, const char *text) {
    tl_write_uint32(w, CRC_textWithEntities_p);
    tl_write_string(w, text);
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 0);                       /* no entities */
}

static void test_skip_media_poll_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPoll_t);
    /* Poll */
    tl_write_uint32(&w, CRC_poll_t);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int64 (&w, 1234567LL);              /* id */
    write_textWithEntities(&w, "Favourite colour?");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, CRC_pollAnswer_t);
    write_textWithEntities(&w, "Blue");
    tl_write_string(&w, "b");
    tl_write_uint32(&w, CRC_pollAnswer_t);
    write_textWithEntities(&w, "Red");
    tl_write_string(&w, "r");

    /* PollResults: empty flags = no results, no voters, no solution */
    tl_write_uint32(&w, CRC_pollResults_t);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int32 (&w, 0xC0FFEE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "poll minimal ok");
    ASSERT(mi.kind == MEDIA_POLL, "kind = poll");
    ASSERT(tl_read_int32(&r) == 0xC0FFEE, "cursor past poll");
    tl_writer_free(&w);
}

static void test_skip_media_poll_with_results(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPoll_t);
    /* Poll — quiz w/ close_date (flags.3 + flags.5) */
    tl_write_uint32(&w, CRC_poll_t);
    tl_write_uint32(&w, (1u << 3) | (1u << 5));
    tl_write_int64 (&w, 9LL);
    write_textWithEntities(&w, "2+2?");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_pollAnswer_t);
    write_textWithEntities(&w, "4");
    tl_write_string(&w, "1");
    tl_write_int32 (&w, 1700000000);            /* close_date */

    /* PollResults with results + total_voters + recent_voters + solution */
    tl_write_uint32(&w, CRC_pollResults_t);
    tl_write_uint32(&w, (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4));
    /* results */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x3b6ddad2u);            /* pollAnswerVoters */
    tl_write_uint32(&w, (1u << 0) | (1u << 1));  /* chosen + correct */
    tl_write_string(&w, "1");                    /* option:bytes */
    tl_write_int32 (&w, 42);                     /* voters */
    /* total_voters */
    tl_write_int32 (&w, 42);
    /* recent_voters */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 999LL);
    /* solution + solution_entities */
    tl_write_string(&w, "Trivia says 4.");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);

    tl_write_int32 (&w, 0xDEAD);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "poll w/ results ok");
    ASSERT(mi.kind == MEDIA_POLL, "kind = poll");
    ASSERT(tl_read_int32(&r) == 0xDEAD, "cursor past poll");
    tl_writer_free(&w);
}

/* ---- messageMediaInvoice / Story / Giveaway ---- */

#define CRC_messageMediaInvoice_t  0xf6a548d3u
#define CRC_messageMediaStory_t    0x68cb6283u
#define CRC_messageMediaGiveaway_t 0xaa073beeu

static void test_skip_media_invoice_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaInvoice_t);
    tl_write_uint32(&w, 0);                       /* flags */
    tl_write_string(&w, "Pizza Margherita");
    tl_write_string(&w, "1 piece");
    tl_write_string(&w, "EUR");
    tl_write_int64 (&w, 999);                     /* cents */
    tl_write_string(&w, "start_abc");
    tl_write_int32 (&w, 0xBEEF);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "invoice minimal ok");
    ASSERT(mi.kind == MEDIA_INVOICE, "kind=invoice");
    ASSERT(tl_read_int32(&r) == 0xBEEF, "cursor past invoice");
    tl_writer_free(&w);
}

static void test_skip_media_invoice_photo_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaInvoice_t);
    tl_write_uint32(&w, (1u << 0));               /* photo flag */
    tl_write_string(&w, "T"); tl_write_string(&w, "D");
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1,
           "invoice photo still bails");
    tl_writer_free(&w);
}

static void test_skip_media_story_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, 0);                       /* flags */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 42LL);
    tl_write_int32 (&w, 77);                      /* story id */
    tl_write_int32 (&w, 0x1010);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "story minimal ok");
    ASSERT(mi.kind == MEDIA_STORY, "kind=story");
    ASSERT(tl_read_int32(&r) == 0x1010, "cursor past story");
    tl_writer_free(&w);
}

static void test_skip_media_giveaway_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGiveaway_t);
    tl_write_uint32(&w, 0);                       /* flags */
    /* channels vector */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_int64 (&w, 10LL);
    tl_write_int64 (&w, 20LL);
    tl_write_int32 (&w, 5);                       /* quantity */
    tl_write_int32 (&w, 1700000999);              /* until_date */
    tl_write_int32 (&w, 0xFACE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "giveaway minimal ok");
    ASSERT(mi.kind == MEDIA_GIVEAWAY, "kind=giveaway");
    ASSERT(tl_read_int32(&r) == 0xFACE, "cursor past giveaway");
    tl_writer_free(&w);
}

static void test_skip_media_giveaway_countries_prize(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGiveaway_t);
    tl_write_uint32(&w, (1u << 1) | (1u << 3) | (1u << 4));
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_int64 (&w, 99LL);
    /* countries_iso2 */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_string(&w, "HU");
    tl_write_string(&w, "DE");
    /* prize_description */
    tl_write_string(&w, "Premium subscription");
    tl_write_int32 (&w, 10);                      /* quantity */
    tl_write_int32 (&w, 3);                       /* months (flag.4) */
    tl_write_int32 (&w, 1700001000);              /* until_date */
    tl_write_int32 (&w, 0xEEEE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "giveaway rich ok");
    ASSERT(tl_read_int32(&r) == 0xEEEE, "cursor past giveaway");
    tl_writer_free(&w);
}

/* ---- messageMediaDocument with full Document ---- */

#define CRC_messageMediaDocument_t    0x4cf4d72du
#define CRC_document_t                0x8fd4c4d8u
#define CRC_documentAttributeFilename_t 0x15590068u

static void test_skip_media_document_full(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));                /* document present */
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, 0);                        /* flags (no thumbs) */
    tl_write_int64 (&w, 5551212LL);
    tl_write_int64 (&w, 0xCAFECAFEDEADBEEFLL);
    uint8_t fr[4] = { 0x01, 0x02, 0x03, 0x04 };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);               /* date */
    tl_write_string(&w, "application/pdf");
    tl_write_int64 (&w, 1024LL);                   /* size */
    tl_write_int32 (&w, 2);                        /* dc_id */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                        /* one attribute */
    tl_write_uint32(&w, CRC_documentAttributeFilename_t);
    tl_write_string(&w, "report.pdf");
    tl_write_int32 (&w, 0xfeed);                   /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "document extract ok");
    ASSERT(mi.kind == MEDIA_DOCUMENT, "kind=document");
    ASSERT(mi.document_id == 5551212LL, "document_id captured");
    ASSERT(mi.access_hash == (int64_t)0xCAFECAFEDEADBEEFLL, "access_hash");
    ASSERT(mi.file_reference_len == 4, "file_reference_len");
    ASSERT(mi.file_reference[0] == 0x01 && mi.file_reference[3] == 0x04,
           "file_reference bytes");
    ASSERT(mi.document_size == 1024, "size captured");
    ASSERT(strcmp(mi.document_mime, "application/pdf") == 0, "mime");
    ASSERT(strcmp(mi.document_filename, "report.pdf") == 0, "filename");
    ASSERT(tl_read_int32(&r) == 0xfeed, "cursor past document");
    tl_writer_free(&w);
}

/* LIM-02: Document with thumbs (Vector<PhotoSize>) set via flags.0. */
#define CRC_photoSize_t              0x75c78e60u
static void test_skip_media_document_with_thumbs(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, (1u << 0));                /* thumbs present */
    tl_write_int64 (&w, 777LL);
    tl_write_int64 (&w, 888LL);
    uint8_t fr[2] = { 0xaa, 0xbb };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "video/mp4");
    tl_write_int64 (&w, 42LL);
    /* thumbs: [photoSize "m" 320x240 size=1234] */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_photoSize_t);
    tl_write_string(&w, "m");
    tl_write_int32 (&w, 320);
    tl_write_int32 (&w, 240);
    tl_write_int32 (&w, 1234);
    tl_write_int32 (&w, 4);                        /* dc_id */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);                        /* no attributes */
    tl_write_int32 (&w, 0x1234);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0,
           "document with thumbs iterates");
    ASSERT(mi.kind == MEDIA_DOCUMENT, "kind=document");
    ASSERT(mi.document_id == 777LL, "id");
    ASSERT(tl_read_int32(&r) == 0x1234, "cursor past document");
    tl_writer_free(&w);
}

/* LIM-02: Document with video_thumbs (Vector<VideoSize>). */
#define CRC_videoSize_t              0xde33b094u
static void test_skip_media_document_with_video_thumbs(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, (1u << 1));                /* video_thumbs present */
    tl_write_int64 (&w, 99LL);
    tl_write_int64 (&w, 100LL);
    uint8_t fr[1] = { 0x01 };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "video/mp4");
    tl_write_int64 (&w, 999LL);
    /* video_thumbs: [videoSize flags=0 "u" 100x100 size=50] */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_videoSize_t);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 100);
    tl_write_int32 (&w, 100);
    tl_write_int32 (&w, 50);
    tl_write_int32 (&w, 2);                        /* dc_id */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 0x5678);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0,
           "document with video_thumbs iterates");
    ASSERT(mi.kind == MEDIA_DOCUMENT, "kind=document");
    ASSERT(tl_read_int32(&r) == 0x5678, "cursor past document");
    tl_writer_free(&w);
}

/* LIM-02: Document with documentAttributeSticker (inputStickerSetID). */
#define CRC_documentAttributeSticker_t  0x6319d612u
#define CRC_inputStickerSetID_t         0x9de7a269u
#define CRC_inputStickerSetEmpty_t      0xffb62b95u
#define CRC_maskCoords_t                0xaed6dbb2u
static void test_skip_media_document_sticker_attr(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, 0);                        /* no thumbs */
    tl_write_int64 (&w, 1);
    tl_write_int64 (&w, 2);
    uint8_t fr[1] = { 0 };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "image/webp");
    tl_write_int64 (&w, 5000);
    tl_write_int32 (&w, 2);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_documentAttributeSticker_t);
    tl_write_uint32(&w, 0);                        /* no mask, no mask_coords */
    tl_write_string(&w, "🦊");                     /* alt */
    tl_write_uint32(&w, CRC_inputStickerSetID_t);
    tl_write_int64 (&w, 42);
    tl_write_int64 (&w, 43);
    tl_write_int32 (&w, 0x9ABC);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0,
           "document with Sticker attr iterates");
    ASSERT(mi.kind == MEDIA_DOCUMENT, "kind=document");
    ASSERT(tl_read_int32(&r) == 0x9ABC, "cursor past document");
    tl_writer_free(&w);
}

/* LIM-02: Document with documentAttributeSticker + mask_coords. */
static void test_skip_media_document_sticker_with_mask(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 1);
    tl_write_int64 (&w, 2);
    uint8_t fr[1] = { 0 };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "image/webp");
    tl_write_int64 (&w, 5000);
    tl_write_int32 (&w, 2);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_documentAttributeSticker_t);
    tl_write_uint32(&w, (1u << 0));                /* mask_coords present */
    tl_write_string(&w, "😀");
    tl_write_uint32(&w, CRC_inputStickerSetEmpty_t);
    tl_write_uint32(&w, CRC_maskCoords_t);
    tl_write_int32 (&w, 1);                        /* n */
    tl_write_double(&w, 0.5);                      /* x */
    tl_write_double(&w, 0.25);                     /* y */
    tl_write_double(&w, 1.5);                      /* zoom */
    tl_write_int32 (&w, 0xDEF0);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "sticker + mask_coords iterates");
    ASSERT(tl_read_int32(&r) == 0xDEF0, "cursor past document");
    tl_writer_free(&w);
}

/* LIM-02: Document with documentAttributeCustomEmoji. */
#define CRC_documentAttributeCustomEmoji_t  0xfd149899u
#define CRC_inputStickerSetShortName_t      0x861cc8a0u
static void test_skip_media_document_custom_emoji_attr(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaDocument_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, CRC_document_t);
    tl_write_uint32(&w, 0);
    tl_write_int64 (&w, 11);
    tl_write_int64 (&w, 22);
    uint8_t fr[1] = { 0 };
    tl_write_bytes (&w, fr, sizeof(fr));
    tl_write_int32 (&w, 1700000000);
    tl_write_string(&w, "image/webp");
    tl_write_int64 (&w, 123);
    tl_write_int32 (&w, 2);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_documentAttributeCustomEmoji_t);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "⚡");
    tl_write_uint32(&w, CRC_inputStickerSetShortName_t);
    tl_write_string(&w, "animated_pack");
    tl_write_int32 (&w, 0xFACE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "document with CustomEmoji attr iterates");
    ASSERT(tl_read_int32(&r) == 0xFACE, "cursor past document");
    tl_writer_free(&w);
}

/* ---- MessageMediaWebPage ---- */

#define CRC_messageMediaWebPage_test 0xddf8c26eu
#define CRC_webPage_test             0xe89c45b2u
#define CRC_webPageEmpty_test        0xeb1477e8u
#define CRC_webPagePending_test      0xb0d13e47u

static void test_skip_media_webpage_empty(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);                     /* outer flags */
    tl_write_uint32(&w, CRC_webPageEmpty_test);
    tl_write_uint32(&w, (1u << 0));             /* url flag */
    tl_write_int64 (&w, 0x123LL);
    tl_write_string(&w, "https://example.com");
    tl_write_int32 (&w, 42);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "webpage empty ok");
    ASSERT(mi.kind == MEDIA_WEBPAGE, "kind == webpage");
    ASSERT(tl_read_int32(&r) == 42, "cursor past webpage");
    tl_writer_free(&w);
}

static void test_skip_media_webpage_rich(void) {
    /* webPage with url + display_url + hash + site_name + title. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);                     /* outer flags */
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 1) | (1u << 2)); /* site_name + title */
    tl_write_int64 (&w, 0xABC);
    tl_write_string(&w, "https://example.com/page");
    tl_write_string(&w, "example.com/page");
    tl_write_int32 (&w, 12345);                 /* hash */
    tl_write_string(&w, "Example");             /* site_name */
    tl_write_string(&w, "A Title");             /* title */
    tl_write_int32 (&w, 0xdead);                /* trailer */
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "webpage rich ok");
    ASSERT(mi.kind == MEDIA_WEBPAGE, "kind == webpage");
    ASSERT(tl_read_int32(&r) == 0xdead, "cursor past webpage");
    tl_writer_free(&w);
}

static void test_skip_media_webpage_pending(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPagePending_test);
    tl_write_uint32(&w, (1u << 0));             /* url */
    tl_write_int64 (&w, 0x999);
    tl_write_string(&w, "https://pending.example");
    tl_write_int32 (&w, 1700000000);
    tl_write_int32 (&w, 0xCAFE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "webpage pending ok");
    ASSERT(tl_read_int32(&r) == 0xCAFE, "cursor past pending");
    tl_writer_free(&w);
}

/* TUI-11: webPage with cached_page set, Page contains two simple
 * PageBlock rows (title + paragraph). Skipper must iterate past them. */
#define CRC_page_test                0x98657f0du
#define CRC_pageBlockTitle_test      0x70abc3fdu
#define CRC_pageBlockParagraph_test  0x467a0766u
#define CRC_pageBlockDivider_test    0xdb20b188u
#define CRC_pageBlockAnchor_test     0xce0d37b0u
#define CRC_pageBlockAuthorDate_t    0xbaafe5e0u
#define CRC_textPlain_test           0x744694e0u
#define CRC_textEmpty_test           0xdc3d824fu
#define CRC_textBold_test            0x6724abc4u
#define CRC_textConcat_test          0x7e6260d7u

static void test_skip_media_webpage_cached_page_simple(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 10));            /* cached_page flag */
    tl_write_int64 (&w, 0x111);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);                     /* hash */
    /* cached_page body — page#98657f0d. */
    tl_write_uint32(&w, CRC_page_test);
    tl_write_uint32(&w, 0);                     /* page flags */
    tl_write_string(&w, "https://x/a");         /* url */
    /* blocks: [Title(plain), Paragraph(bold(plain)), Divider, Anchor] */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 4);
    /* block 0: Title(textPlain) */
    tl_write_uint32(&w, CRC_pageBlockTitle_test);
    tl_write_uint32(&w, CRC_textPlain_test);
    tl_write_string(&w, "The Article");
    /* block 1: Paragraph(textBold(textPlain)) */
    tl_write_uint32(&w, CRC_pageBlockParagraph_test);
    tl_write_uint32(&w, CRC_textBold_test);
    tl_write_uint32(&w, CRC_textPlain_test);
    tl_write_string(&w, "Body");
    /* block 2: Divider */
    tl_write_uint32(&w, CRC_pageBlockDivider_test);
    /* block 3: Anchor */
    tl_write_uint32(&w, CRC_pageBlockAnchor_test);
    tl_write_string(&w, "section-1");
    /* photos: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* documents: empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* sentinel past the whole media */
    tl_write_int32(&w, 0xFACE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "cached_page iterates");
    ASSERT(mi.kind == MEDIA_WEBPAGE, "kind=webpage");
    ASSERT(tl_read_int32(&r) == 0xFACE, "cursor past whole media");
    tl_writer_free(&w);
}

/* TUI-11: same but with textConcat (vector of RichTexts). */
static void test_skip_media_webpage_cached_page_textconcat(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 0x222);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, CRC_page_test);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "x");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    /* block: Paragraph(textConcat([textPlain, textEmpty])) */
    tl_write_uint32(&w, CRC_pageBlockParagraph_test);
    tl_write_uint32(&w, CRC_textConcat_test);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, CRC_textPlain_test);
    tl_write_string(&w, "foo");
    tl_write_uint32(&w, CRC_textEmpty_test);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);  /* photos */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);  /* documents */
    tl_write_int32(&w, 0xDEAD);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "textConcat iterates");
    ASSERT(tl_read_int32(&r) == 0xDEAD, "cursor past media");
    tl_writer_free(&w);
}

/* TUI-11: cached_page with an unsupported PageBlock (Cover etc.) bails. */
static void test_skip_media_webpage_cached_page_unsupported(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 10));
    tl_write_int64 (&w, 0x333);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, CRC_page_test);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "x");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    /* unknown PageBlock CRC */
    tl_write_uint32(&w, 0xDEADBEEFu);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1,
           "unknown PageBlock bails");
    tl_writer_free(&w);
}

/* TUI-11: webPageAttribute — Vector with a theme (no settings). */
#define CRC_webPageAttributeTheme_t      0x54b56617u
#define CRC_webPageAttributeStickerSet_t 0x50cc03d3u
#define CRC_webPageAttributeStory_t      0x2e94c3e7u
#define CRC_storyItemDeleted_t           0x51e6ee4fu

static void test_skip_media_webpage_attributes_theme(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 12));             /* attributes flag */
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    /* attributes: [Theme{documents=[]}] */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_webPageAttributeTheme_t);
    tl_write_uint32(&w, (1u << 0));              /* documents present */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    tl_write_int32(&w, 0xBEEF);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "theme attrs iterate");
    ASSERT(tl_read_int32(&r) == 0xBEEF, "cursor past media");
    tl_writer_free(&w);
}

/* TUI-11: webPageAttribute — StickerSet with a couple of documents. */
static void test_skip_media_webpage_attributes_stickerset(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 12));
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_webPageAttributeStickerSet_t);
    tl_write_uint32(&w, 0);                      /* flags */
    /* stickers: [documentEmpty, documentEmpty] */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, CRC_documentEmpty);
    tl_write_int64 (&w, 11LL);
    tl_write_uint32(&w, CRC_documentEmpty);
    tl_write_int64 (&w, 22LL);
    tl_write_int32(&w, 0xABCD);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "stickerset iterates");
    ASSERT(tl_read_int32(&r) == 0xABCD, "cursor past media");
    tl_writer_free(&w);
}

/* TUI-11: webPageAttribute — Story without a nested story (flag.0 clear). */
static void test_skip_media_webpage_attributes_story_noinline(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 12));
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_webPageAttributeStory_t);
    tl_write_uint32(&w, 0);                      /* no inline story */
    tl_write_uint32(&w, TL_peerUser);            /* peer */
    tl_write_int64 (&w, 42LL);
    tl_write_int32 (&w, 7);                      /* story id */
    tl_write_int32(&w, 0x1234);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "story attr iterates");
    ASSERT(tl_read_int32(&r) == 0x1234, "cursor past media");
    tl_writer_free(&w);
}

/* TUI-11: webPageAttribute — Story with an inline storyItemDeleted. */
static void test_skip_media_webpage_attributes_story_inline(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 12));
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_webPageAttributeStory_t);
    tl_write_uint32(&w, (1u << 0));              /* inline story */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 42LL);
    tl_write_int32 (&w, 7);
    /* story: storyItemDeleted{id=7} — simplest variant */
    tl_write_uint32(&w, CRC_storyItemDeleted_t);
    tl_write_int32 (&w, 7);
    tl_write_int32(&w, 0xBABE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "story inline iterates");
    ASSERT(tl_read_int32(&r) == 0xBABE, "cursor past media");
    tl_writer_free(&w);
}

/* TUI-11: unknown WebPageAttribute variant bails. */
static void test_skip_media_webpage_attributes_unknown(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 12));
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xDEADC0DE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1,
           "unknown WebPageAttribute bails");
    tl_writer_free(&w);
}

/* TUI-11: both cached_page AND attributes set, combined page + stickerset. */
static void test_skip_media_webpage_cached_page_plus_attributes(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaWebPage_test);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_webPage_test);
    tl_write_uint32(&w, (1u << 10) | (1u << 12));
    tl_write_int64 (&w, 1);
    tl_write_string(&w, "u");
    tl_write_string(&w, "u");
    tl_write_int32 (&w, 1);
    /* cached_page: empty page */
    tl_write_uint32(&w, CRC_page_test);
    tl_write_uint32(&w, 0);
    tl_write_string(&w, "p");
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);  /* blocks */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);  /* photos */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);  /* documents */
    /* attributes: one StickerSet with no stickers */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_webPageAttributeStickerSet_t);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0);
    tl_write_int32(&w, 0x5678);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "cached_page + attributes iterate together");
    ASSERT(tl_read_int32(&r) == 0x5678, "cursor past media");
    tl_writer_free(&w);
}

/* ---- messageMediaGame ---- */

#define CRC_messageMediaGame_t  0xfdb19008u
#define CRC_game_t              0xbdf9653bu

static void test_skip_media_game_photo_only(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGame_t);
    tl_write_uint32(&w, CRC_game_t);
    tl_write_uint32(&w, 0);                    /* game flags (no document) */
    tl_write_int64 (&w, 0x11LL);
    tl_write_int64 (&w, 0x22LL);
    tl_write_string(&w, "short");
    tl_write_string(&w, "title");
    tl_write_string(&w, "desc");
    /* photo:photoEmpty */
    tl_write_uint32(&w, CRC_photoEmpty);
    tl_write_int64 (&w, 7777LL);
    tl_write_int32 (&w, 0xCAFE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "game photo-only ok");
    ASSERT(mi.kind == MEDIA_GAME, "kind=game");
    ASSERT(tl_read_int32(&r) == 0xCAFE, "cursor past game");
    tl_writer_free(&w);
}

static void test_skip_media_game_with_document(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGame_t);
    tl_write_uint32(&w, CRC_game_t);
    tl_write_uint32(&w, (1u << 0));            /* document flag set */
    tl_write_int64 (&w, 1LL);
    tl_write_int64 (&w, 2LL);
    tl_write_string(&w, "s");
    tl_write_string(&w, "t");
    tl_write_string(&w, "d");
    tl_write_uint32(&w, CRC_photoEmpty);
    tl_write_int64 (&w, 3LL);
    /* document:documentEmpty (id only) */
    tl_write_uint32(&w, 0x36f8c871u);
    tl_write_int64 (&w, 4LL);
    tl_write_int32 (&w, 0xABC);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "game+doc ok");
    ASSERT(tl_read_int32(&r) == 0xABC, "cursor past game+doc");
    tl_writer_free(&w);
}

static void test_skip_media_game_wrong_inner_crc_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaGame_t);
    tl_write_uint32(&w, 0xdeadbeefu);          /* not a Game CRC */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1, "non-Game inner rejected");
    tl_writer_free(&w);
}

/* ---- messageMediaPaidMedia ---- */

#define CRC_messageMediaPaidMedia_t        0xa8852491u
#define CRC_messageExtendedMediaPreview_t  0xad628cc8u
#define CRC_messageExtendedMedia_t         0xee479c64u

static void test_skip_media_paid_preview_only(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPaidMedia_t);
    tl_write_int64 (&w, 500LL);                /* stars_amount */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);                    /* one extended_media */
    tl_write_uint32(&w, CRC_messageExtendedMediaPreview_t);
    tl_write_uint32(&w, 0);                    /* no flags */
    tl_write_int32 (&w, 0x55AA);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0, "paid preview ok");
    ASSERT(mi.kind == MEDIA_PAID, "kind=paid");
    ASSERT(tl_read_int32(&r) == 0x55AA, "cursor past paid");
    tl_writer_free(&w);
}

static void test_skip_media_paid_preview_with_dims_and_thumb(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPaidMedia_t);
    tl_write_int64 (&w, 1000LL);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_messageExtendedMediaPreview_t);
    tl_write_uint32(&w, (1u << 0) | (1u << 1) | (1u << 2));
    tl_write_int32 (&w, 1280); tl_write_int32(&w, 720);
    /* thumb: photoStrippedSize#e0b0bc2e type:string bytes:bytes */
    tl_write_uint32(&w, 0xe0b0bc2eu);
    tl_write_string(&w, "i");
    uint8_t stripped[3] = { 0x01, 0x02, 0x03 };
    tl_write_bytes(&w, stripped, sizeof(stripped));
    tl_write_int32 (&w, 12);                   /* video_duration */
    tl_write_int32 (&w, 0xFACE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "paid preview full ok");
    ASSERT(tl_read_int32(&r) == 0xFACE, "cursor past paid");
    tl_writer_free(&w);
}

static void test_skip_media_paid_wrapped_inner_media(void) {
    /* extended_media variant that wraps an inner MessageMedia (here
     * messageMediaEmpty). Confirms the recursive tl_skip_message_media_ex
     * dispatch path. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPaidMedia_t);
    tl_write_int64 (&w, 42LL);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);                    /* two extended_media entries */
    tl_write_uint32(&w, CRC_messageExtendedMediaPreview_t);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, CRC_messageExtendedMedia_t);
    tl_write_uint32(&w, CRC_messageMediaEmpty);
    tl_write_int32 (&w, 0xBEEF);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "paid wrapped ok");
    ASSERT(tl_read_int32(&r) == (int32_t)0xBEEF, "cursor past wrapped paid");
    tl_writer_free(&w);
}

/* ---- messageMediaInvoice with WebDocument photo + extended_media ---- */

#define CRC_webDocument_t         0x1c570ed1u
#define CRC_webDocumentNoProxy_t  0xf9c8bcc6u
#define CRC_documentAttributeFilename_test 0x15590068u

static void test_skip_media_invoice_with_webdocument_photo(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaInvoice_t);
    tl_write_uint32(&w, (1u << 0));             /* photo flag */
    tl_write_string(&w, "Pro");
    tl_write_string(&w, "Subscription");
    /* webDocument with one Filename attribute */
    tl_write_uint32(&w, CRC_webDocument_t);
    tl_write_string(&w, "https://icon.example");
    tl_write_int64 (&w, 0x12345LL);             /* access_hash */
    tl_write_int32 (&w, 4096);                  /* size */
    tl_write_string(&w, "image/png");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_documentAttributeFilename_test);
    tl_write_string(&w, "icon.png");
    /* continuing messageMediaInvoice fields */
    tl_write_string(&w, "EUR");
    tl_write_int64 (&w, 1999);
    tl_write_string(&w, "start");
    tl_write_int32 (&w, 0xBEEF);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0,
           "invoice + webDocument iterates");
    ASSERT(mi.kind == MEDIA_INVOICE, "kind=invoice");
    ASSERT(tl_read_int32(&r) == 0xBEEF, "cursor past invoice");
    tl_writer_free(&w);
}

static void test_skip_media_invoice_noproxy_webdocument(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaInvoice_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_string(&w, "T"); tl_write_string(&w, "D");
    /* webDocumentNoProxy — no access_hash */
    tl_write_uint32(&w, CRC_webDocumentNoProxy_t);
    tl_write_string(&w, "https://x");
    tl_write_int32 (&w, 512);
    tl_write_string(&w, "image/jpeg");
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);                     /* no attributes */
    tl_write_string(&w, "USD");
    tl_write_int64 (&w, 99);
    tl_write_string(&w, "s");
    tl_write_int32 (&w, 0xDEAD);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0, "noProxy variant ok");
    ASSERT(tl_read_int32(&r) == (int32_t)0xDEAD, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_invoice_extended_media_preview(void) {
    /* flags.4 (extended_media) now walks through messageExtendedMediaPreview. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaInvoice_t);
    tl_write_uint32(&w, (1u << 4));             /* extended_media flag only */
    tl_write_string(&w, "T"); tl_write_string(&w, "D");
    tl_write_string(&w, "EUR");
    tl_write_int64 (&w, 42);
    tl_write_string(&w, "s");
    /* extended_media: messageExtendedMediaPreview#ad628cc8 with no inner
     * flags — just the flags int. */
    tl_write_uint32(&w, 0xad628cc8u);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 0xBABE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "invoice.extended_media now handled");
    ASSERT(tl_read_int32(&r) == (int32_t)0xBABE, "cursor past invoice");
    tl_writer_free(&w);
}

/* ---- messageMediaStory with inline StoryItem variants ---- */

#define CRC_storyItemDeleted_t 0x51e6ee4fu
#define CRC_storyItemSkipped_t 0xffadc913u
#define CRC_storyItem_t        0x79b26a24u

static void test_skip_media_story_inline_deleted(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));             /* inline story */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 7LL);
    tl_write_int32 (&w, 11);                    /* id */
    /* storyItemDeleted */
    tl_write_uint32(&w, CRC_storyItemDeleted_t);
    tl_write_int32 (&w, 11);
    tl_write_int32 (&w, 0xFACE);
    TlReader r = tl_reader_init(w.data, w.len);
    MediaInfo mi = {0};
    ASSERT(tl_skip_message_media_ex(&r, &mi) == 0,
           "inline storyItemDeleted iterates");
    ASSERT(mi.kind == MEDIA_STORY, "kind=story");
    ASSERT(tl_read_int32(&r) == (int32_t)0xFACE, "cursor past story");
    tl_writer_free(&w);
}

static void test_skip_media_story_inline_skipped(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 7LL);
    tl_write_int32 (&w, 11);
    /* storyItemSkipped#ffadc913 flags:# id:int date:int expire_date:int */
    tl_write_uint32(&w, CRC_storyItemSkipped_t);
    tl_write_uint32(&w, 0);                     /* no close_friends flag */
    tl_write_int32 (&w, 11);
    tl_write_int32 (&w, 1700000000);
    tl_write_int32 (&w, 1700086400);
    tl_write_int32 (&w, 0xFEED);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "inline storyItemSkipped iterates");
    ASSERT(tl_read_int32(&r) == (int32_t)0xFEED, "cursor past story");
    tl_writer_free(&w);
}

static void test_skip_media_story_truncated_full_bails(void) {
    /* Full storyItem body missing: the skipper must bail cleanly rather
     * than overrun the buffer. */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 7LL);
    tl_write_int32 (&w, 11);
    tl_write_uint32(&w, CRC_storyItem_t);
    /* No body bytes — skipper should detect truncation. */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1,
           "truncated full storyItem bails");
    tl_writer_free(&w);
}

/* Full storyItem#79b26a24 minimal: only mandatory fields + empty media. */
static void test_skip_media_story_full_minimal(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));              /* inline story */
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 42LL);
    tl_write_int32 (&w, 99);                     /* outer id */
    /* storyItem with no optional flags. */
    tl_write_uint32(&w, CRC_storyItem_t);
    tl_write_uint32(&w, 0);                      /* flags */
    tl_write_int32 (&w, 99);                     /* id */
    tl_write_int32 (&w, 1700000000);             /* date */
    tl_write_int32 (&w, 1700086400);             /* expire_date */
    tl_write_uint32(&w, CRC_messageMediaEmpty);  /* media */
    tl_write_int32 (&w, 0x12345678);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "minimal full storyItem iterates");
    ASSERT(tl_read_int32(&r) == 0x12345678, "cursor past full storyItem");
    tl_writer_free(&w);
}

/* Full storyItem with caption + entities + views (flags.0|1|3). */
static void test_skip_media_story_full_with_views(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerChannel);
    tl_write_int64 (&w, 500LL);
    tl_write_int32 (&w, 1);
    /* storyItem w/ caption (flags.0) + entities (flags.1) + views (flags.3). */
    tl_write_uint32(&w, CRC_storyItem_t);
    tl_write_uint32(&w, (1u << 0) | (1u << 1) | (1u << 3));
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 1700000000);
    tl_write_int32 (&w, 1700086400);
    tl_write_string(&w, "Hello story");
    /* entities: empty vector */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* media: empty */
    tl_write_uint32(&w, CRC_messageMediaEmpty);
    /* views: storyViews flags=0, just views_count */
    tl_write_uint32(&w, 0x8d595cd6u);            /* storyViews */
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 1234);                   /* views_count */
    tl_write_int32 (&w, 0xABBA);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "full storyItem with views iterates");
    ASSERT(tl_read_int32(&r) == (int32_t)0xABBA, "cursor past");
    tl_writer_free(&w);
}

/* Full storyItem with a privacy list + a media_area. */
static void test_skip_media_story_full_with_privacy_and_area(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaStory_t);
    tl_write_uint32(&w, (1u << 0));
    tl_write_uint32(&w, TL_peerUser);
    tl_write_int64 (&w, 1LL);
    tl_write_int32 (&w, 1);
    /* flags.14 media_areas + flags.2 privacy. */
    tl_write_uint32(&w, CRC_storyItem_t);
    tl_write_uint32(&w, (1u << 14) | (1u << 2));
    tl_write_int32 (&w, 1);
    tl_write_int32 (&w, 1700000000);
    tl_write_int32 (&w, 1700086400);
    tl_write_uint32(&w, CRC_messageMediaEmpty);
    /* media_areas: [mediaAreaUrl with coordinates + url]. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0x37381085u);            /* mediaAreaUrl */
    /* mediaAreaCoordinates flags=0 + 5 doubles */
    tl_write_uint32(&w, 0x03d1ea4eu);
    tl_write_uint32(&w, 0);
    for (int i = 0; i < 5; i++) tl_write_double(&w, 0.0);
    tl_write_string(&w, "https://example.org");
    /* privacy: [allowAll, allowUsers([7,11])]. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_uint32(&w, 0x65427b82u);            /* privacyValueAllowAll */
    tl_write_uint32(&w, 0xb8905fb2u);            /* privacyValueAllowUsers */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 2);
    tl_write_int64 (&w, 7LL);
    tl_write_int64 (&w, 11LL);
    tl_write_int32 (&w, 0xC0DE);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == 0,
           "full storyItem with privacy + media_area iterates");
    ASSERT(tl_read_int32(&r) == (int32_t)0xC0DE, "cursor past");
    tl_writer_free(&w);
}

static void test_skip_media_paid_unknown_variant_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messageMediaPaidMedia_t);
    tl_write_int64 (&w, 1LL);
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, 0xdeadbeefu);          /* unknown ExtendedMedia */
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_message_media_ex(&r, NULL) == -1, "unknown variant bails");
    tl_writer_free(&w);
}

static void test_skip_factcheck_unknown_crc_bails(void) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xdeadbeefu);
    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_skip_factcheck(&r) == -1, "unknown crc rejected");
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
    RUN_TEST(test_extract_user_access_hash);
    RUN_TEST(test_extract_channel_access_hash);
    RUN_TEST(test_extract_channel_forbidden_access_hash);
    RUN_TEST(test_extract_chat_no_access_hash);
    RUN_TEST(test_skip_reply_markup_hide);
    RUN_TEST(test_skip_reply_markup_force_reply_with_placeholder);
    RUN_TEST(test_skip_reply_markup_inline_url_callback);
    RUN_TEST(test_skip_reply_markup_unknown_bails);
    RUN_TEST(test_skip_reply_markup_unknown_button_bails);
    RUN_TEST(test_skip_reactions_empty_results);
    RUN_TEST(test_skip_reactions_emoji_and_custom);
    RUN_TEST(test_skip_reactions_recent_reactors_bails);
    RUN_TEST(test_skip_message_replies_minimal);
    RUN_TEST(test_skip_message_replies_with_commenters);
    RUN_TEST(test_skip_message_replies_unknown_crc_bails);
    RUN_TEST(test_skip_factcheck_need_check_only);
    RUN_TEST(test_skip_factcheck_with_text);
    RUN_TEST(test_skip_factcheck_unknown_crc_bails);
    RUN_TEST(test_skip_media_webpage_empty);
    RUN_TEST(test_skip_media_webpage_rich);
    RUN_TEST(test_skip_media_webpage_pending);
    RUN_TEST(test_skip_media_webpage_cached_page_simple);
    RUN_TEST(test_skip_media_webpage_cached_page_textconcat);
    RUN_TEST(test_skip_media_webpage_cached_page_unsupported);
    RUN_TEST(test_skip_media_webpage_attributes_theme);
    RUN_TEST(test_skip_media_webpage_attributes_stickerset);
    RUN_TEST(test_skip_media_webpage_attributes_story_noinline);
    RUN_TEST(test_skip_media_webpage_attributes_story_inline);
    RUN_TEST(test_skip_media_webpage_attributes_unknown);
    RUN_TEST(test_skip_media_webpage_cached_page_plus_attributes);
    RUN_TEST(test_skip_media_document_full);
    RUN_TEST(test_skip_media_document_with_thumbs);
    RUN_TEST(test_skip_media_document_with_video_thumbs);
    RUN_TEST(test_skip_media_document_sticker_attr);
    RUN_TEST(test_skip_media_document_sticker_with_mask);
    RUN_TEST(test_skip_media_document_custom_emoji_attr);
    RUN_TEST(test_skip_media_poll_minimal);
    RUN_TEST(test_skip_media_poll_with_results);
    RUN_TEST(test_skip_media_invoice_minimal);
    RUN_TEST(test_skip_media_invoice_photo_bails);
    RUN_TEST(test_skip_media_story_minimal);
    RUN_TEST(test_skip_media_giveaway_minimal);
    RUN_TEST(test_skip_media_giveaway_countries_prize);
    RUN_TEST(test_skip_media_game_photo_only);
    RUN_TEST(test_skip_media_game_with_document);
    RUN_TEST(test_skip_media_game_wrong_inner_crc_bails);
    RUN_TEST(test_skip_media_paid_preview_only);
    RUN_TEST(test_skip_media_paid_preview_with_dims_and_thumb);
    RUN_TEST(test_skip_media_paid_wrapped_inner_media);
    RUN_TEST(test_skip_media_paid_unknown_variant_bails);
    RUN_TEST(test_skip_media_invoice_with_webdocument_photo);
    RUN_TEST(test_skip_media_invoice_noproxy_webdocument);
    RUN_TEST(test_skip_media_invoice_extended_media_preview);
    RUN_TEST(test_skip_media_story_inline_deleted);
    RUN_TEST(test_skip_media_story_inline_skipped);
    RUN_TEST(test_skip_media_story_truncated_full_bails);
    RUN_TEST(test_skip_media_story_full_minimal);
    RUN_TEST(test_skip_media_story_full_with_views);
    RUN_TEST(test_skip_media_story_full_with_privacy_and_area);
}
