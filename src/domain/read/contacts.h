/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/contacts.h
 * @brief US-09 / P7-01 — list contacts.getContacts.
 */

#ifndef DOMAIN_READ_CONTACTS_H
#define DOMAIN_READ_CONTACTS_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stdint.h>

#define CONTACTS_MAX 512

typedef struct {
    int64_t user_id;
    int64_t access_hash;
    int     mutual;
    char    name[128];     /**< "First Last" joined; empty if server withholds. */
    char    username[64];
} ContactEntry;

/**
 * @brief Fetch the user's contact list.
 *
 * @param cfg       API config.
 * @param s         Session with auth_key.
 * @param t         Connected transport.
 * @param out       Output array of at least @p max_entries slots.
 * @param max_entries Maximum entries to write (clamp 1..CONTACTS_MAX).
 * @param out_count Receives entries written.
 * @return 0 on success, -1 on RPC / parse error.
 */
int domain_get_contacts(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         ContactEntry *out, int max_entries, int *out_count);

#endif /* DOMAIN_READ_CONTACTS_H */
