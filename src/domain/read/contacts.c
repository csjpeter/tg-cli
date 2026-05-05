/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/contacts.c
 */

#include "domain/read/contacts.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_contacts_getContacts 0x5dd69e12u
#define CRC_contact              0x145ade0bu

/* Copy at most dst_cap-1 bytes from src (NUL-terminates). */
static void copy_str(char *dst, size_t dst_cap, const char *src) {
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t n = strlen(src);
    if (n >= dst_cap) n = dst_cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Parse one User/UserEmpty from @r; fill id + name fields on @e.
 * Unknown constructors are skipped via tl_skip_object. */
static void parse_user_into(TlReader *r, ContactEntry *entries, int count) {
    if (!tl_reader_ok(r)) return;
    uint32_t crc = tl_read_uint32(r);

    if (crc == TL_userEmpty) {
        /* userEmpty#d3bc4b7a id:long */
        int64_t id = tl_read_int64(r);
        (void)id;
        return;
    }
    if (crc != TL_user && crc != TL_user2) {
        logger_log(LOG_WARN, "contacts: unknown User 0x%08x — skipping", crc);
        return;
    }

    uint32_t flags  = tl_read_uint32(r);
    (void)tl_read_uint32(r); /* flags2 */
    int64_t id      = tl_read_int64(r);

    int64_t access_hash = 0;
    if (flags & (1u << 0)) access_hash = tl_read_int64(r);

    char first_name[64] = {0};
    char last_name[64]  = {0};
    char username[64]   = {0};

    if (flags & (1u << 1)) { RAII_STRING char *s = tl_read_string(r); copy_str(first_name, sizeof(first_name), s); }
    if (flags & (1u << 2)) { RAII_STRING char *s = tl_read_string(r); copy_str(last_name,  sizeof(last_name),  s); }
    if (flags & (1u << 3)) { RAII_STRING char *s = tl_read_string(r); copy_str(username,   sizeof(username),   s); }

    /* Match by user_id and fill name fields. */
    for (int i = 0; i < count; i++) {
        if (entries[i].user_id == id) {
            entries[i].access_hash = access_hash;
            copy_str(entries[i].first_name, sizeof(entries[i].first_name), first_name);
            copy_str(entries[i].last_name,  sizeof(entries[i].last_name),  last_name);
            copy_str(entries[i].username,   sizeof(entries[i].username),   username);
            break;
        }
    }
}

int domain_get_contacts(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         ContactEntry *out, int max_entries, int *out_count) {
    if (!cfg || !s || !t || !out || !out_count || max_entries <= 0) return -1;
    *out_count = 0;
    if (max_entries > CONTACTS_MAX) max_entries = CONTACTS_MAX;

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_contacts_getContacts);
    tl_write_int64 (&w, 0);  /* hash = 0 → server always returns full list */
    uint8_t query[32];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(65536);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 65536, &resp_len) != 0) return -1;
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "contacts: RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }
    if (top == TL_contacts_contactsNotModified) {
        return 0;
    }
    if (top != TL_contacts_contacts) {
        logger_log(LOG_ERROR, "contacts: unexpected top 0x%08x", top);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    tl_read_uint32(&r); /* top constructor */

    /* contacts:Vector<Contact> */
    uint32_t vec = tl_read_uint32(&r);
    if (vec != TL_vector) return -1;
    uint32_t count = tl_read_uint32(&r);
    int written = 0;
    for (uint32_t i = 0; i < count && written < max_entries; i++) {
        if (!tl_reader_ok(&r)) break;
        uint32_t ccrc = tl_read_uint32(&r);
        if (ccrc != CRC_contact) {
            logger_log(LOG_WARN,
                       "contacts: unknown Contact 0x%08x — stopping", ccrc);
            break;
        }
        ContactEntry e;
        memset(&e, 0, sizeof(e));
        e.user_id = tl_read_int64(&r);
        int b = tl_read_bool(&r);
        e.mutual = (b == 1) ? 1 : 0;
        out[written++] = e;
    }
    *out_count = written;

    /* saved_count:int */
    tl_read_int32(&r);

    /* users:Vector<User> — enriches the entries already written */
    uint32_t uvec = tl_read_uint32(&r);
    if (uvec == TL_vector) {
        uint32_t ucount = tl_read_uint32(&r);
        for (uint32_t i = 0; i < ucount; i++) {
            if (!tl_reader_ok(&r)) break;
            parse_user_into(&r, out, written);
        }
    }

    return 0;
}
