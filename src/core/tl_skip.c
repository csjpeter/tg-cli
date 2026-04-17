/**
 * @file tl_skip.c
 * @brief TL object skippers for vector iteration.
 */

#include "tl_skip.h"
#include "tl_registry.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

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

/* PhotoSize variants (layer 170+). */
#define CRC_photoSizeEmpty              0x0e17e23cu
#define CRC_photoSize                   0x75c78e60u
#define CRC_photoCachedSize             0x021e1ad6u
#define CRC_photoStrippedSize           0xe0b0bc2eu
#define CRC_photoSizeProgressive        0xfa3efb95u
#define CRC_photoPathSize               0xd8214d41u

/* Photo variants. */
#define CRC_photo                       0xfb197a65u
#define CRC_photoEmpty                  0x2331b22du

/* Document variants. */
#define CRC_document                    0x8fd4c4d8u
#define CRC_documentEmpty               0x36f8c871u

/* DocumentAttribute — present inside document.attributes. We don't walk
 * them; we just skip via Vector count by bailing when document is set. */

/* GeoPoint. */
#define CRC_geoPointEmpty               0x1117dd5fu
#define CRC_geoPoint                    0xb2a2f663u

/* MessageMedia variants. */
#define CRC_messageMediaEmpty           0x3ded6320u
#define CRC_messageMediaPhoto           0x695150d7u
#define CRC_messageMediaDocument        0x4cf4d72du
#define CRC_messageMediaGeo             0x56e0d474u
#define CRC_messageMediaContact         0x70322949u
#define CRC_messageMediaUnsupported     0x9f84f49eu
#define CRC_messageMediaVenue           0x2ec0533fu
#define CRC_messageMediaGeoLive         0xb940c666u
#define CRC_messageMediaDice            0x3f7ee58bu

/* ChatPhoto. */
#define CRC_chatPhotoEmpty              0x37c1011cu
#define CRC_chatPhoto                   0x1c6e1c11u

/* UserProfilePhoto. */
#define CRC_userProfilePhotoEmpty       0x4f11bae1u
#define CRC_userProfilePhoto            0x82d1f706u

/* UserStatus. */
#define CRC_userStatusEmpty             0x09d05049u
#define CRC_userStatusOnline            0xedb93949u
#define CRC_userStatusOffline           0x008c703fu
#define CRC_userStatusRecently          0x7b197dc8u
#define CRC_userStatusLastWeek          0x541a1d1au
#define CRC_userStatusLastMonth         0x65899e67u

/* RestrictionReason. */
#define CRC_restrictionReason           0xd072acb4u

/* Username. */
#define CRC_username                    0xb4073647u

/* PeerColor. */
#define CRC_peerColor                   0xb54b5acfu

/* EmojiStatus. */
#define CRC_emojiStatusEmpty            0x2de11aaeu
#define CRC_emojiStatus                 0x929b619du
#define CRC_emojiStatusUntil            0xfa30a8c7u

/* ChatAdminRights / ChatBannedRights. */
#define CRC_chatAdminRights             0x5fb224d5u
#define CRC_chatBannedRights            0x9f120418u

/* ReplyMarkup variants. */
#define CRC_replyKeyboardHide           0xa03e5b85u
#define CRC_replyKeyboardForceReply     0x86b40b08u
#define CRC_replyKeyboardMarkup         0x85dd99d1u
#define CRC_replyInlineMarkup           0x48a30254u

/* KeyboardButtonRow. */
#define CRC_keyboardButtonRow           0x77608b83u

/* KeyboardButton variants we handle inline. */
#define CRC_keyboardButton              0xa2fa4880u
#define CRC_keyboardButtonUrl           0x258aff05u
#define CRC_keyboardButtonCallback      0x35bbdb6bu
#define CRC_keyboardButtonRequestPhone  0xb16a6c29u
#define CRC_keyboardButtonRequestGeoLoc 0xfc796b3fu
#define CRC_keyboardButtonSwitchInline  0x93b9fbb5u
#define CRC_keyboardButtonGame          0x50f41ccfu
#define CRC_keyboardButtonBuy           0xafd93fbbu
#define CRC_keyboardButtonUrlAuth       0x10b78d29u
#define CRC_keyboardButtonRequestPoll   0xbbc7515du
#define CRC_keyboardButtonUserProfile   0x308660c1u
#define CRC_keyboardButtonWebView       0x13767230u
#define CRC_keyboardButtonSimpleWebView 0xa0c0505cu

/* MessageReactions + Reaction variants. */
#define CRC_messageReactions            0x4f2b9479u
#define CRC_reactionCount               0xa3d1cb80u
#define CRC_reactionEmpty               0x79f5d419u
#define CRC_reactionEmoji               0x1b2286b8u
#define CRC_reactionCustomEmoji         0x8935fc73u
#define CRC_reactionPaid                0x523da4ebu

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

