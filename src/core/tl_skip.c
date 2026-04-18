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
#define CRC_messageMediaWebPage         0xddf8c26eu
#define CRC_messageMediaPoll            0x4bd6e798u
#define CRC_messageMediaInvoice         0xf6a548d3u
#define CRC_messageMediaStory           0x68cb6283u
#define CRC_messageMediaGiveaway        0xaa073beeu
#define CRC_messageMediaGame            0xfdb19008u
#define CRC_messageMediaPaidMedia       0xa8852491u

/* Game (inside messageMediaGame). */
#define CRC_game                        0xbdf9653bu

/* MessageExtendedMedia (inside messageMediaPaidMedia). */
#define CRC_messageExtendedMediaPreview 0xad628cc8u
#define CRC_messageExtendedMedia        0xee479c64u

/* WebDocument (inside messageMediaInvoice.photo). */
#define CRC_webDocument                 0x1c570ed1u
#define CRC_webDocumentNoProxy          0xf9c8bcc6u

/* StoryItem variants (inside messageMediaStory when flags.0). */
#define CRC_storyItemDeleted            0x51e6ee4fu
#define CRC_storyItemSkipped            0xffadc913u
#define CRC_storyItem                   0x79b26a24u

/* StoryFwdHeader (inside full storyItem.fwd_from). */
#define CRC_storyFwdHeader              0xb826e150u

/* MediaAreaCoordinates (inner of every MediaArea). */
#define CRC_mediaAreaCoordinates        0x03d1ea4eu

/* GeoPointAddress (inside mediaAreaGeoPoint when flags.0). */
#define CRC_geoPointAddress             0xde4c5d93u

/* MediaArea variants. */
#define CRC_mediaAreaVenue              0xbe82db9cu
#define CRC_mediaAreaGeoPoint           0xdf8b3b22u
#define CRC_mediaAreaSuggestedReaction  0x14455871u
#define CRC_mediaAreaChannelPost        0x770416afu
#define CRC_mediaAreaUrl                0x37381085u
#define CRC_mediaAreaWeather            0x49a6549cu
#define CRC_mediaAreaStarGift           0x5787686du

/* PrivacyRule variants. */
#define CRC_privacyValueAllowContacts          0xfffe1bacu
#define CRC_privacyValueAllowAll               0x65427b82u
#define CRC_privacyValueAllowUsers             0xb8905fb2u
#define CRC_privacyValueDisallowContacts       0xf888fa1au
#define CRC_privacyValueDisallowAll            0x8b73e763u
#define CRC_privacyValueDisallowUsers          0xe4621141u
#define CRC_privacyValueAllowChatParticipants  0x6b134e8eu
#define CRC_privacyValueDisallowChatParticipants 0x41c87565u
#define CRC_privacyValueAllowCloseFriends      0xf7e8d89bu
#define CRC_privacyValueAllowPremium           0xece9814bu
#define CRC_privacyValueAllowBots              0x21461b5du
#define CRC_privacyValueDisallowBots           0xf6a5f82fu

/* StoryViews. */
#define CRC_storyViews                  0x8d595cd6u

/* WebPage variants (inside messageMediaWebPage). */
#define CRC_webPage                     0xe89c45b2u
#define CRC_webPageEmpty                0xeb1477e8u
#define CRC_webPagePending              0xb0d13e47u
#define CRC_webPageNotModified          0x7311ca11u

/* WebPageAttribute variants (webPage.attributes, flags.12). */
#define CRC_webPageAttributeTheme       0x54b56617u
#define CRC_webPageAttributeStory       0x2e94c3e7u
#define CRC_webPageAttributeStickerSet  0x50cc03d3u

/* Page (webPage.cached_page, flags.10). */
#define CRC_page                        0x98657f0du

/* PageBlock variants — full set for cached_page iteration. */
#define CRC_pageBlockUnsupported        0x13567e8au
#define CRC_pageBlockTitle              0x70abc3fdu
#define CRC_pageBlockSubtitle           0x8ffa9a1fu
#define CRC_pageBlockHeader             0xbfd064ecu
#define CRC_pageBlockSubheader          0xf12bb6e1u
#define CRC_pageBlockKicker             0x1e148390u
#define CRC_pageBlockParagraph          0x467a0766u
#define CRC_pageBlockPreformatted       0xc070d93eu
#define CRC_pageBlockFooter             0x48870999u
#define CRC_pageBlockDivider            0xdb20b188u
#define CRC_pageBlockAnchor             0xce0d37b0u
#define CRC_pageBlockAuthorDate         0xbaafe5e0u
#define CRC_pageBlockBlockquote         0x263d7c26u
#define CRC_pageBlockPullquote          0x4f4456d5u
#define CRC_pageBlockPhoto              0x1759c560u
#define CRC_pageBlockVideo              0x7c8fe7b6u
#define CRC_pageBlockAudio              0x804361eau
#define CRC_pageBlockCover              0x39f23300u
#define CRC_pageBlockChannel            0xef1751b5u
#define CRC_pageBlockMap                0xa44f3ef6u
#define CRC_pageBlockList               0xe4e88011u
#define CRC_pageBlockOrderedList        0x9a8ae1e1u
#define CRC_pageBlockCollage            0x65a0fa4du
#define CRC_pageBlockSlideshow          0x031f9590u
#define CRC_pageBlockDetails            0x76768bedu
#define CRC_pageBlockRelatedArticles    0x16115a96u
#define CRC_pageBlockTable              0xbf4dea82u
#define CRC_pageBlockEmbed              0xa8718dc5u
#define CRC_pageBlockEmbedPost          0xf259a80bu

/* PageCaption, PageListItem, PageListOrderedItem, PageTableRow/Cell,
 * PageRelatedArticle — all reached from PageBlock variants. */
#define CRC_pageCaption                 0x6f747657u
#define CRC_pageListItemText            0xb92fb6cdu
#define CRC_pageListItemBlocks          0x25e073fcu
#define CRC_pageListOrderedItemText     0x5e068047u
#define CRC_pageListOrderedItemBlocks   0x98dd8936u
#define CRC_pageTableRow                0xe0c0c5e5u
#define CRC_pageTableCell               0x34566b6au
#define CRC_pageRelatedArticle          0xb390dc08u

/* RichText variants — full tree so every supported PageBlock can walk
 * its RichText payload. */
#define CRC_textEmpty                   0xdc3d824fu
#define CRC_textPlain                   0x744694e0u
#define CRC_textBold                    0x6724abc4u
#define CRC_textItalic                  0xd912a59cu
#define CRC_textUnderline               0xc12622c4u
#define CRC_textStrike                  0x9bf8bb95u
#define CRC_textFixed                   0x6c3f19b9u
#define CRC_textUrl                     0x3c2884c1u
#define CRC_textEmail                   0xde5a0dd6u
#define CRC_textConcat                  0x7e6260d7u
#define CRC_textSubscript               0xed6a8504u
#define CRC_textSuperscript             0xc7fb5e01u
#define CRC_textMarked                  0x034b27f6u
#define CRC_textPhone                   0x1ccb966au
#define CRC_textImage                   0x081ccf4fu
#define CRC_textAnchor                  0x35553762u

