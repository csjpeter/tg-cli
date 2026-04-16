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