/* ---- ReplyMarkup skipper ---- */

/* Skip a KeyboardButton. Returns -1 on unknown variant. */
static int skip_keyboard_button(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_keyboardButton:
    case CRC_keyboardButtonRequestPhone:
    case CRC_keyboardButtonRequestGeoLoc:
    case CRC_keyboardButtonGame:
    case CRC_keyboardButtonBuy:
        return tl_skip_string(r);                 /* text */
    case CRC_keyboardButtonUrl: {
        if (tl_skip_string(r) != 0) return -1;    /* text */
        return tl_skip_string(r);                 /* url */
    }
    case CRC_keyboardButtonCallback: {
        if (r->len - r->pos < 4) return -1;
        tl_read_uint32(r);                        /* flags */
        if (tl_skip_string(r) != 0) return -1;    /* text */
        return tl_skip_string(r);                 /* data:bytes */
    }
    case CRC_keyboardButtonSwitchInline: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_string(r) != 0) return -1;    /* text */
        if (tl_skip_string(r) != 0) return -1;    /* query */
        if (flags & (1u << 1)) {
            /* peer_types:Vector<InlineQueryPeerType> — unknown nested
             * variants; bail. */
            logger_log(LOG_WARN,
                       "skip_keyboard_button: peer_types on switchInline");
            return -1;
        }
        return 0;
    }
    case CRC_keyboardButtonUrlAuth: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_string(r) != 0) return -1;    /* text */
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;/* fwd_text */
        if (tl_skip_string(r) != 0) return -1;    /* url */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                         /* button_id */
        return 0;
    }
    case CRC_keyboardButtonRequestPoll: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & (1u << 0)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_uint32(r);                    /* Bool (crc only) */
        }
        return tl_skip_string(r);                 /* text */
    }
    case CRC_keyboardButtonUserProfile: {
        if (tl_skip_string(r) != 0) return -1;    /* text */
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                         /* user_id */
        return 0;
    }
    case CRC_keyboardButtonWebView:
    case CRC_keyboardButtonSimpleWebView: {
        if (tl_skip_string(r) != 0) return -1;    /* text */
        return tl_skip_string(r);                 /* url */
    }
    default:
        logger_log(LOG_WARN, "skip_keyboard_button: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a KeyboardButtonRow = crc + Vector<KeyboardButton>. */
static int skip_keyboard_button_row(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_keyboardButtonRow) {
        logger_log(LOG_WARN,
                   "skip_keyboard_button_row: expected 0x77608b83 got 0x%08x",
                   crc);
        return -1;
    }
    if (r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_keyboard_button(r) != 0) return -1;
    }
    return 0;
}

/* Skip a Vector<KeyboardButtonRow>. */
static int skip_button_row_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_keyboard_button_row(r) != 0) return -1;
    }
    return 0;
}

int tl_skip_reply_markup(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_replyKeyboardHide:
    case CRC_replyKeyboardForceReply: {
        /* Body: flags:# (+ optional placeholder:flags.3?string for
         * forceReply). */
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (crc == CRC_replyKeyboardForceReply && (flags & (1u << 3))) {
            if (tl_skip_string(r) != 0) return -1; /* placeholder */
        }
        return 0;
    }
    case CRC_replyKeyboardMarkup: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (skip_button_row_vector(r) != 0) return -1;
        if (flags & (1u << 3)) {
            if (tl_skip_string(r) != 0) return -1; /* placeholder */
        }
        return 0;
    }
    case CRC_replyInlineMarkup:
        return skip_button_row_vector(r);
    default:
        logger_log(LOG_WARN, "tl_skip_reply_markup: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- MessageReactions skipper ---- */

/* Skip a Reaction variant. */
static int skip_reaction(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_reactionEmpty:
    case CRC_reactionPaid:
        return 0;
    case CRC_reactionEmoji:
        return tl_skip_string(r);
    case CRC_reactionCustomEmoji:
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                       /* document_id */
        return 0;
    default:
        logger_log(LOG_WARN, "skip_reaction: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a ReactionCount#a3d1cb80: flags + (chosen_order) + Reaction + count. */
static int skip_reaction_count(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_reactionCount) {
        logger_log(LOG_WARN, "skip_reaction_count: expected 0xa3d1cb80 got 0x%08x",
                   crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 0)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                       /* chosen_order */
    }
    if (skip_reaction(r) != 0) return -1;
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);                           /* count */
    return 0;
}

int tl_skip_message_reactions(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_messageReactions) {
        logger_log(LOG_WARN, "tl_skip_message_reactions: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    /* results:Vector<ReactionCount> — required. */
    if (r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_reaction_count(r) != 0) return -1;
    }
    /* recent_reactions:flags.1?Vector<MessagePeerReaction>  — BAIL */
    /* top_reactors:flags.2?Vector<MessagePeerVote>           — BAIL */
    if (flags & ((1u << 1) | (1u << 2))) {
        logger_log(LOG_WARN,
                   "tl_skip_message_reactions: recent/top reactors present "
                   "(flags=0x%08x) — not supported", flags);
        return -1;
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

/* ---- PhotoSize skipper ----
 * Variants:
 *   photoSizeEmpty#0e17e23c type:string
 *   photoSize#75c78e60 type:string w:int h:int size:int
 *   photoCachedSize#021e1ad6 type:string w:int h:int bytes:bytes
 *   photoStrippedSize#e0b0bc2e type:string bytes:bytes
 *   photoSizeProgressive#fa3efb95 type:string w:int h:int sizes:Vector<int>
 *   photoPathSize#d8214d41 type:string bytes:bytes
 */
int tl_skip_photo_size(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_photoSizeEmpty:
        return tl_skip_string(r);
    case CRC_photoSize:
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 12) return -1;
        tl_read_int32(r); tl_read_int32(r); tl_read_int32(r);
        return 0;
    case CRC_photoCachedSize:
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);
        return tl_skip_string(r); /* bytes */
    case CRC_photoStrippedSize:
    case CRC_photoPathSize:
        if (tl_skip_string(r) != 0) return -1;
        return tl_skip_string(r); /* bytes */
    case CRC_photoSizeProgressive: {
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);
        /* sizes:Vector<int> */
        if (r->len - r->pos < 8) return -1;
        uint32_t vec_crc = tl_read_uint32(r);
        if (vec_crc != TL_vector) return -1;
        uint32_t count = tl_read_uint32(r);
        if (r->len - r->pos < count * 4ULL) return -1;
        for (uint32_t i = 0; i < count; i++) tl_read_int32(r);
        return 0;
    }
    default:
        logger_log(LOG_WARN, "tl_skip_photo_size: unknown 0x%08x", crc);
        return -1;
    }
}

