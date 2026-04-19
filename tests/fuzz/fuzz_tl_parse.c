/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file fuzz_tl_parse.c
 * @brief libFuzzer harness for TL binary parser (tl_serial + tl_skip).
 *
 * Build with clang + libFuzzer:
 *   cmake -DENABLE_FUZZ=ON -DCMAKE_C_COMPILER=clang ..
 *   cmake --build . --target fuzz-tl-parse
 *
 * Or use the convenience wrapper:
 *   ./manage.sh fuzz
 *
 * The harness feeds arbitrary bytes into every public reader entry point
 * and a representative selection of tl_skip helpers.  The contract is
 * simple: no crash, no sanitizer trip.  Return-value errors (-1 / NULL)
 * are expected and explicitly tolerated.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tl_serial.h"
#include "tl_skip.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* ---- tl_serial reader entry points ---- */

    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_uint32(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_int32(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_int64(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_uint64(&r);
    }
    {
        unsigned char buf128[16];
        TlReader r = tl_reader_init(data, size);
        tl_read_int128(&r, buf128);
    }
    {
        unsigned char buf256[32];
        TlReader r = tl_reader_init(data, size);
        tl_read_int256(&r, buf256);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_double(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_read_bool(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        char *s = tl_read_string(&r);
        free(s);
    }
    {
        size_t out_len = 0;
        TlReader r = tl_reader_init(data, size);
        unsigned char *b = tl_read_bytes(&r, &out_len);
        free(b);
    }

    /* ---- tl_skip helpers ---- */

    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_bool(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_string(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_peer(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_peer_notify_settings(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_draft_message(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_entity(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_entities_vector(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_fwd_header(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_reply_markup(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_reactions(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_replies(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_factcheck(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_reply_header(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_photo_size(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_photo_size_vector(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_photo(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_document(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_media(&r);
    }
    {
        MediaInfo mi = {0};
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message_media_ex(&r, &mi);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_chat_photo(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_user_profile_photo(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_user_status(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_restriction_reason_vector(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_username_vector(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_emoji_status(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_chat(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_user(&r);
    }
    {
        TlReader r = tl_reader_init(data, size);
        (void)tl_skip_message(&r);
    }
    {
        ChatSummary cs = {0};
        TlReader r = tl_reader_init(data, size);
        (void)tl_extract_chat(&r, &cs);
    }
    {
        UserSummary us = {0};
        TlReader r = tl_reader_init(data, size);
        (void)tl_extract_user(&r, &us);
    }

    return 0;
}
