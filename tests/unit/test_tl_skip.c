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
}