int tl_skip_photo_size_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t count = tl_read_uint32(r);
    for (uint32_t i = 0; i < count; i++) {
        if (tl_skip_photo_size(r) != 0) return -1;
    }
    return 0;
}

/* ---- Photo skipper / extractor ----
 * photoEmpty#2331b22d id:long
 * photo#fb197a65 flags:# has_stickers:flags.0?true id:long access_hash:long
 *                file_reference:bytes date:int
 *                sizes:Vector<PhotoSize>
 *                video_sizes:flags.1?Vector<VideoSize>     <- BAIL
 *                dc_id:int
 *
 * photo_full walks the full object and, when @p out is non-NULL, fills
 * access_hash, file_reference, dc_id and the largest PhotoSize.type.
 * Used by tl_skip_message_media_ex for MEDIA_PHOTO + file-download
 * support. tl_skip_photo is a thin wrapper that passes NULL.
 */

/* Walk a Vector<PhotoSize> and record the `type` of the largest entry
 * (by pixel dimensions for photoSize / photoSizeProgressive, by byte
 * count as a tie-breaker for cached/stripped forms). Leaves the cursor
 * past the vector. */
static int walk_photo_size_vector(TlReader *r,
                                    char *best_type, size_t best_type_cap) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t count = tl_read_uint32(r);

    long best_score = -1;
    if (best_type && best_type_cap) best_type[0] = '\0';

    for (uint32_t i = 0; i < count; i++) {
        if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
        uint32_t crc = tl_read_uint32(r);

        /* Capture type:string into a small buffer we may keep as best. */
        char type_buf[8] = {0};
        size_t type_n = 0;
        {
            size_t before = r->pos;
            char *s = tl_read_string(r);
            if (!s) return -1;
            type_n = strlen(s);
            if (type_n >= sizeof(type_buf)) type_n = sizeof(type_buf) - 1;
            memcpy(type_buf, s, type_n);
            type_buf[type_n] = '\0';
            free(s);
            (void)before;
        }

        long score = 0;
        switch (crc) {
        case CRC_photoSizeEmpty:
            score = -1; /* empty — never best */
            break;
        case CRC_photoSize: {
            if (r->len - r->pos < 12) return -1;
            int32_t w = tl_read_int32(r);
            int32_t h = tl_read_int32(r);
            int32_t sz = tl_read_int32(r); (void)sz;
            score = (long)w * (long)h;
            break;
        }
        case CRC_photoCachedSize: {
            if (r->len - r->pos < 8) return -1;
            int32_t w = tl_read_int32(r);
            int32_t h = tl_read_int32(r);
            if (tl_skip_string(r) != 0) return -1; /* bytes */
            score = (long)w * (long)h;
            break;
        }
        case CRC_photoStrippedSize:
        case CRC_photoPathSize:
            if (tl_skip_string(r) != 0) return -1; /* bytes */
            score = 0;
            break;
        case CRC_photoSizeProgressive: {
            if (r->len - r->pos < 8) return -1;
            int32_t w = tl_read_int32(r);
            int32_t h = tl_read_int32(r);
            if (r->len - r->pos < 8) return -1;
            uint32_t vc = tl_read_uint32(r);
            if (vc != TL_vector) return -1;
            uint32_t nvals = tl_read_uint32(r);
            if (r->len - r->pos < nvals * 4ULL) return -1;
            for (uint32_t j = 0; j < nvals; j++) tl_read_int32(r);
            score = (long)w * (long)h;
            break;
        }
        default:
            logger_log(LOG_WARN, "walk_photo_size_vector: unknown 0x%08x", crc);
            return -1;
        }

        if (score > best_score && best_type && best_type_cap) {
            best_score = score;
            size_t n = type_n < best_type_cap - 1 ? type_n : best_type_cap - 1;
            memcpy(best_type, type_buf, n);
            best_type[n] = '\0';
        }
    }
    return 0;
}

