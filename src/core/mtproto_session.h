/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file mtproto_session.h
 * @brief MTProto session state — msg_id, seq_no, salt, auth_key management.
 */

#ifndef MTPROTO_SESSION_H
#define MTPROTO_SESSION_H

#include <stddef.h>
#include <stdint.h>

#define MTPROTO_AUTH_KEY_SIZE 256

typedef struct {
    uint64_t session_id;
    uint64_t server_salt;
    uint32_t seq_no;
    uint64_t last_msg_id;
    uint8_t  auth_key[MTPROTO_AUTH_KEY_SIZE];
    int      has_auth_key;
} MtProtoSession;

/** Initialize session with random session_id. */
void mtproto_session_init(MtProtoSession *s);

/** Assign a new random session_id and reset seqno counters.
 *  Keeps auth_key and server_salt intact.  Call after restoring a saved
 *  session before reconnecting so the server starts fresh seqno tracking. */
void mtproto_session_renew_id(MtProtoSession *s);

/** Generate next msg_id (monotonically increasing, even). */
uint64_t mtproto_session_next_msg_id(MtProtoSession *s);

/** Get next seq_no. content_related=1 for RPC calls. */
uint32_t mtproto_session_next_seq_no(MtProtoSession *s, int content_related);

/** Set the 256-byte auth key. */
void mtproto_session_set_auth_key(MtProtoSession *s, const uint8_t key[MTPROTO_AUTH_KEY_SIZE]);

/** Set server salt. */
void mtproto_session_set_salt(MtProtoSession *s, uint64_t salt);

/** Save auth key to file. Returns 0 on success. */
int mtproto_session_save_auth_key(const MtProtoSession *s, const char *path);

/** Load auth key from file. Returns 0 on success. */
int mtproto_session_load_auth_key(MtProtoSession *s, const char *path);

#endif /* MTPROTO_SESSION_H */
