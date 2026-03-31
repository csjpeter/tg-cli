/**
 * @file mtproto_session.c
 * @brief MTProto session state management.
 */

#include "mtproto_session.h"
#include "crypto.h"
#include "raii.h"
#include "fs_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Initialize a new MTProto session with a random session_id.
 * @param s Session to initialize.
 */
void mtproto_session_init(MtProtoSession *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));

    /* Random session_id */
    crypto_rand_bytes((unsigned char *)&s->session_id, sizeof(s->session_id));
    /* Ensure non-zero */
    if (s->session_id == 0) s->session_id = 1;
}

/**
 * @brief Generate the next monotonically increasing message ID.
 *
 * Message IDs are approximately unix_time * 2^32, must be divisible by 4
 * (client→server), and strictly increasing.
 *
 * @param s Session.
 * @return Next message ID, or 0 if s is NULL.
 */
uint64_t mtproto_session_next_msg_id(MtProtoSession *s) {
    if (!s) return 0;

    /* msg_id ≈ unix_time * 2^32, must be even and monotonically increasing */
    uint64_t now = (uint64_t)time(NULL);
    uint64_t msg_id = (now << 32) | ((uint64_t)rand() & 0xFFFFFFFC);

    /* Ensure monotonic increase */
    if (msg_id <= s->last_msg_id) {
        msg_id = s->last_msg_id + 4;
    }

    /* Client→server: msg_id mod 4 == 0 */
    msg_id &= ~(uint64_t)3;

    s->last_msg_id = msg_id;
    return msg_id;
}

/**
 * @brief Get the next sequence number.
 *
 * For content-related messages, increments the internal counter.
 *
 * @param s Session.
 * @param content_related 1 for RPC calls, 0 for acks/pings.
 * @return Next sequence number, or 0 if s is NULL.
 */
uint32_t mtproto_session_next_seq_no(MtProtoSession *s, int content_related) {
    if (!s) return 0;
    uint32_t result = s->seq_no * 2 + (content_related ? 1 : 0);
    if (content_related) s->seq_no++;
    return result;
}

/**
 * @brief Set the 256-byte authorization key on the session.
 * @param s   Session.
 * @param key 256-byte key to copy.
 */
void mtproto_session_set_auth_key(MtProtoSession *s, const uint8_t key[256]) {
    if (!s || !key) return;
    memcpy(s->auth_key, key, 256);
    s->has_auth_key = 1;
}

/**
 * @brief Set the server salt.
 * @param s    Session.
 * @param salt Server salt value.
 */
void mtproto_session_set_salt(MtProtoSession *s, uint64_t salt) {
    if (!s) return;
    s->server_salt = salt;
}

/**
 * @brief Save the auth key to a file with mode 0600.
 *
 * @param s    Session (must have auth_key).
 * @param path File path to write.
 * @return 0 on success, -1 on error.
 */
int mtproto_session_save_auth_key(const MtProtoSession *s, const char *path) {
    if (!s || !path || !s->has_auth_key) return -1;
    RAII_FILE FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* Set restrictive permissions (auth key is sensitive) */
    if (fs_ensure_permissions(path, 0600) != 0) {
        /* Non-fatal but log-worthy — file was already created */
    }

    size_t written = fwrite(s->auth_key, 1, 256, f);
    return (written == 256) ? 0 : -1;
}

/**
 * @brief Load an auth key from a file.
 *
 * @param s    Session (auth_key and has_auth_key will be set on success).
 * @param path File path to read.
 * @return 0 on success, -1 on error (file not found, too short, etc.).
 */
int mtproto_session_load_auth_key(MtProtoSession *s, const char *path) {
    if (!s || !path) return -1;
    RAII_FILE FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t nread = fread(s->auth_key, 1, 256, f);
    if (nread != 256) return -1;
    s->has_auth_key = 1;
    return 0;
}