static int photo_full(TlReader *r, MediaInfo *out) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_photoEmpty) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->photo_id = id;
        return 0;
    }
    if (crc != CRC_photo) {
        logger_log(LOG_WARN, "tl_skip_photo: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (r->len - r->pos < 16) return -1;
    int64_t id = tl_read_int64(r);
    int64_t access = tl_read_int64(r);
    if (out) { out->photo_id = id; out->access_hash = access; }

    /* file_reference is a TL bytes field. Capture it into MediaInfo
     * (truncating if it exceeds MEDIA_FILE_REF_MAX) while advancing. */
    size_t fr_len = 0;
    uint8_t *fr = tl_read_bytes(r, &fr_len);
    if (!fr && fr_len != 0) return -1;
    if (out) {
        size_t n = fr_len;
        if (n > MEDIA_FILE_REF_MAX) n = MEDIA_FILE_REF_MAX;
        if (fr) memcpy(out->file_reference, fr, n);
        out->file_reference_len = n;
    }
    free(fr);

    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r); /* date */

    if (walk_photo_size_vector(r,
                                out ? out->thumb_type : NULL,
                                out ? sizeof(out->thumb_type) : 0) != 0)
        return -1;

    if (flags & (1u << 1)) {
        logger_log(LOG_WARN, "tl_skip_photo: video_sizes not implemented");
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    int32_t dc = tl_read_int32(r);
    if (out) out->dc_id = dc;
    return 0;
}

int tl_skip_photo(TlReader *r) {
    return photo_full(r, NULL);
}

/* ---- Document skipper ----
 * documentEmpty#36f8c871 id:long
 * document — flag-heavy; bail out without implementing for now. The layer-
 * specific layout changes frequently, and ordinary chat messages that
 * reach us typically have Photo, not Document. Callers treating a
 * Document as complex+stop-iter is acceptable for v1.
 */
static int document_inner(TlReader *r, int64_t *id_out) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_documentEmpty) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (id_out) *id_out = id;
        return 0;
    }
    if (crc == CRC_document) {
        logger_log(LOG_WARN, "tl_skip_document: non-empty Document not implemented");
        return -1;
    }
    logger_log(LOG_WARN, "tl_skip_document: unknown 0x%08x", crc);
    return -1;
}

int tl_skip_document(TlReader *r) {
    return document_inner(r, NULL);
}

/* Skip GeoPoint: empty → CRC-only; geoPoint → CRC + long lat + long long +
 * long access_hash + flags:# + optional accuracy_radius:flags.0?int.
 * Actually:
 *   geoPointEmpty#1117dd5f
 *   geoPoint#b2a2f663 flags:# long:double lat:double access_hash:long
 *                    accuracy_radius:flags.0?int
 */
