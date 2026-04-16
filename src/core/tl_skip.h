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

#endif /* TL_SKIP_H */
