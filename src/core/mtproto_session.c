/**
 * @file mtproto_session.c
 * @brief MTProto session state management.
 */

#include "mtproto_session.h"
#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void mtproto_session_init(MtProtoSession *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));

    /* Random session_id */
    crypto_rand_bytes((unsigned char *)&s->session_id, sizeof(s->session_id));
    /* Ensure non-zero */
    if (s->session_id == 0) s->session_id = 1;
}

uint64_t mtproto_session_next_msg_id(MtProtoSession *s) {
    if (!s) return 0;

    /* msg_id ≈ unix_time * 2^32, must be even and monotonically increasing */
    uint64_t now = (uint64_t)time(NULL);
    uint64_t msg_id = (now << 32) | ((uint64_t)rand() & 0xFFFFFFFC);

    /* Ensure monotonic increase */
    if (msg_id <= s->last_msg_id) {
        msg_id = s->last_msg_id + 4;
    }

    /* Ensure even */
    msg_id &= ~(uint64_t)3;
    /* Set mod 4 = 0 for client messages (even) */
    /* Actually: client→server even, server→client odd.
       Client sends: msg_id mod 4 == 0 */

    s->last_msg_id = msg_id;
    return msg_id;
}

uint32_t mtproto_session_next_seq_no(MtProtoSession *s, int content_related) {
    if (!s) return 0;
    uint32_t result = s->seq_no * 2 + (content_related ? 1 : 0);
    if (content_related) s->seq_no++;
    return result;
}

void mtproto_session_set_auth_key(MtProtoSession *s, const uint8_t key[256]) {
    if (!s || !key) return;
    memcpy(s->auth_key, key, 256);
    s->has_auth_key = 1;
}

void mtproto_session_set_salt(MtProtoSession *s, uint64_t salt) {
    if (!s) return;
    s->server_salt = salt;
}

int mtproto_session_save_auth_key(const MtProtoSession *s, const char *path) {
    if (!s || !path || !s->has_auth_key) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(s->auth_key, 1, 256, f);
    fclose(f);
    return (written == 256) ? 0 : -1;
}

int mtproto_session_load_auth_key(MtProtoSession *s, const char *path) {
    if (!s || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t read = fread(s->auth_key, 1, 256, f);
    fclose(f);
    if (read != 256) return -1;
    s->has_auth_key = 1;
    return 0;
}