/* Poll + PollAnswer + PollResults + PollAnswerVoters. */
#define CRC_poll                        0x58747131u
#define CRC_pollAnswer                  0x6ca9c2e9u
#define CRC_pollResults                 0x7adc669du
#define CRC_pollAnswerVoters            0x3b6ddad2u
#define CRC_textWithEntities_poll       0x751f3146u

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

/* MessageReplies. */
#define CRC_messageReplies              0x83d60fc2u

/* FactCheck + TextWithEntities. */
#define CRC_factCheck                   0xb89bfccfu
#define CRC_textWithEntities            0x751f3146u

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

/* ---- MessageReplies skipper ----
 * messageReplies#83d60fc2 flags:# comments:flags.0?true
 *   replies:int replies_pts:int
 *   recent_repliers:flags.1?Vector<Peer>
 *   channel_id:flags.0?long
 *   max_id:flags.2?int  read_max_id:flags.3?int
 */
int tl_skip_message_replies(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_messageReplies) {
        logger_log(LOG_WARN, "tl_skip_message_replies: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 12) return -1;
    uint32_t flags = tl_read_uint32(r);
    tl_read_int32(r);                                 /* replies */
    tl_read_int32(r);                                 /* replies_pts */
    if (flags & (1u << 1)) {
        if (r->len - r->pos < 8) return -1;
        uint32_t vec_crc = tl_read_uint32(r);
        if (vec_crc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (tl_skip_peer(r) != 0) return -1;
        }
    }
    if (flags & (1u << 0)) {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                             /* channel_id */
    }
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                             /* max_id */
    }
    if (flags & (1u << 3)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                             /* read_max_id */
    }
    return 0;
}

/* ---- FactCheck skipper ----
 * factCheck#b89bfccf flags:#
 *   need_check:flags.0?true
 *   country:flags.1?string
 *   text:flags.1?TextWithEntities
 *   hash:long
 *
 * textWithEntities#751f3146 text:string entities:Vector<MessageEntity>
 */
