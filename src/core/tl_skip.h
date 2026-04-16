/**
 * @file tl_skip.h
 * @brief Advance a TlReader past nested TL objects without fully parsing them.
 *
 * Telegram objects are flag-conditional and contain variable-length nested
 * types. A full schema-driven parser is future work (see issues/incoming/
 * P5-07). Until then these helpers cover the nested types that block
 * iteration through Vector<Dialog> / Vector<Message>.
 *
 * Each skipper returns 0 on success and leaves `r->pos` past the object.
 * On -1 the cursor position is undefined — the caller should stop
 * iterating the enclosing vector.
 */

#ifndef TL_SKIP_H
#define TL_SKIP_H

#include "tl_serial.h"

#include <stdint.h>

/** Skip a TL Bool (4 bytes; the ID is the value). */
int tl_skip_bool(TlReader *r);

/** Skip a TL string (length-prefixed + 4-byte padding). */
int tl_skip_string(TlReader *r);

/** Skip a TL Peer (crc + int64). Returns 0 if crc was a known Peer variant. */
int tl_skip_peer(TlReader *r);

/** Skip a NotificationSound variant. */
int tl_skip_notification_sound(TlReader *r);

/** Skip a PeerNotifySettings. */
int tl_skip_peer_notify_settings(TlReader *r);

/** Skip a DraftMessage (draftMessageEmpty or draftMessage). */
int tl_skip_draft_message(TlReader *r);

/** Skip a single MessageEntity (any of the known variants). */
int tl_skip_message_entity(TlReader *r);

/** Skip a `Vector<MessageEntity>` (TL_vector + count + entities). */
int tl_skip_message_entities_vector(TlReader *r);

/** Skip a MessageFwdHeader. */
int tl_skip_message_fwd_header(TlReader *r);

/** Skip a MessageReplyHeader (conservative — bails on reply_media). */
int tl_skip_message_reply_header(TlReader *r);

/** Skip a PhotoSize variant. */
int tl_skip_photo_size(TlReader *r);

/** Skip a Vector<PhotoSize>. */
int tl_skip_photo_size_vector(TlReader *r);

/** Skip a Photo variant (photo or photoEmpty). */
int tl_skip_photo(TlReader *r);

/** Skip a Document variant (document or documentEmpty). */
int tl_skip_document(TlReader *r);

/** Skip a MessageMedia variant. Handles the common ones; on the rest
 *  returns -1 so the caller stops iterating. */
int tl_skip_message_media(TlReader *r);

/* ---- Chat / User support ---- */

/** Skip a ChatPhoto (chatPhotoEmpty or chatPhoto). */
int tl_skip_chat_photo(TlReader *r);

/** Skip a UserProfilePhoto (empty or non-empty). */
int tl_skip_user_profile_photo(TlReader *r);

/** Skip a UserStatus (empty / online / offline / recently / lastWeek / lastMonth). */
int tl_skip_user_status(TlReader *r);

/** Skip a Vector<RestrictionReason>. */
int tl_skip_restriction_reason_vector(TlReader *r);

/** Skip a Vector<Username>. */
int tl_skip_username_vector(TlReader *r);

/** Skip a PeerColor. */
int tl_skip_peer_color(TlReader *r);

/** Skip an EmojiStatus (empty / plain / until). */
int tl_skip_emoji_status(TlReader *r);

/** Skip a Chat (chatEmpty / chat / chatForbidden / channel / channelForbidden). */
int tl_skip_chat(TlReader *r);

/** Skip a User (userEmpty / user). */
int tl_skip_user(TlReader *r);

/**
 * @brief Skip a Message object (any of message / messageEmpty /
 *        messageService) past its trailer.
 *
 * Same contract as tl_skip_chat/tl_skip_user: advances past the whole
 * object on success, returns -1 on fail (cursor may be mid-object).
 * Bails on flags that have no skipper yet (reply_markup, reactions,
 * replies, restriction_reason, factcheck, unsupported MessageMedia).
 */
int tl_skip_message(TlReader *r);

/** Summary fields extracted from a Chat object. */
typedef struct {
    int64_t id;
    char    title[128];
} ChatSummary;

/**
 * @brief Skip a Chat while extracting (id, title).
 *
 * On success the cursor is advanced past the whole Chat object, the same as
 * tl_skip_chat(). For chatEmpty the title is left as an empty string.
 *
 * @param r   Reader.
 * @param out Receives id + title (title is always NUL-terminated).
 * @return 0 on success, -1 on failure.
 */
int tl_extract_chat(TlReader *r, ChatSummary *out);

/** Summary fields extracted from a User object. */
typedef struct {
    int64_t id;
    char    name[128];
    char    username[64];
} UserSummary;

/**
 * @brief Skip a User while extracting (id, name, username).
 *
 * `name` joins first_name and last_name with a single space when both are
 * present. Fields absent in the payload are left as empty strings. On success
 * the cursor is advanced past the whole User object.
 *
 * @param r   Reader.
 * @param out Receives id + name + username (both always NUL-terminated).
 * @return 0 on success, -1 on failure.
 */
int tl_extract_user(TlReader *r, UserSummary *out);

#endif /* TL_SKIP_H */
