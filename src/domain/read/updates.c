/**
 * @file domain/read/updates.c
 * @brief updates.getState / updates.getDifference minimal parsers.
 */

#include "domain/read/updates.h"

#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "logger.h"
#include "raii.h"

#include <stdlib.h>
#include <string.h>

#define CRC_updates_getState      0xedd4882au
#define CRC_updates_getDifference 0x19c2f763u
#define CRC_updates_differenceTooLong 0x4afe8f6du

static int send_trivial(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         uint32_t method_crc,
                         uint8_t *resp, size_t resp_cap, size_t *resp_len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, method_crc);
    uint8_t query[32];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    if (api_call(cfg, s, t, query, qlen, resp, resp_cap, resp_len) != 0) {
        logger_log(LOG_ERROR, "updates: api_call failed");
        return -1;
    }
    return 0;
}

static int parse_state(TlReader *r, UpdatesState *out) {
    uint32_t crc = tl_read_uint32(r);
    if (crc != TL_updates_state) {
        logger_log(LOG_ERROR, "updates: not updates.state (0x%08x)", crc);
        return -1;
    }
    out->pts  = tl_read_int32(r);
    out->qts  = tl_read_int32(r);
    out->date = tl_read_int32(r);
    out->seq  = tl_read_int32(r);
    out->unread_count = tl_read_int32(r);
    return 0;
}

int domain_updates_state(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          UpdatesState *out) {
    if (!cfg || !s || !t || !out) return -1;
    memset(out, 0, sizeof(*out));

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(4096);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (send_trivial(cfg, s, t, CRC_updates_getState,
                      resp, 4096, &resp_len) != 0) return -1;
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "updates.getState RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    return parse_state(&r, out);
}

int domain_updates_difference(const ApiConfig *cfg,
                               MtProtoSession *s, Transport *t,
                               const UpdatesState *in,
                               UpdatesDifference *out) {
    if (!cfg || !s || !t || !in || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->next_state = *in;

    /* Build getDifference(flags=0, pts, date, qts). */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_updates_getDifference);
    tl_write_uint32(&w, 0);                /* flags = 0 (no pts_total_limit) */
    tl_write_int32 (&w, in->pts);
    tl_write_int32 (&w, in->date);
    tl_write_int32 (&w, in->qts);

    uint8_t query[64];
    if (w.len > sizeof(query)) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    RAII_STRING uint8_t *resp = (uint8_t *)malloc(262144);
    if (!resp) return -1;
    size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, 262144, &resp_len) != 0) return -1;
    if (resp_len < 4) return -1;

    uint32_t top;
    memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError err; rpc_parse_error(resp, resp_len, &err);
        logger_log(LOG_ERROR, "updates.getDifference RPC error %d: %s",
                   err.error_code, err.error_msg);
        return -1;
    }

    TlReader r = tl_reader_init(resp, resp_len);
    uint32_t crc = tl_read_uint32(&r);

    if (crc == TL_updates_differenceEmpty) {
        out->is_empty = 1;
        /* differenceEmpty: date:int seq:int */
        out->next_state.date = tl_read_int32(&r);
        out->next_state.seq  = tl_read_int32(&r);
        return 0;
    }
    if (crc == CRC_updates_differenceTooLong) {
        out->is_too_long = 1;
        /* Next call must reseed via updates.getState. */
        out->next_state.pts = tl_read_int32(&r);
        return 0;
    }
    if (crc != TL_updates_difference && crc != TL_updates_differenceSlice) {
        logger_log(LOG_ERROR, "updates: unexpected Difference 0x%08x", crc);
        return -1;
    }

    /* difference / differenceSlice both start with:
     *   new_messages:Vector<Message>
     *   new_encrypted_messages:Vector<EncryptedMessage>
     *   other_updates:Vector<Update>
     *   chats:Vector<Chat>
     *   users:Vector<User>
     *   state:updates.State            (only for plain difference)
     *   intermediate_state:updates.State (only for differenceSlice)
     *
     * We only read the top-level vector counts so we can report
     * activity. Actual message/update bodies need a per-element TL
     * walker which is deferred to v2.
     */
    uint32_t vec;
    /* new_messages */
    vec = tl_read_uint32(&r);
    if (vec != TL_vector) {
        logger_log(LOG_ERROR, "updates: expected Vector, got 0x%08x", vec);
        return -1;
    }
    out->new_messages_count = (int32_t)tl_read_uint32(&r);
    /* We cannot reliably step over each Message, so stop here. next_state
     * is left as the input state; the caller will need to re-seed via
     * getState for the next poll. */
    out->next_state = *in;
    (void)out->other_updates_count;
    return 0;
}