int tl_skip_factcheck(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_factCheck) {
        logger_log(LOG_WARN, "tl_skip_factcheck: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 1)) {
        if (tl_skip_string(r) != 0) return -1;  /* country */
        /* text:TextWithEntities */
        if (r->len - r->pos < 4) return -1;
        uint32_t twe_crc = tl_read_uint32(r);
        if (twe_crc != CRC_textWithEntities) {
            logger_log(LOG_WARN,
                       "tl_skip_factcheck: expected textWithEntities got 0x%08x",
                       twe_crc);
            return -1;
        }
        if (tl_skip_string(r) != 0) return -1;  /* text */
        if (tl_skip_message_entities_vector(r) != 0) return -1;
    }
    if (r->len - r->pos < 8) return -1;
    tl_read_int64(r);                            /* hash */
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
/* DocumentAttribute CRCs we can skip without bailing. */
#define CRC_documentAttributeImageSize   0x6c37c15cu
#define CRC_documentAttributeAnimated    0x11b58939u
#define CRC_documentAttributeHasStickers 0x9801d2f7u
#define CRC_documentAttributeFilename    0x15590068u
#define CRC_documentAttributeVideo       0x43c57c48u
#define CRC_documentAttributeAudio       0x9852f9c6u
#define CRC_documentAttributeSticker     0x6319d612u
#define CRC_documentAttributeCustomEmoji 0xfd149899u

/* InputStickerSet variants (documentAttributeSticker / CustomEmoji). */
#define CRC_inputStickerSetEmpty                    0xffb62b95u
#define CRC_inputStickerSetID                       0x9de7a269u
#define CRC_inputStickerSetShortName                0x861cc8a0u
#define CRC_inputStickerSetAnimatedEmoji            0x028703c8u
#define CRC_inputStickerSetDice                     0xe67f520eu
#define CRC_inputStickerSetAnimatedEmojiAnimations  0x0cde3739u
#define CRC_inputStickerSetPremiumGifts             0xc88b3b02u
#define CRC_inputStickerSetEmojiGenericAnimations   0x04c4d4ceu
#define CRC_inputStickerSetEmojiDefaultStatuses     0x29d0f5eeu
#define CRC_inputStickerSetEmojiDefaultTopicIcons   0x44c1f8e9u
#define CRC_inputStickerSetEmojiChannelDefaultStatuses 0x49748553u

/* MaskCoords (documentAttributeSticker.mask_coords). */
#define CRC_maskCoords                   0xaed6dbb2u

/* VideoSize variants (document.video_thumbs, flags.1). */
#define CRC_videoSize                    0xde33b094u
#define CRC_videoSizeEmojiMarkup         0xf85c413cu
#define CRC_videoSizeStickerMarkup       0x0da082feu

/* Skip an InputStickerSet variant. Used by documentAttributeSticker /
 * CustomEmoji. Handles every declared variant; on unknown ones we bail. */
static int skip_input_sticker_set(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_inputStickerSetEmpty:
    case CRC_inputStickerSetAnimatedEmoji:
    case CRC_inputStickerSetAnimatedEmojiAnimations:
    case CRC_inputStickerSetPremiumGifts:
    case CRC_inputStickerSetEmojiGenericAnimations:
    case CRC_inputStickerSetEmojiDefaultStatuses:
    case CRC_inputStickerSetEmojiDefaultTopicIcons:
    case CRC_inputStickerSetEmojiChannelDefaultStatuses:
        return 0;
    case CRC_inputStickerSetID:
        if (r->len - r->pos < 16) return -1;
        tl_read_int64(r);                            /* id */
        tl_read_int64(r);                            /* access_hash */
        return 0;
    case CRC_inputStickerSetShortName:
        return tl_skip_string(r);
    case CRC_inputStickerSetDice:
        return tl_skip_string(r);                    /* emoticon */
    default:
        logger_log(LOG_WARN,
                   "skip_input_sticker_set: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a MaskCoords#aed6dbb2: n:int + 3 doubles. */
static int skip_mask_coords(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_maskCoords) {
        logger_log(LOG_WARN, "skip_mask_coords: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4 + 3 * 8) return -1;
    tl_read_int32(r);                                /* n */
    tl_read_double(r); tl_read_double(r); tl_read_double(r); /* x, y, zoom */
    return 0;
}

/* Skip a single VideoSize variant (document.video_thumbs element). */
static int skip_video_size(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_videoSize: {
        /* flags:# type:string w:int h:int size:int video_start_ts:flags.0?double */
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 12) return -1;
        tl_read_int32(r); tl_read_int32(r); tl_read_int32(r);
        if (flags & (1u << 0)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_double(r);
        }
        return 0;
    }
    case CRC_videoSizeEmojiMarkup: {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* emoji_id */
        /* background_colors:Vector<int> */
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        if (r->len - r->pos < (size_t)n * 4) return -1;
        for (uint32_t i = 0; i < n; i++) tl_read_int32(r);
        return 0;
    }
    case CRC_videoSizeStickerMarkup: {
        if (skip_input_sticker_set(r) != 0) return -1;
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* sticker_id */
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        if (r->len - r->pos < (size_t)n * 4) return -1;
        for (uint32_t i = 0; i < n; i++) tl_read_int32(r);
        return 0;
    }
    default:
        logger_log(LOG_WARN, "skip_video_size: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a Vector<VideoSize>. */
static int skip_video_size_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_video_size(r) != 0) return -1;
    }
    return 0;
}

/* Skip a single DocumentAttribute, optionally extracting the filename. */
static int skip_document_attribute(TlReader *r,
                                    char *filename_out, size_t fn_cap) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_documentAttributeImageSize:
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r); tl_read_int32(r);              /* w, h */
        return 0;
    case CRC_documentAttributeAnimated:
    case CRC_documentAttributeHasStickers:
        return 0;
    case CRC_documentAttributeFilename: {
        size_t before = r->pos;
        char *s = tl_read_string(r);
        if (!s) { r->pos = before; return -1; }
        if (filename_out && fn_cap) {
            size_t n = strlen(s);
            if (n >= fn_cap) n = fn_cap - 1;
            memcpy(filename_out, s, n);
            filename_out[n] = '\0';
        }
        free(s);
        return 0;
    }
    case CRC_documentAttributeVideo: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (r->len - r->pos < 8 + 4 + 4) return -1;
        tl_read_double(r);                               /* duration */
        tl_read_int32(r); tl_read_int32(r);              /* w, h */
        if (flags & (1u << 2)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                            /* preload_prefix_size */
        }
        if (flags & (1u << 4)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_double(r);                           /* video_start_ts */
        }
        if (flags & (1u << 5))
            if (tl_skip_string(r) != 0) return -1;       /* video_codec */
        return 0;
    }
    case CRC_documentAttributeAudio: {
        if (r->len - r->pos < 8) return -1;
        uint32_t flags = tl_read_uint32(r);
        tl_read_int32(r);                                /* duration */
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;       /* title */
        if (flags & (1u << 1))
            if (tl_skip_string(r) != 0) return -1;       /* performer */
        if (flags & (1u << 2))
            if (tl_skip_string(r) != 0) return -1;       /* waveform:bytes */
        return 0;
    }
    case CRC_documentAttributeSticker: {
        /* flags:# mask:flags.1?true alt:string stickerset:InputStickerSet
         *        mask_coords:flags.0?MaskCoords */
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_string(r) != 0) return -1;           /* alt */
        if (skip_input_sticker_set(r) != 0) return -1;
        if (flags & (1u << 0)) {
            if (skip_mask_coords(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_documentAttributeCustomEmoji: {
        /* flags:# free:flags.0?true text_color:flags.1?true alt:string
         *        stickerset:InputStickerSet */
        if (r->len - r->pos < 4) return -1;
        (void)tl_read_uint32(r);                         /* flags */
        if (tl_skip_string(r) != 0) return -1;           /* alt */
        return skip_input_sticker_set(r);
    }
    default:
        logger_log(LOG_WARN, "skip_document_attribute: unknown 0x%08x", crc);
        return -1;
    }
}

/* Walk a Document. When @p out is non-NULL, fills id + access_hash +
 * file_reference + dc_id + size + mime_type + filename. Returns -1 on
 * thumbs / video_thumbs (flags.0 / flags.1) or an unknown attribute. */
static int document_inner(TlReader *r, MediaInfo *out) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc == CRC_documentEmpty) {
        if (r->len - r->pos < 8) return -1;
        int64_t id = tl_read_int64(r);
        if (out) out->document_id = id;
        return 0;
    }
    if (crc != CRC_document) {
        logger_log(LOG_WARN, "tl_skip_document: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (r->len - r->pos < 16) return -1;
    int64_t id     = tl_read_int64(r);
    int64_t access = tl_read_int64(r);
    if (out) { out->document_id = id; out->access_hash = access; }

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
    tl_read_int32(r);                                    /* date */

    size_t mime_before = r->pos;
    char *mime = tl_read_string(r);
    if (!mime) { r->pos = mime_before; return -1; }
    if (out) {
        size_t n = strlen(mime);
        if (n >= sizeof(out->document_mime)) n = sizeof(out->document_mime) - 1;
        memcpy(out->document_mime, mime, n);
        out->document_mime[n] = '\0';
    }
    free(mime);

    if (r->len - r->pos < 8) return -1;
    int64_t size = tl_read_int64(r);
    if (out) out->document_size = size;

    if (flags & (1u << 0)) {
        if (tl_skip_photo_size_vector(r) != 0) return -1;
    }
    if (flags & (1u << 1)) {
        if (skip_video_size_vector(r) != 0) return -1;
    }

    if (r->len - r->pos < 4) return -1;
    int32_t dc = tl_read_int32(r);
    if (out) out->dc_id = dc;

    /* attributes:Vector<DocumentAttribute> */
    if (r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t n_attrs = tl_read_uint32(r);
    for (uint32_t i = 0; i < n_attrs; i++) {
        if (skip_document_attribute(r,
                                     out ? out->document_filename : NULL,
                                     out ? sizeof(out->document_filename) : 0) != 0)
            return -1;
    }
    return 0;
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

/* Forward decl: skip_story_item is defined later in the file (next to
 * the MessageMedia switch) and is re-used by webPageAttributeStory. */
static int skip_story_item(TlReader *r);

/* Forward decls: PageBlock recursion (Cover → PageBlock, Details →
 * Vector<PageBlock>, EmbedPost → Vector<PageBlock>, List item blocks,
 * etc.) and reliance on skip_geo_point for pageBlockMap. */
static int skip_page_block(TlReader *r);
static int skip_geo_point(TlReader *r);

/* RichText tree (used by cached_page's PageBlock variants). Recursive:
 * wrap-type variants contain a nested RichText. textConcat is a vector. */
static int skip_rich_text(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_textEmpty:
        return 0;
    case CRC_textPlain:
        return tl_skip_string(r);
    case CRC_textBold:
    case CRC_textItalic:
    case CRC_textUnderline:
    case CRC_textStrike:
    case CRC_textFixed:
    case CRC_textSubscript:
    case CRC_textSuperscript:
    case CRC_textMarked:
        return skip_rich_text(r);
    case CRC_textUrl: {
        if (skip_rich_text(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* webpage_id */
        return 0;
    }
    case CRC_textEmail:
    case CRC_textPhone: {
        if (skip_rich_text(r) != 0) return -1;
        return tl_skip_string(r);
    }
    case CRC_textConcat: {
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_rich_text(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_textImage: {
        if (r->len - r->pos < 16) return -1;
        tl_read_int64(r);                            /* document_id */
        tl_read_int32(r);                            /* w */
        tl_read_int32(r);                            /* h */
        return 0;
    }
    case CRC_textAnchor: {
        if (skip_rich_text(r) != 0) return -1;
        return tl_skip_string(r);
    }
    default:
        logger_log(LOG_WARN, "skip_rich_text: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a pageCaption#6f747657 text:RichText credit:RichText. */
static int skip_page_caption(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pageCaption) {
        logger_log(LOG_WARN, "skip_page_caption: unknown 0x%08x", crc);
        return -1;
    }
    if (skip_rich_text(r) != 0) return -1;
    return skip_rich_text(r);
}

/* Skip a Vector<PageBlock> — recursive into skip_page_block. */
static int skip_page_block_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_page_block(r) != 0) return -1;
    }
    return 0;
}

/* Skip a single PageListItem. */
static int skip_page_list_item(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_pageListItemText:
        return skip_rich_text(r);
    case CRC_pageListItemBlocks:
        return skip_page_block_vector(r);
    default:
        logger_log(LOG_WARN, "skip_page_list_item: unknown 0x%08x", crc);
        return -1;
    }
}

static int skip_page_list_ordered_item(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_pageListOrderedItemText:
        if (tl_skip_string(r) != 0) return -1;       /* num */
        return skip_rich_text(r);
    case CRC_pageListOrderedItemBlocks:
        if (tl_skip_string(r) != 0) return -1;       /* num */
        return skip_page_block_vector(r);
    default:
        logger_log(LOG_WARN,
                   "skip_page_list_ordered_item: unknown 0x%08x", crc);
        return -1;
    }
}

/* pageTableCell#34566b6a flags:# header/align/valign:flags
 *   text:flags.7?RichText colspan:flags.1?int rowspan:flags.2?int */
static int skip_page_table_cell(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pageTableCell) {
        logger_log(LOG_WARN, "skip_page_table_cell: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 7)) {
        if (skip_rich_text(r) != 0) return -1;
    }
    if (flags & (1u << 1)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* colspan */
    }
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* rowspan */
    }
    return 0;
}

/* pageTableRow#e0c0c5e5 cells:Vector<PageTableCell>. */
static int skip_page_table_row(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pageTableRow) {
        logger_log(LOG_WARN, "skip_page_table_row: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 8) return -1;
    uint32_t vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_page_table_cell(r) != 0) return -1;
    }
    return 0;
}

/* pageRelatedArticle#b390dc08 flags:# url:string webpage_id:long
 *   title:flags.0?string description:flags.1?string
 *   photo_id:flags.2?long author:flags.3?string
 *   published_date:flags.4?int */
static int skip_page_related_article(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pageRelatedArticle) {
        logger_log(LOG_WARN,
                   "skip_page_related_article: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (tl_skip_string(r) != 0) return -1;           /* url */
    if (r->len - r->pos < 8) return -1;
    tl_read_int64(r);                                /* webpage_id */
    if (flags & (1u << 0)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 1)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* photo_id */
    }
    if (flags & (1u << 3)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 4)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* published_date */
    }
    return 0;
}