static int skip_geo_point(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_geoPointEmpty) return 0;
    if (crc != CRC_geoPoint) {
        logger_log(LOG_WARN, "skip_geo_point: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 28) return -1;
    uint32_t flags = tl_read_uint32(r);
    tl_read_double(r); tl_read_double(r); tl_read_int64(r);
    if (flags & 1u) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    return 0;
}

/* ---- MessageMedia skipper ----
 * Common variants supported; unusual ones (Poll, Story, Game, Invoice,
 * Giveaway, PaidMedia, WebPage, ...) bail with -1.
 */
int tl_skip_message_media_ex(TlReader *r, MediaInfo *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_messageMediaEmpty:
        if (out) out->kind = MEDIA_EMPTY;
        return 0;
    case CRC_messageMediaUnsupported:
        if (out) out->kind = MEDIA_UNSUPPORTED;
        return 0;

    case CRC_messageMediaGeo:
        if (out) out->kind = MEDIA_GEO;
        return skip_geo_point(r);

    case CRC_messageMediaContact:
        if (out) out->kind = MEDIA_CONTACT;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);
        return 0;

    case CRC_messageMediaVenue:
        if (out) out->kind = MEDIA_VENUE;
        if (skip_geo_point(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        return 0;

    case CRC_messageMediaGeoLive: {
        if (out) out->kind = MEDIA_GEO_LIVE;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (skip_geo_point(r) != 0) return -1;
        if (flags & 1u) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
        if (flags & (1u << 1)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
        return 0;
    }

    case CRC_messageMediaDice:
        if (out) out->kind = MEDIA_DICE;
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
        return tl_skip_string(r);

    case CRC_messageMediaPhoto: {
        if (out) out->kind = MEDIA_PHOTO;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & (1u << 0)) {
            if (photo_full(r, out) != 0) return -1;
        }
        if (flags & (1u << 2)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
        return 0;
    }

    case CRC_messageMediaDocument: {
        if (out) out->kind = MEDIA_DOCUMENT;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & ((1u << 5) | (1u << 9) | (1u << 10) | (1u << 11))) {
            logger_log(LOG_WARN,
                       "tl_skip_message_media: document with unsupported flags 0x%x",
                       flags);
            return -1;
        }
        if (flags & (1u << 0)) {
            int64_t id = 0;
            if (document_inner(r, &id) != 0) return -1;
            if (out) out->document_id = id;
        }
        if (flags & (1u << 2)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
        return 0;
    }

    default:
        if (out) out->kind = MEDIA_OTHER;
        logger_log(LOG_WARN, "tl_skip_message_media: unsupported 0x%08x", crc);
        return -1;
    }
}

int tl_skip_message_media(TlReader *r) {
    return tl_skip_message_media_ex(r, NULL);
}

/* ---- Chat / User helpers ---- */

/* Read a TL string into a fixed-size buffer, truncating if necessary.
 * Always NUL-terminates `out` unless out_cap == 0. Advances the cursor.
 * Returns 0 on success, -1 on reader error. */
static int read_string_into(TlReader *r, char *out, size_t out_cap) {
    char *s = tl_read_string(r);
    if (!s) {
        if (out_cap > 0) out[0] = '\0';
        return -1;
    }
    if (out_cap > 0) {
        size_t n = strlen(s);
        if (n >= out_cap) n = out_cap - 1;
        memcpy(out, s, n);
        out[n] = '\0';
    }
    free(s);
    return 0;
}

/* Append `src` to `dst` (NUL-terminated), respecting dst_cap. */
static void str_append(char *dst, size_t dst_cap, const char *src) {
    if (dst_cap == 0) return;
    size_t cur = strlen(dst);
    if (cur >= dst_cap - 1) return;
    size_t room = dst_cap - 1 - cur;
    size_t n = strlen(src);
    if (n > room) n = room;
    memcpy(dst + cur, src, n);
    dst[cur + n] = '\0';
}

/* ---- ChatPhoto ----
 * chatPhotoEmpty#37c1011c
 * chatPhoto#1c6e1c11 flags:# has_video:flags.0?true photo_id:long
 *                    stripped_thumb:flags.1?bytes dc_id:int
 */
int tl_skip_chat_photo(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_chatPhotoEmpty) return 0;
    if (crc != CRC_chatPhoto) {
        logger_log(LOG_WARN, "tl_skip_chat_photo: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    /* photo_id:long */
    if (r->len - r->pos < 8) return -1;
    tl_read_int64(r);
    if (flags & (1u << 1)) {
        if (tl_skip_string(r) != 0) return -1; /* stripped_thumb:bytes */
    }
    /* dc_id:int */
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);
    return 0;
}

/* ---- UserProfilePhoto ----
 * userProfilePhotoEmpty#4f11bae1
 * userProfilePhoto#82d1f706 flags:# has_video:flags.0?true
 *                           personal:flags.2?true photo_id:long
 *                           stripped_thumb:flags.1?bytes dc_id:int
 */
int tl_skip_user_profile_photo(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_userProfilePhotoEmpty) return 0;
    if (crc != CRC_userProfilePhoto) {
        logger_log(LOG_WARN, "tl_skip_user_profile_photo: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    /* photo_id:long */
    if (r->len - r->pos < 8) return -1;
    tl_read_int64(r);
    if (flags & (1u << 1)) {
        if (tl_skip_string(r) != 0) return -1; /* stripped_thumb:bytes */
    }
    /* dc_id:int */
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);
    return 0;
}

/* ---- UserStatus ---- */
int tl_skip_user_status(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_userStatusEmpty:
        return 0;
    case CRC_userStatusOnline:
    case CRC_userStatusOffline:
    case CRC_userStatusRecently:
    case CRC_userStatusLastWeek:
    case CRC_userStatusLastMonth:
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
        return 0;
    default:
        logger_log(LOG_WARN, "tl_skip_user_status: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- Vector<RestrictionReason> ----
 * restrictionReason#d072acb4 platform:string reason:string text:string
 */
int tl_skip_restriction_reason_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) {
        logger_log(LOG_WARN,
                   "tl_skip_restriction_reason_vector: expected vector 0x%08x",
                   vec_crc);
        return -1;
    }
    uint32_t count = tl_read_uint32(r);
    for (uint32_t i = 0; i < count; i++) {
        if (r->len - r->pos < 4) return -1;
        uint32_t crc = tl_read_uint32(r);
        if (crc != CRC_restrictionReason) {
            logger_log(LOG_WARN,
                       "tl_skip_restriction_reason_vector: bad entry 0x%08x",
                       crc);
            return -1;
        }
        if (tl_skip_string(r) != 0) return -1; /* platform */
        if (tl_skip_string(r) != 0) return -1; /* reason */
        if (tl_skip_string(r) != 0) return -1; /* text */
    }
    return 0;
}

/* ---- Vector<Username> ----
 * username#b4073647 flags:# editable:flags.0?true active:flags.1?true username:string
 */
int tl_skip_username_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) {
        logger_log(LOG_WARN,
                   "tl_skip_username_vector: expected vector 0x%08x", vec_crc);
        return -1;
    }
    uint32_t count = tl_read_uint32(r);
    for (uint32_t i = 0; i < count; i++) {
        if (r->len - r->pos < 8) return -1;
        uint32_t crc = tl_read_uint32(r);
        if (crc != CRC_username) {
            logger_log(LOG_WARN,
                       "tl_skip_username_vector: bad entry 0x%08x", crc);
            return -1;
        }
        tl_read_uint32(r); /* flags — only 'true' bits, no data */
        if (tl_skip_string(r) != 0) return -1;
    }
    return 0;
}

