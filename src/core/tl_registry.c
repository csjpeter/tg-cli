/**
 * @file tl_registry.c
 * @brief TL constructor registry — lookup table.
 */

#include "tl_registry.h"

#include <stddef.h>

static const TlRegistryEntry g_registry[] = {
    /* MTProto internal */
    { TL_rpc_result,            "rpc_result" },
    { TL_rpc_error,             "rpc_error" },
    { TL_gzip_packed,           "gzip_packed" },
    { TL_msg_container,         "msg_container" },
    { TL_msgs_ack,              "msgs_ack" },
    { TL_bad_msg_notification,  "bad_msg_notification" },
    { TL_bad_server_salt,       "bad_server_salt" },
    { TL_new_session_created,   "new_session_created" },
    { TL_pong,                  "pong" },
    { TL_future_salts,          "future_salts" },
    { TL_destroy_session_ok,    "destroy_session_ok" },
    { TL_destroy_session_none,  "destroy_session_none" },

    /* Auth (DH exchange) */
    { TL_resPQ,                 "resPQ" },
    { TL_server_DH_params_ok,   "server_DH_params_ok" },
    { TL_server_DH_params_fail, "server_DH_params_fail" },
    { TL_dh_gen_ok,             "dh_gen_ok" },
    { TL_dh_gen_retry,          "dh_gen_retry" },
    { TL_dh_gen_fail,           "dh_gen_fail" },

    /* auth.* */
    { TL_auth_sentCode,         "auth.sentCode" },
    { TL_auth_authorization,    "auth.authorization" },

    /* account.* (2FA / SRP) */
    { TL_account_password,      "account.password" },
    { TL_passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow,
                                "passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow" },
    { TL_passwordKdfAlgoUnknown, "passwordKdfAlgoUnknown" },
    { TL_inputCheckPasswordSRP, "inputCheckPasswordSRP" },
    { TL_inputCheckPasswordEmpty, "inputCheckPasswordEmpty" },

    /* User */
    { TL_user,                  "user" },
    { TL_userEmpty,             "userEmpty" },
    { TL_userFull,              "userFull" },

    /* Chat/Channel */
    { TL_chat,                  "chat" },
    { TL_chatEmpty,             "chatEmpty" },
    { TL_chatForbidden,         "chatForbidden" },
    { TL_channel,               "channel" },
    { TL_channelForbidden,      "channelForbidden" },

    /* Messages */
    { TL_message,               "message" },
    { TL_messageEmpty,          "messageEmpty" },
    { TL_messageService,        "messageService" },
    { TL_messages_dialogs,      "messages.dialogs" },
    { TL_messages_dialogsSlice,       "messages.dialogsSlice" },
    { TL_messages_dialogsNotModified, "messages.dialogsNotModified" },
    { TL_messages_messages,     "messages.messages" },
    { TL_messages_messagesSlice,"messages.messagesSlice" },
    { TL_messages_channelMessages, "messages.channelMessages" },
    { TL_messages_affectedHistory, "messages.affectedHistory" },
    { TL_messages_affectedMessages, "messages.affectedMessages" },

    /* Media */
    { TL_messageMediaEmpty,     "messageMediaEmpty" },
    { TL_messageMediaPhoto,     "messageMediaPhoto" },
    { TL_messageMediaDocument,  "messageMediaDocument" },
    { TL_messageMediaGeo,       "messageMediaGeo" },
    { TL_messageMediaContact,   "messageMediaContact" },
    { TL_messageMediaWebPage,   "messageMediaWebPage" },

    /* Document/Photo */
    { TL_document,              "document" },
    { TL_documentEmpty,         "documentEmpty" },
    { TL_photo,                 "photo" },
    { TL_photoEmpty,            "photoEmpty" },
    { TL_photoSize,             "photoSize" },

    /* Contacts */
    { TL_contacts_contacts,     "contacts.contacts" },
    { TL_contacts_contactsNotModified, "contacts.contactsNotModified" },
    { TL_contacts_resolvedPeer, "contacts.resolvedPeer" },

    /* Updates */
    { TL_updates_state,         "updates.state" },
    { TL_updates_difference,    "updates.difference" },
    { TL_updates_differenceEmpty, "updates.differenceEmpty" },
    { TL_updates_differenceSlice, "updates.differenceSlice" },
    { TL_updateShort,           "updateShort" },
    { TL_updates,               "updates" },
    { TL_updatesCombined,       "updatesCombined" },
    { TL_updateNewMessage,      "updateNewMessage" },
    { TL_updateReadHistoryInbox, "updateReadHistoryInbox" },
    { TL_updateReadHistoryOutbox, "updateReadHistoryOutbox" },

    /* Peer */
    { TL_peerUser,              "peerUser" },
    { TL_peerChat,              "peerChat" },
    { TL_peerChannel,           "peerChannel" },
    { TL_inputPeerUser,         "inputPeerUser" },
    { TL_inputPeerChat,         "inputPeerChat" },
    { TL_inputPeerChannel,      "inputPeerChannel" },
    { TL_inputPeerSelf,         "inputPeerSelf" },
    { TL_inputPeerEmpty,        "inputPeerEmpty" },

    /* Config */
    { TL_config,                "config" },
    { TL_dcOption,              "dcOption" },
    { TL_nearestDc,             "nearestDc" },

    /* Bool */
    { TL_boolTrue,              "boolTrue" },
    { TL_boolFalse,             "boolFalse" },

    /* Vector */
    { TL_vector,                "vector" },
};

#define REGISTRY_SIZE (sizeof(g_registry) / sizeof(g_registry[0]))

const char *tl_constructor_name(uint32_t id) {
    for (size_t i = 0; i < REGISTRY_SIZE; i++) {
        if (g_registry[i].id == id) return g_registry[i].name;
    }
    return "unknown";
}

int tl_constructor_known(uint32_t id) {
    for (size_t i = 0; i < REGISTRY_SIZE; i++) {
        if (g_registry[i].id == id) return 1;
    }
    return 0;
}
