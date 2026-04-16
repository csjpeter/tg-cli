/**
 * @file tl_skip.c
 * @brief TL object skippers for vector iteration.
 */

#include "tl_skip.h"
#include "tl_registry.h"
#include "logger.h"

#include <stdlib.h>

/* ---- CRCs not yet in tl_registry.h ---- */

#define CRC_peerNotifySettings          0xa83b0426u
#define CRC_notificationSoundDefault    0x97e8bebeu
#define CRC_notificationSoundNone       0x6f0c34dfu
#define CRC_notificationSoundLocal      0x830b9ae4u
#define CRC_notificationSoundRingtone   0xff6c8049u
#define CRC_draftMessage                0x3fccf7efu
#define CRC_draftMessageEmpty           0x1b0c841au

/* MessageEntity variants (layer 170+). */
#define CRC_messageEntityUnknown        0xbb92ba95u
#define CRC_messageEntityMention        0xfa04579du
#define CRC_messageEntityHashtag        0x6f635b0du
#define CRC_messageEntityBotCommand     0x6cef8ac7u
#define CRC_messageEntityUrl            0x6ed02538u
#define CRC_messageEntityEmail          0x64e475c2u
#define CRC_messageEntityBold           0xbd610bc9u
#define CRC_messageEntityItalic         0x826f8b60u
#define CRC_messageEntityCode           0x28a20571u
#define CRC_messageEntityPre            0x73924be0u
#define CRC_messageEntityTextUrl        0x76a6d327u
#define CRC_messageEntityMentionName    0xdc7b1140u
#define CRC_messageEntityPhone          0x9b69e34bu
#define CRC_messageEntityCashtag        0x4c4e743fu
#define CRC_messageEntityUnderline      0x9c4e7e8bu
#define CRC_messageEntityStrike         0xbf0693d4u
#define CRC_messageEntityBlockquote     0xf1ccaaacu
#define CRC_messageEntityBankCard       0x761e6af4u
#define CRC_messageEntitySpoiler        0x32ca960fu
#define CRC_messageEntityCustomEmoji    0xc8cf05f8u

/* MessageFwdHeader (layer 170+). */
#define CRC_messageFwdHeader            0x4e4df4bbu

/* MessageReplyHeader variants. */
#define CRC_messageReplyHeader          0xafbc09dbu
#define CRC_messageReplyStoryHeader     0xe5af939u

int tl_skip_bool(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    tl_read_uint32(r);
    return 0;
}

int tl_skip_string(TlReader *r) {
    /* tl_read_string already advances the cursor and handles padding. */
    if (!tl_reader_ok(r)) return -1;
    char *s = tl_read_string(r);
    if (!s) return -1;
    free(s);
    return 0;
}

int tl_skip_peer(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 12) return -1;
    uint32_t crc = tl_read_uint32(r);
    (void)tl_read_int64(r);
    switch (crc) {
    case TL_peerUser:
    case TL_peerChat:
    case TL_peerChannel:
        return 0;
    default:
        logger_log(LOG_WARN, "tl_skip_peer: unknown Peer 0x%08x", crc);
        return -1;
    }
}

int tl_skip_notification_sound(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_notificationSoundDefault:
    case CRC_notificationSoundNone:
        return 0;                              /* no payload */
    case CRC_notificationSoundRingtone:
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                      /* id */
        return 0;
    case CRC_notificationSoundLocal:
        if (tl_skip_string(r) != 0) return -1; /* title */
        if (tl_skip_string(r) != 0) return -1; /* data */
        return 0;
    default:
        logger_log(LOG_WARN, "tl_skip_notification_sound: unknown 0x%08x", crc);
        return -1;
    }
}

/* peerNotifySettings#a83b0426 flags:#
 *   show_previews:flags.0?Bool  silent:flags.1?Bool  mute_until:flags.2?int
 *   ios_sound:flags.3?NotificationSound
 *   android_sound:flags.4?NotificationSound
 *   other_sound:flags.5?NotificationSound
 *   stories_muted:flags.6?Bool  stories_hide_sender:flags.7?Bool
 *   stories_ios_sound:flags.8?NotificationSound
 *   stories_android_sound:flags.9?NotificationSound
 *   stories_other_sound:flags.10?NotificationSound
 */
int tl_skip_peer_notify_settings(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_peerNotifySettings) {
        logger_log(LOG_WARN,
                   "tl_skip_peer_notify_settings: unexpected 0x%08x", crc);
        return -1;
    }
    uint32_t flags = tl_read_uint32(r);

    if (flags & (1u << 0)) if (tl_skip_bool(r) != 0) return -1;
    if (flags & (1u << 1)) if (tl_skip_bool(r) != 0) return -1;
    if (flags & (1u << 2)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 3)) if (tl_skip_notification_sound(r) != 0) return -1;
    if (flags & (1u << 4)) if (tl_skip_notification_sound(r) != 0) return -1;
    if (flags & (1u << 5)) if (tl_skip_notification_sound(r) != 0) return -1;
    if (flags & (1u << 6)) if (tl_skip_bool(r) != 0) return -1;
    if (flags & (1u << 7)) if (tl_skip_bool(r) != 0) return -1;
    if (flags & (1u << 8)) if (tl_skip_notification_sound(r) != 0) return -1;
    if (flags & (1u << 9)) if (tl_skip_notification_sound(r) != 0) return -1;
    if (flags & (1u << 10)) if (tl_skip_notification_sound(r) != 0) return -1;
    return 0;
}

/* draftMessageEmpty#1b0c841a flags:# date:flags.0?int
 * draftMessage#3fccf7ef      — we do NOT fully parse this (it contains
 * InputReplyTo, Vector<MessageEntity>, InputMedia which are deep nested).
 * Return -1 so the caller stops iteration; callers wrapping dialogs can
 * skip entries with draft by first checking Dialog.flags.1.
 */