/* ---- PeerColor ----
 * peerColor#b54b5acf flags:# color:flags.0?int background_emoji_id:flags.1?long
 */
int tl_skip_peer_color(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_peerColor) {
        logger_log(LOG_WARN, "tl_skip_peer_color: unknown 0x%08x", crc);
        return -1;
    }
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 0)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 1)) { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    return 0;
}

/* ---- EmojiStatus ---- */
int tl_skip_emoji_status(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_emojiStatusEmpty:
        return 0;
    case CRC_emojiStatus:
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);
        return 0;
    case CRC_emojiStatusUntil:
        if (r->len - r->pos < 12) return -1;
        tl_read_int64(r); tl_read_int32(r);
        return 0;
    default:
        logger_log(LOG_WARN, "tl_skip_emoji_status: unknown 0x%08x", crc);
        return -1;
    }
}

/* ChatAdminRights#5fb224d5 flags:# — single uint32. */
static int skip_chat_admin_rights(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_chatAdminRights) {
        logger_log(LOG_WARN, "skip_chat_admin_rights: unknown 0x%08x", crc);
        return -1;
    }
    tl_read_uint32(r); /* flags */
    return 0;
}

/* ChatBannedRights#9f120418 flags:# until_date:int. */
static int skip_chat_banned_rights(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 12) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_chatBannedRights) {
        logger_log(LOG_WARN, "skip_chat_banned_rights: unknown 0x%08x", crc);
        return -1;
    }
    tl_read_uint32(r); /* flags */
    tl_read_int32(r);  /* until_date */
    return 0;
}

/* ---- Core Chat extractor ----
 *
 * Covers:
 *   chatEmpty#29562865 id:long
 *   chat#41cbf256                       (see header comment)
 *   chatForbidden#6592a1a7 id:long title:string
 *   channel#0aadfc8f                    (layer 170+)
 *   channelForbidden#17d493d5
 *
 * If `out` is non-NULL, populates (id, title). chatEmpty leaves title empty.
 */
