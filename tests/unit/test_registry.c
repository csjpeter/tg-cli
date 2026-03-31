/**
 * @file test_registry.c
 * @brief Unit tests for TL constructor registry.
 */

#include "test_helpers.h"
#include "tl_registry.h"

#include <string.h>

void test_registry_known_constructors(void) {
    ASSERT(tl_constructor_known(TL_rpc_result), "rpc_result should be known");
    ASSERT(tl_constructor_known(TL_rpc_error), "rpc_error should be known");
    ASSERT(tl_constructor_known(TL_gzip_packed), "gzip_packed should be known");
    ASSERT(tl_constructor_known(TL_msg_container), "msg_container should be known");
    ASSERT(tl_constructor_known(TL_user), "user should be known");
    ASSERT(tl_constructor_known(TL_message), "message should be known");
    ASSERT(tl_constructor_known(TL_channel), "channel should be known");
    ASSERT(tl_constructor_known(TL_peerUser), "peerUser should be known");
    ASSERT(tl_constructor_known(TL_config), "config should be known");
    ASSERT(tl_constructor_known(TL_boolTrue), "boolTrue should be known");
}

void test_registry_unknown_constructor(void) {
    ASSERT(!tl_constructor_known(0x00000000), "0x00000000 should be unknown");
    ASSERT(!tl_constructor_known(0xFFFFFFFF), "0xFFFFFFFF should be unknown");
    ASSERT(!tl_constructor_known(0xDEADBEEF), "0xDEADBEEF should be unknown");
}

void test_registry_name_lookup(void) {
    ASSERT(strcmp(tl_constructor_name(TL_rpc_result), "rpc_result") == 0,
           "rpc_result name");
    ASSERT(strcmp(tl_constructor_name(TL_user), "user") == 0,
           "user name");
    ASSERT(strcmp(tl_constructor_name(TL_message), "message") == 0,
           "message name");
    ASSERT(strcmp(tl_constructor_name(TL_gzip_packed), "gzip_packed") == 0,
           "gzip_packed name");
    ASSERT(strcmp(tl_constructor_name(TL_peerChannel), "peerChannel") == 0,
           "peerChannel name");
}

void test_registry_unknown_name(void) {
    ASSERT(strcmp(tl_constructor_name(0xDEADBEEF), "unknown") == 0,
           "unknown constructor should return 'unknown'");
}

void test_registry_auth_constructors(void) {
    ASSERT(tl_constructor_known(TL_resPQ), "resPQ");
    ASSERT(tl_constructor_known(TL_server_DH_params_ok), "server_DH_params_ok");
    ASSERT(tl_constructor_known(TL_dh_gen_ok), "dh_gen_ok");
    ASSERT(tl_constructor_known(TL_auth_sentCode), "auth.sentCode");
    ASSERT(tl_constructor_known(TL_auth_authorization), "auth.authorization");
}

void test_registry_update_constructors(void) {
    ASSERT(tl_constructor_known(TL_updates_state), "updates.state");
    ASSERT(tl_constructor_known(TL_updates_difference), "updates.difference");
    ASSERT(tl_constructor_known(TL_updateNewMessage), "updateNewMessage");
    ASSERT(tl_constructor_known(TL_updateShort), "updateShort");
}

void test_registry_media_constructors(void) {
    ASSERT(tl_constructor_known(TL_messageMediaPhoto), "messageMediaPhoto");
    ASSERT(tl_constructor_known(TL_messageMediaDocument), "messageMediaDocument");
    ASSERT(tl_constructor_known(TL_messageMediaGeo), "messageMediaGeo");
    ASSERT(tl_constructor_known(TL_messageMediaContact), "messageMediaContact");
    ASSERT(tl_constructor_known(TL_document), "document");
    ASSERT(tl_constructor_known(TL_photo), "photo");
}

void test_registry(void) {
    RUN_TEST(test_registry_known_constructors);
    RUN_TEST(test_registry_unknown_constructor);
    RUN_TEST(test_registry_name_lookup);
    RUN_TEST(test_registry_unknown_name);
    RUN_TEST(test_registry_auth_constructors);
    RUN_TEST(test_registry_update_constructors);
    RUN_TEST(test_registry_media_constructors);
}