/* Skip a single PageBlock — full coverage of the PageBlock tree. */
static int skip_page_block(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_pageBlockUnsupported:
    case CRC_pageBlockDivider:
        return 0;
    case CRC_pageBlockTitle:
    case CRC_pageBlockSubtitle:
    case CRC_pageBlockHeader:
    case CRC_pageBlockSubheader:
    case CRC_pageBlockKicker:
    case CRC_pageBlockParagraph:
    case CRC_pageBlockFooter:
        return skip_rich_text(r);
    case CRC_pageBlockAnchor:
        return tl_skip_string(r);
    case CRC_pageBlockPreformatted: {
        if (skip_rich_text(r) != 0) return -1;
        return tl_skip_string(r);                    /* language */
    }
    case CRC_pageBlockAuthorDate: {
        if (skip_rich_text(r) != 0) return -1;       /* author */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* published_date */
        return 0;
    }
    case CRC_pageBlockBlockquote:
    case CRC_pageBlockPullquote: {
        if (skip_rich_text(r) != 0) return -1;       /* text */
        return skip_rich_text(r);                    /* caption */
    }
    case CRC_pageBlockPhoto: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* photo_id */
        if (skip_page_caption(r) != 0) return -1;
        if (flags & (1u << 0)) {
            if (tl_skip_string(r) != 0) return -1;   /* url */
            if (r->len - r->pos < 8) return -1;
            tl_read_int64(r);                        /* webpage_id */
        }
        return 0;
    }
    case CRC_pageBlockVideo: {
        if (r->len - r->pos < 12) return -1;
        tl_read_uint32(r);                           /* flags (autoplay/loop only) */
        tl_read_int64(r);                            /* video_id */
        return skip_page_caption(r);
    }
    case CRC_pageBlockAudio: {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                            /* audio_id */
        return skip_page_caption(r);
    }
    case CRC_pageBlockCover:
        return skip_page_block(r);
    case CRC_pageBlockChannel:
        return tl_skip_chat(r);
    case CRC_pageBlockMap: {
        if (skip_geo_point(r) != 0) return -1;
        if (r->len - r->pos < 12) return -1;
        tl_read_int32(r);                            /* zoom */
        tl_read_int32(r); tl_read_int32(r);          /* w, h */
        return skip_page_caption(r);
    }
    case CRC_pageBlockList: {
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_page_list_item(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_pageBlockOrderedList: {
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_page_list_ordered_item(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_pageBlockCollage:
    case CRC_pageBlockSlideshow: {
        if (skip_page_block_vector(r) != 0) return -1;
        return skip_page_caption(r);
    }
    case CRC_pageBlockDetails: {
        if (r->len - r->pos < 4) return -1;
        tl_read_uint32(r);                           /* flags (open only) */
        if (skip_page_block_vector(r) != 0) return -1;
        return skip_rich_text(r);                    /* title */
    }
    case CRC_pageBlockRelatedArticles: {
        if (skip_rich_text(r) != 0) return -1;       /* title */
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_page_related_article(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_pageBlockTable: {
        if (r->len - r->pos < 4) return -1;
        tl_read_uint32(r);                           /* flags (bordered/striped) */
        if (skip_rich_text(r) != 0) return -1;       /* title */
        if (r->len - r->pos < 8) return -1;
        uint32_t vec = tl_read_uint32(r);
        if (vec != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_page_table_row(r) != 0) return -1;
        }
        return 0;
    }
    case CRC_pageBlockEmbed: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & (1u << 1)) if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 2)) if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 4)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int64(r);                        /* poster_photo_id */
        }
        if (flags & (1u << 5)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int32(r); tl_read_int32(r);      /* w, h */
        }
        return skip_page_caption(r);
    }
    case CRC_pageBlockEmbedPost: {
        if (tl_skip_string(r) != 0) return -1;       /* url */
        if (r->len - r->pos < 16) return -1;
        tl_read_int64(r);                            /* webpage_id */
        tl_read_int64(r);                            /* author_photo_id */
        if (tl_skip_string(r) != 0) return -1;       /* author */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* date */
        if (skip_page_block_vector(r) != 0) return -1;
        return skip_page_caption(r);
    }
    default:
        logger_log(LOG_WARN, "skip_page_block: unsupported 0x%08x", crc);
        return -1;
    }
}

