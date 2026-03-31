/**
 * @file tl_registry.h
 * @brief TL constructor registry — maps constructor IDs to names.
 *
 * Provides a lookup table for identifying TL objects by their constructor ID.
 * Used for debugging, logging, and dispatching received messages.
 */

#ifndef TL_REGISTRY_H
#define TL_REGISTRY_H

#include <stdint.h>

/* ---- Well-known constructor IDs ---- */

/* MTProto internal */
#define TL_rpc_result           0xf35c6d01
#define TL_rpc_error            0x2144ca19
#define TL_gzip_packed          0x3072cfa1
#define TL_msg_container        0x73f1f8dc
#define TL_msg_copy             0xe06046b2
#define TL_msgs_ack             0x62d6b459
#define TL_bad_msg_notification 0xa7eff811
#define TL_bad_server_salt      0xedab447b
#define TL_new_session_created  0x9ec20908
#define TL_pong                 0x347773c5
#define TL_future_salts         0xae500895
#define TL_msgs_state_req       0xda69fb52
#define TL_msgs_state_info      0x04deb57d
#define TL_msgs_all_info        0x8cc0d131
#define TL_msg_detailed_info    0x276d3ec6
#define TL_msg_new_detailed_info 0x809db6df
#define TL_destroy_session_ok   0xe22045fc
#define TL_destroy_session_none 0x62d350c9

/* Auth */
#define TL_resPQ                0x05162463
#define TL_server_DH_params_ok  0xd0e8075c
#define TL_server_DH_params_fail 0x79cb045d
#define TL_dh_gen_ok            0x3bcbf734
#define TL_dh_gen_retry         0x46dc1fb9
#define TL_dh_gen_fail          0xa69dae02

/* auth.* */
#define TL_auth_sentCode        0x5e002502
#define TL_auth_authorization   0x2ea2c0d4
#define TL_auth_password        0x4a82327c

/* User */
#define TL_user                 0x3ff6ecb0
#define TL_userEmpty            0xd3bc4b7a
#define TL_userFull             0x93eadb53

/* Chat/Channel */
#define TL_chat                 0x41cbf256
#define TL_chatEmpty            0x29562865
#define TL_chatForbidden        0x6592a1a7
#define TL_channel              0x0aadfc8f
#define TL_channelForbidden     0x17d493d5

/* Messages */
#define TL_message              0x94345242
#define TL_messageEmpty         0x90a6ca84
#define TL_messageService       0x2b085862
#define TL_messages_dialogs     0x15ba6c40
#define TL_messages_dialogsSlice 0x71e094f3
#define TL_messages_messages    0x8c718e87
#define TL_messages_messagesSlice 0x3a54685e
#define TL_messages_channelMessages 0xc776ba4e
#define TL_messages_affectedHistory 0xb45c69d1
#define TL_messages_affectedMessages 0x84d19185

/* Media */
#define TL_messageMediaEmpty    0x3ded6320
#define TL_messageMediaPhoto    0x695150d7
#define TL_messageMediaDocument 0x4cf4d72d
#define TL_messageMediaGeo      0x56e0d474
#define TL_messageMediaContact  0x70322949
#define TL_messageMediaWebPage  0xddf8c26e

/* Document */
#define TL_document             0x8fd4c4d8
#define TL_documentEmpty        0x36f8c871

/* Photo */
#define TL_photo                0xfb197a65
#define TL_photoEmpty           0x2331b22d
#define TL_photoSize            0x75c78e60
#define TL_photoSizeProgressive 0xfa3efb95

/* Contacts */
#define TL_contacts_contacts    0xeae87e42
#define TL_contacts_contactsNotModified 0xb74ba9d2
#define TL_contacts_resolvedPeer 0x7f077ad9

/* Updates */
#define TL_updates_state        0xa56c2a3e
#define TL_updates_difference   0x00f49d63
#define TL_updates_differenceEmpty 0x5d75a138
#define TL_updates_differenceSlice 0xa8fb1981
#define TL_updateShort          0x78d4dec1
#define TL_updates              0x74ae4240
#define TL_updatesCombined      0x725b04c3
#define TL_updateNewMessage     0x1f2b0afd
#define TL_updateReadHistoryInbox 0x9c974fdf
#define TL_updateReadHistoryOutbox 0x2f2f21bf

/* Peer */
#define TL_peerUser             0x59511722
#define TL_peerChat             0x36c6019a
#define TL_peerChannel          0xa2a5371e
#define TL_inputPeerUser        0xdde8a54c
#define TL_inputPeerChat        0x35a95cb9
#define TL_inputPeerChannel     0x27bcbbfc
#define TL_inputPeerSelf        0x7da07ec9
#define TL_inputPeerEmpty       0x7f3b18ea

/* Config */
#define TL_config               0xcc1a241e
#define TL_dcOption             0x18b7a10d
#define TL_nearestDc            0x8e1a1775

/* Bool */
#define TL_boolTrue             0x997275b5
#define TL_boolFalse            0xbc799737

/* Vector */
#define TL_vector               0x1cb5c415

/**
 * @brief Registry entry mapping constructor ID to name.
 */
typedef struct {
    uint32_t id;
    const char *name;
} TlRegistryEntry;

/**
 * @brief Look up a constructor name by ID.
 * @param id Constructor ID.
 * @return Name string (static), or "unknown" if not found.
 */
const char *tl_constructor_name(uint32_t id);

/**
 * @brief Check if a constructor ID is a known type.
 * @param id Constructor ID.
 * @return 1 if known, 0 if unknown.
 */
int tl_constructor_known(uint32_t id);

#endif /* TL_REGISTRY_H */