static int extract_chat_inner(TlReader *r, ChatSummary *out) {
    if (out) { out->id = 0; out->title[0] = '\0'; }

    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_chatEmpty) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        return 0;
    }

    if (crc == TL_chatForbidden) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        if (out) {
            if (read_string_into(r, out->title, sizeof(out->title)) != 0)
                return -1;
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
        return 0;
    }

    if (crc == TL_chat) {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        /* migrated_to:flags.6?InputChannel — too complex to dispatch here. */
        if (flags & (1u << 6)) {
            logger_log(LOG_WARN,
                       "extract_chat: chat with migrated_to not supported");
            return -1;
        }
        /* id:long title:string photo:ChatPhoto */
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        if (out) {
            if (read_string_into(r, out->title, sizeof(out->title)) != 0)
                return -1;
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
        if (tl_skip_chat_photo(r) != 0) return -1;
        /* participants_count:int date:int version:int */
        if (r->len - r->pos < 12) return -1;
        tl_read_int32(r); tl_read_int32(r); tl_read_int32(r);
        if (flags & (1u << 14)) {
            if (skip_chat_admin_rights(r) != 0) return -1;
        }
        if (flags & (1u << 18)) {
            if (skip_chat_banned_rights(r) != 0) return -1;
        }
        return 0;
    }

    if (crc == TL_channelForbidden) {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (r->len - r->pos < 16) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        tl_read_int64(r); /* access_hash */
        if (out) {
            if (read_string_into(r, out->title, sizeof(out->title)) != 0)
                return -1;
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
        if (flags & (1u << 16)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* until_date */
        }
        return 0;
    }

    if (crc == TL_channel) {
        if (r->len - r->pos < 8) return -1;
        uint32_t flags  = tl_read_uint32(r);
        uint32_t flags2 = tl_read_uint32(r);
        /* Known flags2 bits: 0,4,7,8,9,10,11. Reject any others. */
        const uint32_t flags2_known =
            (1u << 0) | (1u << 4) | (1u << 7) | (1u << 8) |
            (1u << 9) | (1u << 10) | (1u << 11);
        if (flags2 & ~flags2_known) {
            logger_log(LOG_WARN,
                       "extract_chat: channel with unknown flags2 bits 0x%x",
                       flags2);
            return -1;
        }
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        if (flags & (1u << 13)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int64(r); /* access_hash */
        }
        if (out) {
            if (read_string_into(r, out->title, sizeof(out->title)) != 0)
                return -1;
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
        if (flags & (1u << 6)) {
            if (tl_skip_string(r) != 0) return -1; /* username */
        }
        if (tl_skip_chat_photo(r) != 0) return -1;
        /* date:int */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
        if (flags & (1u << 9)) {
            if (tl_skip_restriction_reason_vector(r) != 0) return -1;
        }
        if (flags & (1u << 14)) {
            if (skip_chat_admin_rights(r) != 0) return -1;
        }
        if (flags & (1u << 15)) {
            if (skip_chat_banned_rights(r) != 0) return -1;
        }
        if (flags & (1u << 18)) {
            if (skip_chat_banned_rights(r) != 0) return -1;
        }
        if (flags & (1u << 17)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* participants_count */
        }
        if (flags2 & (1u << 0)) {
            if (tl_skip_username_vector(r) != 0) return -1;
        }
        if (flags2 & (1u << 4)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* stories_max_id */
        }
        if (flags2 & (1u << 7)) {
            if (tl_skip_peer_color(r) != 0) return -1;
        }
        if (flags2 & (1u << 8)) {
            if (tl_skip_peer_color(r) != 0) return -1;
        }
        if (flags2 & (1u << 9)) {
            if (tl_skip_emoji_status(r) != 0) return -1;
        }
        if (flags2 & (1u << 10)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* level */
        }
        if (flags2 & (1u << 11)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r); /* subscription_until_date */
        }
        return 0;
    }

    logger_log(LOG_WARN, "tl_skip_chat: unknown Chat variant 0x%08x", crc);
    return -1;
}

int tl_skip_chat(TlReader *r) {
    return extract_chat_inner(r, NULL);
}

int tl_extract_chat(TlReader *r, ChatSummary *out) {
    if (!out) return extract_chat_inner(r, NULL);
    return extract_chat_inner(r, out);
}

/* ---- Core User extractor ----
 *
 * Covers:
 *   userEmpty#d3bc4b7a id:long
 *   user#3ff6ecb0 (layer 170+)
 *
 * If `out` is non-NULL, populates (id, name, username). `name` is
 * `first_name` and `last_name` joined with a single space where both are
 * present.
 */
static int extract_user_inner(TlReader *r, UserSummary *out) {
    if (out) {
        out->id = 0;
        out->name[0] = '\0';
        out->username[0] = '\0';
    }

    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_userEmpty) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->id = id;
        return 0;
    }

    if (crc != TL_user) {
        logger_log(LOG_WARN, "tl_skip_user: unknown User variant 0x%08x", crc);
        return -1;
    }

    if (r->len - r->pos < 8) return -1;
    uint32_t flags  = tl_read_uint32(r);
    uint32_t flags2 = tl_read_uint32(r);
    /* Known flags2 bits go up to 13. Reject any bits above that. */
    if (flags2 & ~((1u << 14) - 1u)) {
        logger_log(LOG_WARN,
                   "tl_skip_user: unknown flags2 bits 0x%x", flags2);
        return -1;
    }
    if (r->len - r->pos < 8) return -1;
    int64_t id = tl_read_int64(r);
    if (out) out->id = id;

    if (flags & (1u << 0)) {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r); /* access_hash */
    }
    /* first_name */
    if (flags & (1u << 1)) {
        if (out) {
            char first[96] = {0};
            if (read_string_into(r, first, sizeof(first)) != 0) return -1;
            str_append(out->name, sizeof(out->name), first);
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
    }
    /* last_name */
    if (flags & (1u << 2)) {
        if (out) {
            char last[96] = {0};
            if (read_string_into(r, last, sizeof(last)) != 0) return -1;
            if (last[0] != '\0') {
                if (out->name[0] != '\0') {
                    str_append(out->name, sizeof(out->name), " ");
                }
                str_append(out->name, sizeof(out->name), last);
            }
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
    }
    /* username */
    if (flags & (1u << 3)) {
        if (out) {
            if (read_string_into(r, out->username, sizeof(out->username)) != 0)
                return -1;
        } else {
            if (tl_skip_string(r) != 0) return -1;
        }
    }
    /* phone */
    if (flags & (1u << 4)) {
        if (tl_skip_string(r) != 0) return -1;
    }
    /* photo */
    if (flags & (1u << 5)) {
        if (tl_skip_user_profile_photo(r) != 0) return -1;
    }
    /* status */
    if (flags & (1u << 6)) {
        if (tl_skip_user_status(r) != 0) return -1;
    }
    /* bot_info_version */
    if (flags & (1u << 14)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    /* restriction_reason */
    if (flags & (1u << 18)) {
        if (tl_skip_restriction_reason_vector(r) != 0) return -1;
    }
    /* bot_inline_placeholder */
    if (flags & (1u << 19)) {
        if (tl_skip_string(r) != 0) return -1;
    }
    /* lang_code */
    if (flags & (1u << 22)) {
        if (tl_skip_string(r) != 0) return -1;
    }
    /* emoji_status */
    if (flags & (1u << 30)) {
        if (tl_skip_emoji_status(r) != 0) return -1;
    }
    /* usernames */
    if (flags2 & (1u << 0)) {
        if (tl_skip_username_vector(r) != 0) return -1;
    }
    /* stories_max_id */
    if (flags2 & (1u << 5)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    /* color */
    if (flags2 & (1u << 8)) {
        if (tl_skip_peer_color(r) != 0) return -1;
    }
    /* profile_color */
    if (flags2 & (1u << 9)) {
        if (tl_skip_peer_color(r) != 0) return -1;
    }
    /* bot_active_users */
    if (flags2 & (1u << 12)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    return 0;
}

int tl_skip_user(TlReader *r) {
    return extract_user_inner(r, NULL);
}

int tl_extract_user(TlReader *r, UserSummary *out) {
    if (!out) return extract_user_inner(r, NULL);
    return extract_user_inner(r, out);
}

/* ---- tl_skip_message ----
 * Mirrors the parser in domain/read/history.c but discards all output.
 * Returns 0 if the cursor is safely past the whole Message, -1 if we
 * had to bail (cursor possibly mid-object).
 */
int tl_skip_message(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_messageEmpty) {
        /* flags:# id:int peer_id:flags.0?Peer */
        if (r->len - r->pos < 8) return -1;
        uint32_t flags = tl_read_uint32(r);
        tl_read_int32(r); /* id */
        if (flags & 1u) {
            if (tl_skip_peer(r) != 0) return -1;
        }
        return 0;
    }

    if (crc == TL_messageService) {
        /* action-heavy; we do not implement skipping yet. */
        return -1;
    }

    if (crc != TL_message) {
        logger_log(LOG_WARN, "tl_skip_message: unknown 0x%08x", crc);
        return -1;
    }

    if (r->len - r->pos < 12) return -1;
    uint32_t flags  = tl_read_uint32(r);
    uint32_t flags2 = tl_read_uint32(r);
    tl_read_int32(r); /* id */

    if (flags & (1u << 8)) if (tl_skip_peer(r) != 0) return -1; /* from_id */
    if (tl_skip_peer(r) != 0) return -1;                         /* peer_id */
    if (flags & (1u << 28)) if (tl_skip_peer(r) != 0) return -1; /* saved_peer_id */
    if (flags & (1u << 2))  if (tl_skip_message_fwd_header(r) != 0) return -1;
    if (flags & (1u << 11)) { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    if (flags2 & (1u << 0)) { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    if (flags & (1u << 3))  if (tl_skip_message_reply_header(r) != 0) return -1;

    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r); /* date */
    if (tl_skip_string(r) != 0) return -1; /* message */

    if (flags & (1u << 9)) if (tl_skip_message_media(r) != 0) return -1;

    if (flags & (1u << 6))  if (tl_skip_reply_markup(r) != 0) return -1;
    if (flags & (1u << 7))  if (tl_skip_message_entities_vector(r) != 0) return -1;
    if (flags & (1u << 10)) { if (r->len - r->pos < 8) return -1; tl_read_int32(r); tl_read_int32(r); }

    /* replies (flags.23), restriction_reason (flags.22), factcheck
     * (flags2.3) still halt. reactions (flags.20) has a skipper. */
    if (flags & ((1u << 22) | (1u << 23))) return -1;
    if (flags2 & (1u << 3)) return -1;
    if (flags & (1u << 15)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 16)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 17)) { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    if (flags & (1u << 20)) if (tl_skip_message_reactions(r) != 0) return -1;
    if (flags & (1u << 25)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags2 & (1u << 30)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags2 & (1u << 2))  { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    return 0;
}