/* Skip a Page object (webPage.cached_page). Best-effort: simple
 * Instant View article bodies iterate; anything that contains a
 * complex PageBlock variant bails so the caller stops walking. */
static int skip_page(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_page) {
        logger_log(LOG_WARN, "skip_page: unknown Page variant 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (tl_skip_string(r) != 0) return -1;           /* url */

    /* blocks:Vector<PageBlock> */
    if (r->len - r->pos < 8) return -1;
    uint32_t vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_page_block(r) != 0) return -1;
    }

    /* photos:Vector<Photo> */
    if (r->len - r->pos < 8) return -1;
    vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (tl_skip_photo(r) != 0) return -1;
    }

    /* documents:Vector<Document> */
    if (r->len - r->pos < 8) return -1;
    vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (tl_skip_document(r) != 0) return -1;
    }

    /* views:flags.3?int */
    if (flags & (1u << 3)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);
    }
    return 0;
}

/* Skip a Vector<WebPageAttribute> (webPage.attributes, flags.12). Only
 * the three known WebPageAttribute variants are supported; anything
 * else makes the caller bail. */
static int skip_webpage_attributes_vector(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 8) return -1;
    uint32_t vec = tl_read_uint32(r);
    if (vec != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (r->len - r->pos < 4) return -1;
        uint32_t crc = tl_read_uint32(r);
        switch (crc) {
        case CRC_webPageAttributeTheme: {
            /* flags:# documents:flags.0?Vector<Document>
             *        settings:flags.1?ThemeSettings
             * v1 bails if settings is present — ThemeSettings is a
             * fat nested object we don't otherwise walk. */
            if (r->len - r->pos < 4) return -1;
            uint32_t flags = tl_read_uint32(r);
            if (flags & (1u << 0)) {
                if (r->len - r->pos < 8) return -1;
                uint32_t dvec = tl_read_uint32(r);
                if (dvec != TL_vector) return -1;
                uint32_t dn = tl_read_uint32(r);
                for (uint32_t j = 0; j < dn; j++) {
                    if (tl_skip_document(r) != 0) return -1;
                }
            }
            if (flags & (1u << 1)) {
                logger_log(LOG_WARN,
                    "skip_webpage_attributes: theme settings not supported");
                return -1;
            }
            break;
        }
        case CRC_webPageAttributeStory: {
            /* flags:# peer:Peer id:int story:flags.0?StoryItem */
            if (r->len - r->pos < 4) return -1;
            uint32_t flags = tl_read_uint32(r);
            if (tl_skip_peer(r) != 0) return -1;
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                        /* id */
            if (flags & (1u << 0)) {
                if (skip_story_item(r) != 0) return -1;
            }
            break;
        }
        case CRC_webPageAttributeStickerSet: {
            /* flags:# emojis:flags.0?true text_color:flags.1?true
             *        stickers:Vector<Document> */
            if (r->len - r->pos < 4) return -1;
            (void)tl_read_uint32(r);                 /* flags */
            if (r->len - r->pos < 8) return -1;
            uint32_t svec = tl_read_uint32(r);
            if (svec != TL_vector) return -1;
            uint32_t sn = tl_read_uint32(r);
            for (uint32_t j = 0; j < sn; j++) {
                if (tl_skip_document(r) != 0) return -1;
            }
            break;
        }
        default:
            logger_log(LOG_WARN,
                "skip_webpage_attributes: unknown variant 0x%08x", crc);
            return -1;
        }
    }
    return 0;
}

/* Skip a WebPage variant. */
static int skip_webpage(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_webPageEmpty: {
        if (r->len - r->pos < 12) return -1;
        uint32_t flags = tl_read_uint32(r);
        tl_read_int64(r);                        /* id */
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;
        return 0;
    }
    case CRC_webPagePending: {
        if (r->len - r->pos < 12) return -1;
        uint32_t flags = tl_read_uint32(r);
        tl_read_int64(r);
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                        /* date */
        return 0;
    }
    case CRC_webPageNotModified: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & (1u << 0)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                    /* cached_page_views */
        }
        return 0;
    }
    case CRC_webPage: {
        /* webPage#e89c45b2 flags:# id:long url:string display_url:string
         *   hash:int type:flags.0?string site_name:flags.1?string
         *   title:flags.2?string description:flags.3?string
         *   photo:flags.4?Photo embed_url:flags.5?string
         *   embed_type:flags.5?string embed_width/height:flags.6?int
         *   duration:flags.7?int author:flags.8?string
         *   document:flags.9?Document cached_page:flags.10?Page
         *   attributes:flags.12?Vector<WebPageAttribute>
         */
        if (r->len - r->pos < 16) return -1;
        uint32_t flags = tl_read_uint32(r);
        tl_read_int64(r);                        /* id */
        if (tl_skip_string(r) != 0) return -1;   /* url */
        if (tl_skip_string(r) != 0) return -1;   /* display_url */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                        /* hash */
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 1))
            if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 2))
            if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 3))
            if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 4)) {
            if (photo_full(r, NULL) != 0) return -1;
        }
        if (flags & (1u << 5)) {
            if (tl_skip_string(r) != 0) return -1;
            if (tl_skip_string(r) != 0) return -1;
        }
        if (flags & (1u << 6)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int32(r); tl_read_int32(r);
        }
        if (flags & (1u << 7)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);
        }
        if (flags & (1u << 8))
            if (tl_skip_string(r) != 0) return -1;
        if (flags & (1u << 9)) {
            /* document:Document — reuse tl_skip_document. */
            if (tl_skip_document(r) != 0) return -1;
        }
        if (flags & (1u << 10)) {
            if (skip_page(r) != 0) return -1;
        }
        if (flags & (1u << 12)) {
            if (skip_webpage_attributes_vector(r) != 0) return -1;
        }
        return 0;
    }
    default:
        logger_log(LOG_WARN, "skip_webpage: unknown 0x%08x", crc);
        return -1;
    }
}

