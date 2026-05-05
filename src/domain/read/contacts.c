/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/contacts.c
 */

#include "domain/read/contacts.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_contacts_getContacts 0x5dd69e12u
#define CRC_contact              0x145ade0bu

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

    /* users:Vector<User> — tl_extract_user advances past the FULL object
     * so the cursor is correctly positioned for the next user each iteration. */
    uint32_t uvec = tl_read_uint32(&r);
    if (uvec == TL_vector) {
        uint32_t ucount = tl_read_uint32(&r);
        for (uint32_t i = 0; i < ucount; i++) {
            if (!tl_reader_ok(&r)) break;
            UserSummary us = {0};
            if (tl_extract_user(&r, &us) != 0) {
                logger_log(LOG_WARN, "contacts: user parse failed at index %u", i);
                break;
            }
            for (int j = 0; j < written; j++) {
                if (out[j].user_id == us.id) {
                    out[j].access_hash = us.access_hash;
                    size_t n = strlen(us.name);
                    if (n >= sizeof(out[j].name)) n = sizeof(out[j].name) - 1;
                    memcpy(out[j].name, us.name, n);
                    out[j].name[n] = '\0';
                    n = strlen(us.username);
                    if (n >= sizeof(out[j].username)) n = sizeof(out[j].username) - 1;
                    memcpy(out[j].username, us.username, n);
                    out[j].username[n] = '\0';
                    break;
                }
            }
        }
    }

    return 0;
}