int tl_skip_draft_message(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_draftMessageEmpty) {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & 1u) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* date */
        }
        return 0;
    }
    if (crc == CRC_draftMessage) {
        logger_log(LOG_WARN,
                   "tl_skip_draft_message: non-empty draft not parseable yet");
        return -1;
    }
    logger_log(LOG_WARN, "tl_skip_draft_message: unknown 0x%08x", crc);
    return -1;
}

int tl_skip_message_entity(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);

    /* All entities start with offset:int length:int (8 bytes). */
    switch (crc) {
    case CRC_messageEntityUnknown:
    case CRC_messageEntityMention:
    case CRC_messageEntityHashtag:
    case CRC_messageEntityBotCommand:
    case CRC_messageEntityUrl:
    case CRC_messageEntityEmail:
    case CRC_messageEntityBold:
    case CRC_messageEntityItalic:
    case CRC_messageEntityCode:
    case CRC_messageEntityPhone:
    case CRC_messageEntityCashtag:
    case CRC_messageEntityUnderline:
    case CRC_messageEntityStrike:
    case CRC_messageEntityBankCard:
    case CRC_messageEntitySpoiler:
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);
        return 0;

    case CRC_messageEntityPre:
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);
        return tl_skip_string(r); /* language */

    case CRC_messageEntityTextUrl:
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);
        return tl_skip_string(r); /* url */

    case CRC_messageEntityMentionName:
        if (r->len - r->pos < 16) return -1;
        tl_read_int32(r); tl_read_int32(r);
        tl_read_int64(r); /* user_id */
        return 0;

    case CRC_messageEntityCustomEmoji:
        if (r->len - r->pos < 16) return -1;
        tl_read_int32(r); tl_read_int32(r);
        tl_read_int64(r); /* document_id */
        return 0;

    case CRC_messageEntityBlockquote:
        /* flags:# offset:int length:int — no string payload */
        if (r->len - r->pos < 12) return -1;
        tl_read_uint32(r); /* flags */
        tl_read_int32(r); tl_read_int32(r);
        return 0;

    default:
        logger_log(LOG_WARN, "tl_skip_message_entity: unknown 0x%08x", crc);
        return -1;
    }
}

int tl_skip_message_entities_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) {
        logger_log(LOG_WARN,
                   "tl_skip_message_entities_vector: expected vector 0x%08x",
                   vec_crc);
        return -1;
    }
    uint32_t count = tl_read_uint32(r);
    for (uint32_t i = 0; i < count; i++) {
        if (tl_skip_message_entity(r) != 0) return -1;
    }
    return 0;
}

/* messageFwdHeader#4e4df4bb flags:#
 *   imported:flags.7?true           (no data)
 *   saved_out:flags.11?true         (no data)
 *   from_id:flags.0?Peer
 *   from_name:flags.5?string
 *   date:int                        (always present)
 *   channel_post:flags.2?int
 *   post_author:flags.3?string
 *   saved_from_peer:flags.4?Peer
 *   saved_from_msg_id:flags.4?int
 *   saved_from_id:flags.8?Peer
 *   saved_from_name:flags.9?string
 *   saved_date:flags.10?int
 *   psa_type:flags.6?string
 */
int tl_skip_message_fwd_header(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_messageFwdHeader) {
        logger_log(LOG_WARN,
                   "tl_skip_message_fwd_header: unexpected 0x%08x", crc);
        return -1;
    }
    uint32_t flags = tl_read_uint32(r);

    if (flags & (1u << 0)) if (tl_skip_peer(r) != 0) return -1;
    if (flags & (1u << 5)) if (tl_skip_string(r) != 0) return -1;
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r); /* date */
    if (flags & (1u << 2)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 3)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 4)) {
        if (tl_skip_peer(r) != 0) return -1;
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    if (flags & (1u << 8)) if (tl_skip_peer(r) != 0) return -1;
    if (flags & (1u << 9)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 10)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 6)) if (tl_skip_string(r) != 0) return -1;
    return 0;
}

/* messageReplyHeader#afbc09db flags:#
 *   reply_to_scheduled:flags.2?true   (no data)
 *   forum_topic:flags.3?true          (no data)
 *   quote:flags.9?true                (no data)
 *   reply_to_msg_id:flags.4?int
 *   reply_to_peer_id:flags.0?Peer
 *   reply_from:flags.5?MessageFwdHeader
 *   reply_media:flags.8?MessageMedia           <- NOT IMPLEMENTED
 *   reply_to_top_id:flags.1?int
 *   quote_text:flags.6?string
 *   quote_entities:flags.7?Vector<MessageEntity>
 *   quote_offset:flags.10?int
 */
int tl_skip_message_reply_header(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_messageReplyStoryHeader) {
        /* peer_id:Peer story_id:int */
        if (tl_skip_peer(r) != 0) return -1;
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
        return 0;
    }
    if (crc != CRC_messageReplyHeader) {
        logger_log(LOG_WARN,
                   "tl_skip_message_reply_header: unexpected 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);

    if (flags & (1u << 4)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 0)) if (tl_skip_peer(r) != 0) return -1;
    if (flags & (1u << 5)) if (tl_skip_message_fwd_header(r) != 0) return -1;
    if (flags & (1u << 8)) {
        logger_log(LOG_WARN,
                   "tl_skip_message_reply_header: reply_media not implemented");
        return -1;
    }
    if (flags & (1u << 1)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 6)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 7)) if (tl_skip_message_entities_vector(r) != 0) return -1;
    if (flags & (1u << 10)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    return 0;
}