/* Skip a textWithEntities#751f3146: text:string + Vector<MessageEntity>. */
static int skip_text_with_entities(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_textWithEntities_poll) {
        logger_log(LOG_WARN, "skip_text_with_entities: unknown 0x%08x", crc);
        return -1;
    }
    if (tl_skip_string(r) != 0) return -1;
    return tl_skip_message_entities_vector(r);
}

/* Skip a Poll#58747131 object. */
static int skip_poll(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_poll) {
        logger_log(LOG_WARN, "skip_poll: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 12) return -1;
    uint32_t flags = tl_read_uint32(r);
    tl_read_int64(r);                                /* id */
    if (skip_text_with_entities(r) != 0) return -1;  /* question */

    /* answers:Vector<PollAnswer> */
    if (r->len - r->pos < 8) return -1;
    uint32_t vec_crc = tl_read_uint32(r);
    if (vec_crc != TL_vector) return -1;
    uint32_t n_answers = tl_read_uint32(r);
    for (uint32_t i = 0; i < n_answers; i++) {
        if (r->len - r->pos < 4) return -1;
        uint32_t pa_crc = tl_read_uint32(r);
        if (pa_crc != CRC_pollAnswer) {
            logger_log(LOG_WARN, "skip_poll: bad PollAnswer 0x%08x", pa_crc);
            return -1;
        }
        if (skip_text_with_entities(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;       /* option:bytes */
    }
    if (flags & (1u << 4)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* close_period */
    }
    if (flags & (1u << 5)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* close_date */
    }
    return 0;
}

/* Skip a PollAnswerVoters#3b6ddad2. */
static int skip_poll_answer_voters(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pollAnswerVoters) {
        logger_log(LOG_WARN, "skip_poll_answer_voters: 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    tl_read_uint32(r);                               /* flags */
    if (tl_skip_string(r) != 0) return -1;           /* option:bytes */
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);                                /* voters */
    return 0;
}

/* Skip a PollResults#7adc669d object. */
static int skip_poll_results(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_pollResults) {
        logger_log(LOG_WARN, "skip_poll_results: 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 1)) {
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_poll_answer_voters(r) != 0) return -1;
        }
    }
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                            /* total_voters */
    }
    if (flags & (1u << 3)) {
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (tl_skip_peer(r) != 0) return -1;
        }
    }
    if (flags & (1u << 4)) {
        if (tl_skip_string(r) != 0) return -1;       /* solution */
        if (tl_skip_message_entities_vector(r) != 0) return -1;
    }
    return 0;
}

/* ---- Game ----
 * game#bdf9653b flags:# id:long access_hash:long short_name:string
 *   title:string description:string photo:Photo document:flags.0?Document
 */
static int skip_game(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_game) {
        logger_log(LOG_WARN, "skip_game: unexpected 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4 + 8 + 8) return -1;
    uint32_t flags = tl_read_uint32(r);
    tl_read_int64(r);                                /* id */
    tl_read_int64(r);                                /* access_hash */
    if (tl_skip_string(r) != 0) return -1;           /* short_name */
    if (tl_skip_string(r) != 0) return -1;           /* title */
    if (tl_skip_string(r) != 0) return -1;           /* description */
    if (tl_skip_photo(r) != 0) return -1;            /* photo */
    if (flags & (1u << 0)) {
        if (tl_skip_document(r) != 0) return -1;     /* document */
    }
    return 0;
}

/* ---- MessageExtendedMedia ----
 * messageExtendedMediaPreview#ad628cc8 flags:# w:flags.0?int h:flags.0?int
 *   thumb:flags.1?PhotoSize video_duration:flags.2?int
 * messageExtendedMedia#ee479c64 media:MessageMedia
 */
static int skip_message_extended_media(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_messageExtendedMediaPreview: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (flags & (1u << 0)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int32(r); tl_read_int32(r);       /* w, h */
        }
        if (flags & (1u << 1)) {
            if (tl_skip_photo_size(r) != 0) return -1;
        }
        if (flags & (1u << 2)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                         /* video_duration */
        }
        return 0;
    }
    case CRC_messageExtendedMedia:
        /* Recurse into the wrapped MessageMedia. The inner metadata is
         * discarded — paid-media iteration only needs the outer kind. */
        return tl_skip_message_media_ex(r, NULL);
    default:
        logger_log(LOG_WARN,
                   "skip_message_extended_media: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- WebDocument ----
 * webDocument#1c570ed1        url:string access_hash:long size:int
 *                             mime_type:string attributes:Vector<DocumentAttribute>
 * webDocumentNoProxy#f9c8bcc6 url:string size:int mime_type:string
 *                             attributes:Vector<DocumentAttribute>
 */
static int skip_web_document(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    int has_access_hash;
    switch (crc) {
    case CRC_webDocument:        has_access_hash = 1; break;
    case CRC_webDocumentNoProxy: has_access_hash = 0; break;
    default:
        logger_log(LOG_WARN, "skip_web_document: unknown 0x%08x", crc);
        return -1;
    }
    if (tl_skip_string(r) != 0) return -1;                /* url */
    if (has_access_hash) {
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                                 /* access_hash */
    }
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);                                     /* size */
    if (tl_skip_string(r) != 0) return -1;                /* mime_type */
    /* attributes:Vector<DocumentAttribute> */
    if (r->len - r->pos < 8) return -1;
    uint32_t vc = tl_read_uint32(r);
    if (vc != TL_vector) return -1;
    uint32_t n = tl_read_uint32(r);
    for (uint32_t i = 0; i < n; i++) {
        if (skip_document_attribute(r, NULL, 0) != 0) return -1;
    }
    return 0;
}

/* ---- StoryFwdHeader ----
 * storyFwdHeader#b826e150 flags:# modified:flags.3?true
 *   from:flags.0?Peer from_name:flags.1?string story_id:flags.2?int
 */
static int skip_story_fwd_header(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_storyFwdHeader) {
        logger_log(LOG_WARN, "skip_story_fwd_header: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (flags & (1u << 0))
        if (tl_skip_peer(r) != 0) return -1;              /* from */
    if (flags & (1u << 1))
        if (tl_skip_string(r) != 0) return -1;            /* from_name */
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                                 /* story_id */
    }
    return 0;
}

/* ---- MediaAreaCoordinates ----
 * mediaAreaCoordinates#03d1ea4e flags:# x:double y:double w:double h:double
 *                                rotation:double radius:flags.0?double
 */
static int skip_media_area_coordinates(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_mediaAreaCoordinates) {
        logger_log(LOG_WARN,
                   "skip_media_area_coordinates: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4 + 5 * 8) return -1;
    uint32_t flags = tl_read_uint32(r);
    for (int i = 0; i < 5; i++) tl_read_double(r);        /* x, y, w, h, rot */
    if (flags & (1u << 0)) {
        if (r->len - r->pos < 8) return -1;
        tl_read_double(r);                                /* radius */
    }
    return 0;
}

/* ---- GeoPointAddress ----
 * geoPointAddress#de4c5d93 flags:# country_iso2:string
 *   state:flags.0?string city:flags.1?string street:flags.2?string
 */
static int skip_geo_point_address(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_geoPointAddress) {
        logger_log(LOG_WARN,
                   "skip_geo_point_address: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (tl_skip_string(r) != 0) return -1;                /* country_iso2 */
    if (flags & (1u << 0))
        if (tl_skip_string(r) != 0) return -1;            /* state */
    if (flags & (1u << 1))
        if (tl_skip_string(r) != 0) return -1;            /* city */
    if (flags & (1u << 2))
        if (tl_skip_string(r) != 0) return -1;            /* street */
    return 0;
}

/* ---- MediaArea ---- dispatch over 7 known variants. */
static int skip_media_area(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_mediaAreaVenue:
        if (skip_media_area_coordinates(r) != 0) return -1;
        if (skip_geo_point(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;            /* title */
        if (tl_skip_string(r) != 0) return -1;            /* address */
        if (tl_skip_string(r) != 0) return -1;            /* provider */
        if (tl_skip_string(r) != 0) return -1;            /* venue_id */
        if (tl_skip_string(r) != 0) return -1;            /* venue_type */
        return 0;
    case CRC_mediaAreaGeoPoint: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (skip_media_area_coordinates(r) != 0) return -1;
        if (skip_geo_point(r) != 0) return -1;
        if (flags & (1u << 0))
            if (skip_geo_point_address(r) != 0) return -1;
        return 0;
    }
    case CRC_mediaAreaSuggestedReaction: {
        if (r->len - r->pos < 4) return -1;
        tl_read_uint32(r);                                /* flags */
        if (skip_media_area_coordinates(r) != 0) return -1;
        return skip_reaction(r);
    }
    case CRC_mediaAreaChannelPost:
        if (skip_media_area_coordinates(r) != 0) return -1;
        if (r->len - r->pos < 12) return -1;
        tl_read_int64(r);                                 /* channel_id */
        tl_read_int32(r);                                 /* msg_id */
        return 0;
    case CRC_mediaAreaUrl:
        if (skip_media_area_coordinates(r) != 0) return -1;
        return tl_skip_string(r);                         /* url */
    case CRC_mediaAreaWeather:
        if (skip_media_area_coordinates(r) != 0) return -1;
        if (tl_skip_string(r) != 0) return -1;            /* emoji */
        if (r->len - r->pos < 8 + 4) return -1;
        tl_read_double(r);                                /* temperature_c */
        tl_read_int32(r);                                 /* color */
        return 0;
    case CRC_mediaAreaStarGift:
        if (skip_media_area_coordinates(r) != 0) return -1;
        return tl_skip_string(r);                         /* slug */
    default:
        logger_log(LOG_WARN, "skip_media_area: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- PrivacyRule ---- 12 variants; most are CRC-only. */
static int skip_privacy_rule(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_privacyValueAllowContacts:
    case CRC_privacyValueAllowAll:
    case CRC_privacyValueDisallowContacts:
    case CRC_privacyValueDisallowAll:
    case CRC_privacyValueAllowCloseFriends:
    case CRC_privacyValueAllowPremium:
    case CRC_privacyValueAllowBots:
    case CRC_privacyValueDisallowBots:
        return 0;
    case CRC_privacyValueAllowUsers:
    case CRC_privacyValueDisallowUsers:
    case CRC_privacyValueAllowChatParticipants:
    case CRC_privacyValueDisallowChatParticipants: {
        /* Vector<long> */
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        if (r->len - r->pos < (size_t)n * 8) return -1;
        for (uint32_t i = 0; i < n; i++) tl_read_int64(r);
        return 0;
    }
    default:
        logger_log(LOG_WARN, "skip_privacy_rule: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- StoryViews ----
 * storyViews#8d595cd6 flags:# has_viewers:flags.1?true views_count:int
 *   forwards_count:flags.2?int reactions:flags.3?Vector<ReactionCount>
 *   reactions_count:flags.4?int recent_viewers:flags.0?Vector<long>
 */
static int skip_story_views(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    if (crc != CRC_storyViews) {
        logger_log(LOG_WARN, "skip_story_views: unknown 0x%08x", crc);
        return -1;
    }
    if (r->len - r->pos < 4) return -1;
    uint32_t flags = tl_read_uint32(r);
    if (r->len - r->pos < 4) return -1;
    tl_read_int32(r);                                     /* views_count */
    if (flags & (1u << 2)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                                 /* forwards_count */
    }
    if (flags & (1u << 3)) {
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++)
            if (skip_reaction_count(r) != 0) return -1;
    }
    if (flags & (1u << 4)) {
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                                 /* reactions_count */
    }
    if (flags & (1u << 0)) {
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        if (r->len - r->pos < (size_t)n * 8) return -1;
        for (uint32_t i = 0; i < n; i++) tl_read_int64(r);
    }
    return 0;
}

/* ---- StoryItem ----
 * storyItemDeleted#51e6ee4f id:int
 * storyItemSkipped#ffadc913 flags:# close_friends:flags.8?true
 *                           id:int date:int expire_date:int
 * storyItem#79b26a24 — full layout. The inner `media:MessageMedia`
 * dispatches recursively through tl_skip_message_media_ex, which
 * handles the common sub-cases (empty / photo / document / webpage /
 * …). Deeply nested stories-inside-stories would bail by the inner
 * dispatcher's own bail rules.
 */
static int skip_story_item(TlReader *r) {
    if (!tl_reader_ok(r) || r->len - r->pos < 4) return -1;
    uint32_t crc = tl_read_uint32(r);
    switch (crc) {
    case CRC_storyItemDeleted:
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                                 /* id */
        return 0;
    case CRC_storyItemSkipped: {
        if (r->len - r->pos < 4) return -1;
        tl_read_uint32(r);                                /* flags */
        if (r->len - r->pos < 12) return -1;
        tl_read_int32(r);                                 /* id */
        tl_read_int32(r);                                 /* date */
        tl_read_int32(r);                                 /* expire_date */
        return 0;
    }
    case CRC_storyItem: {
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (r->len - r->pos < 8) return -1;
        tl_read_int32(r);                                 /* id */
        tl_read_int32(r);                                 /* date */
        if (flags & (1u << 18))
            if (tl_skip_peer(r) != 0) return -1;          /* from_id */
        if (flags & (1u << 17))
            if (skip_story_fwd_header(r) != 0) return -1; /* fwd_from */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                                 /* expire_date */
        if (flags & (1u << 0))
            if (tl_skip_string(r) != 0) return -1;        /* caption */
        if (flags & (1u << 1))
            if (tl_skip_message_entities_vector(r) != 0) return -1;
        if (tl_skip_message_media_ex(r, NULL) != 0) return -1;
        if (flags & (1u << 14)) {
            if (r->len - r->pos < 8) return -1;
            uint32_t vc = tl_read_uint32(r);
            if (vc != TL_vector) return -1;
            uint32_t n = tl_read_uint32(r);
            for (uint32_t i = 0; i < n; i++)
                if (skip_media_area(r) != 0) return -1;
        }
        if (flags & (1u << 2)) {
            if (r->len - r->pos < 8) return -1;
            uint32_t vc = tl_read_uint32(r);
            if (vc != TL_vector) return -1;
            uint32_t n = tl_read_uint32(r);
            for (uint32_t i = 0; i < n; i++)
                if (skip_privacy_rule(r) != 0) return -1;
        }
        if (flags & (1u << 3))
            if (skip_story_views(r) != 0) return -1;
        if (flags & (1u << 15))
            if (skip_reaction(r) != 0) return -1;         /* sent_reaction */
        return 0;
    }
    default:
        logger_log(LOG_WARN, "skip_story_item: unknown 0x%08x", crc);
        return -1;
    }
}

/* ---- MessageMedia skipper ----
 * Covers all normal variants (photo, document, geo, contact, venue,
 * poll, invoice, story-deleted/skipped, giveaway, game, paid, webpage).
 * Returns -1 only on the heaviest variants: full inline storyItem and
 * the rare Invoice extended_media with unknown ExtendedMedia CRCs.
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

    case CRC_messageMediaWebPage: {
        if (out) out->kind = MEDIA_WEBPAGE;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        (void)flags;                              /* only boolean previews */
        return skip_webpage(r);
    }

    case CRC_messageMediaPoll: {
        if (out) out->kind = MEDIA_POLL;
        if (skip_poll(r) != 0) return -1;
        return skip_poll_results(r);
    }

    case CRC_messageMediaInvoice: {
        if (out) out->kind = MEDIA_INVOICE;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_string(r) != 0) return -1;    /* title */
        if (tl_skip_string(r) != 0) return -1;    /* description */
        if (flags & (1u << 0)) {
            if (skip_web_document(r) != 0) return -1;
        }
        if (flags & (1u << 2)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                     /* receipt_msg_id */
        }
        if (tl_skip_string(r) != 0) return -1;    /* currency */
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                         /* total_amount */
        if (tl_skip_string(r) != 0) return -1;    /* start_param */
        if (flags & (1u << 4)) {
            if (skip_message_extended_media(r) != 0) return -1;
        }
        return 0;
    }

    case CRC_messageMediaStory: {
        if (out) out->kind = MEDIA_STORY;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        if (tl_skip_peer(r) != 0) return -1;      /* peer */
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                         /* id */
        if (flags & (1u << 0)) {
            if (skip_story_item(r) != 0) return -1;
        }
        return 0;
    }

    case CRC_messageMediaGiveaway: {
        if (out) out->kind = MEDIA_GIVEAWAY;
        if (r->len - r->pos < 4) return -1;
        uint32_t flags = tl_read_uint32(r);
        /* channels:Vector<long> */
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n_ch = tl_read_uint32(r);
        if (r->len - r->pos < (size_t)n_ch * 8) return -1;
        for (uint32_t i = 0; i < n_ch; i++) tl_read_int64(r);
        if (flags & (1u << 1)) {
            if (r->len - r->pos < 8) return -1;
            uint32_t cvc = tl_read_uint32(r);
            if (cvc != TL_vector) return -1;
            uint32_t n_c = tl_read_uint32(r);
            for (uint32_t i = 0; i < n_c; i++)
                if (tl_skip_string(r) != 0) return -1;
        }
        if (flags & (1u << 3))
            if (tl_skip_string(r) != 0) return -1;
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                         /* quantity */
        if (flags & (1u << 4)) {
            if (r->len - r->pos < 4) return -1;
            tl_read_int32(r);                     /* months */
        }
        if (flags & (1u << 5)) {
            if (r->len - r->pos < 8) return -1;
            tl_read_int64(r);                     /* stars */
        }
        if (r->len - r->pos < 4) return -1;
        tl_read_int32(r);                         /* until_date */
        return 0;
    }

    case CRC_messageMediaGame: {
        if (out) out->kind = MEDIA_GAME;
        return skip_game(r);
    }

    case CRC_messageMediaPaidMedia: {
        if (out) out->kind = MEDIA_PAID;
        if (r->len - r->pos < 8) return -1;
        tl_read_int64(r);                         /* stars_amount */
        if (r->len - r->pos < 8) return -1;
        uint32_t vc = tl_read_uint32(r);
        if (vc != TL_vector) return -1;
        uint32_t n = tl_read_uint32(r);
        for (uint32_t i = 0; i < n; i++) {
            if (skip_message_extended_media(r) != 0) return -1;
        }
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
            if (document_inner(r, out) != 0) return -1;
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
    if (out) {
        out->id = 0;
        out->access_hash = 0;
        out->have_access_hash = 0;
        out->title[0] = '\0';
    }

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
        int64_t access_hash = tl_read_int64(r);
        if (out) {
            out->access_hash = access_hash;
            out->have_access_hash = 1;
        }
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
            int64_t access_hash = tl_read_int64(r);
            if (out) {
                out->access_hash = access_hash;
                out->have_access_hash = 1;
            }
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
        out->access_hash = 0;
        out->have_access_hash = 0;
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
        int64_t access_hash = tl_read_int64(r);
        if (out) {
            out->access_hash = access_hash;
            out->have_access_hash = 1;
        }
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
    if (flags & (1u << 23)) if (tl_skip_message_replies(r) != 0) return -1;
    if (flags & (1u << 15)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags & (1u << 16)) if (tl_skip_string(r) != 0) return -1;
    if (flags & (1u << 17)) { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    if (flags & (1u << 20)) if (tl_skip_message_reactions(r) != 0) return -1;
    if (flags & (1u << 22)) if (tl_skip_restriction_reason_vector(r) != 0) return -1;
    if (flags & (1u << 25)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags2 & (1u << 30)) { if (r->len - r->pos < 4) return -1; tl_read_int32(r); }
    if (flags2 & (1u << 2))  { if (r->len - r->pos < 8) return -1; tl_read_int64(r); }
    if (flags2 & (1u << 3))  if (tl_skip_factcheck(r) != 0) return -1;
    return 0;
}
