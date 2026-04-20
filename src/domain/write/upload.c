/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/upload.c
 * @brief upload.saveFilePart + messages.sendMedia (US-P6-02).
 */

#include "domain/write/upload.h"

#include "app/dc_session.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "crypto.h"
#include "logger.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CRC_upload_saveFilePart           0xb304a621u
#define CRC_upload_saveBigFilePart        0xde7b673du
#define CRC_messages_sendMedia            0x7547c966u
#define CRC_inputFile                     0xf52ff27fu
#define CRC_inputFileBig                  0xfa4f0bb5u
#define CRC_inputMediaUploadedDocument    0x5b38c6c1u
#define CRC_inputMediaUploadedPhoto       0x1e287d04u
#define CRC_documentAttributeFilename     0x15590068u

static int write_input_peer(TlWriter *w, const HistoryPeer *p) {
    switch (p->kind) {
    case HISTORY_PEER_SELF:
        tl_write_uint32(w, TL_inputPeerSelf); return 0;
    case HISTORY_PEER_USER:
        tl_write_uint32(w, TL_inputPeerUser);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash); return 0;
    case HISTORY_PEER_CHAT:
        tl_write_uint32(w, TL_inputPeerChat);
        tl_write_int64 (w, p->peer_id); return 0;
    case HISTORY_PEER_CHANNEL:
        tl_write_uint32(w, TL_inputPeerChannel);
        tl_write_int64 (w, p->peer_id);
        tl_write_int64 (w, p->access_hash); return 0;
    default: return -1;
    }
}

/* Send one upload.saveFilePart (small) or upload.saveBigFilePart (big).
 * Returns 0 on boolTrue, -1 on error. When @p out_migrate_dc is non-NULL,
 * the server's migrate_dc hint (FILE_MIGRATE_X / NETWORK_MIGRATE_X) is
 * written there so the caller can switch DCs. */
static int save_part(const ApiConfig *cfg,
                      MtProtoSession *s, Transport *t,
                      int is_big,
                      int64_t file_id, int32_t part_idx, int32_t total_parts,
                      const uint8_t *bytes, size_t len,
                      int *out_migrate_dc) {
    if (out_migrate_dc) *out_migrate_dc = 0;

    TlWriter w; tl_writer_init(&w);
    if (is_big) {
        tl_write_uint32(&w, CRC_upload_saveBigFilePart);
        tl_write_int64 (&w, file_id);
        tl_write_int32 (&w, part_idx);
        tl_write_int32 (&w, total_parts);
        tl_write_bytes (&w, bytes, len);
    } else {
        tl_write_uint32(&w, CRC_upload_saveFilePart);
        tl_write_int64 (&w, file_id);
        tl_write_int32 (&w, part_idx);
        tl_write_bytes (&w, bytes, len);
    }

    RAII_STRING uint8_t *query = (uint8_t *)malloc(w.len);
    if (!query) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    uint8_t resp[256]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0)
        return -1;
    if (resp_len < 4) return -1;
    uint32_t top; memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        RpcError perr; rpc_parse_error(resp, resp_len, &perr);
        if (out_migrate_dc && perr.migrate_dc > 0)
            *out_migrate_dc = perr.migrate_dc;
        logger_log(LOG_ERROR, "upload: save%sFilePart RPC %d: %s (migrate=%d)",
                   is_big ? "Big" : "",
                   perr.error_code, perr.error_msg, perr.migrate_dc);
        return -1;
    }
    if (top != TL_boolTrue) {
        logger_log(LOG_ERROR, "upload: unexpected save%sFilePart reply 0x%08x",
                   is_big ? "Big" : "", top);
        return -1;
    }
    return 0;
}

/* Upload @p fp fully on the given (s, t). Caller must have rewound @p fp.
 * On NETWORK_MIGRATE_X / FILE_MIGRATE_X at any part, writes the target DC
 * to @p out_migrate_dc and returns -1 without continuing. */
static int upload_all_parts(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             FILE *fp, int is_big,
                             int64_t total,
                             int64_t file_id, int32_t total_parts,
                             uint8_t *chunk,
                             int32_t *out_part_count,
                             int *out_migrate_dc) {
    if (out_migrate_dc) *out_migrate_dc = 0;
    if (fseek(fp, 0, SEEK_SET) != 0) return -1;

    int32_t part_idx = 0;
    int64_t done = 0;
    while (done < total) {
        int64_t want = total - done;
        if (want > UPLOAD_CHUNK_SIZE) want = UPLOAD_CHUNK_SIZE;
        size_t got = fread(chunk, 1, (size_t)want, fp);
        if ((int64_t)got != want) {
            logger_log(LOG_ERROR, "upload: short read at part %d", part_idx);
            return -1;
        }
        int migrate = 0;
        if (save_part(cfg, s, t, is_big, file_id, part_idx, total_parts,
                        chunk, got, &migrate) != 0) {
            if (out_migrate_dc && migrate > 0) *out_migrate_dc = migrate;
            logger_log(LOG_ERROR, "upload: part %d failed (migrate=%d)",
                       part_idx, migrate);
            return -1;
        }
        done += (int64_t)got;
        part_idx++;
    }
    if (out_part_count) *out_part_count = part_idx;
    return 0;
}

/* Extract the basename of a path, e.g. "/a/b/c.jpg" → "c.jpg". */
static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int domain_path_is_image(const char *path) {
    if (!path) return 0;
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    const char *ext = dot + 1;
    /* Case-insensitive extension match. */
    char buf[8] = {0};
    size_t n = strlen(ext);
    if (n == 0 || n >= sizeof(buf)) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = (unsigned char)ext[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        buf[i] = (char)c;
    }
    return (strcmp(buf, "jpg")  == 0 ||
            strcmp(buf, "jpeg") == 0 ||
            strcmp(buf, "png")  == 0 ||
            strcmp(buf, "webp") == 0 ||
            strcmp(buf, "gif")  == 0) ? 1 : 0;
}

/* Chunked upload step: open @p file_path, generate a random file_id,
 * push every part via upload_all_parts (with cross-DC migration), and
 * hand the resulting (file_id, part_count, is_big) back to the caller
 * so it can compose the second-phase messages.sendMedia. */
typedef struct {
    int64_t  file_id;
    int32_t  part_count;
    int      is_big;
    char     file_name[256];
} UploadedFile;

static int upload_chunk_phase(const ApiConfig *cfg,
                               MtProtoSession *s, Transport *t,
                               const char *file_path,
                               UploadedFile *uf) {
    struct stat st;
    if (stat(file_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        logger_log(LOG_ERROR, "upload: cannot stat %s", file_path);
        return -1;
    }
    if (st.st_size <= 0 || (int64_t)st.st_size > UPLOAD_MAX_SIZE) {
        logger_log(LOG_ERROR,
                   "upload: size %lld out of supported range (1..%lld)",
                   (long long)st.st_size, (long long)UPLOAD_MAX_SIZE);
        return -1;
    }
    int is_big = ((int64_t)st.st_size >= UPLOAD_BIG_THRESHOLD);
    int64_t total = (int64_t)st.st_size;
    int32_t total_parts = (int32_t)((total + UPLOAD_CHUNK_SIZE - 1)
                                     / UPLOAD_CHUNK_SIZE);

    RAII_FILE FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        logger_log(LOG_ERROR, "upload: cannot open %s", file_path);
        return -1;
    }

    uint8_t id_buf[8] = {0};
    int64_t file_id = 0;
    if (crypto_rand_bytes(id_buf, 8) != 0) return -1;
    memcpy(&file_id, id_buf, 8);

    RAII_STRING uint8_t *chunk = (uint8_t *)malloc(UPLOAD_CHUNK_SIZE);
    if (!chunk) return -1;

    int32_t part_idx = 0;
    int home_migrate = 0;
    if (upload_all_parts(cfg, s, t, fp, is_big, total,
                          file_id, total_parts, chunk,
                          &part_idx, &home_migrate) != 0) {
        if (home_migrate <= 0) {
            logger_log(LOG_ERROR, "upload: non-migrate failure");
            return -1;
        }
        logger_log(LOG_INFO,
                   "upload: NETWORK/FILE_MIGRATE_%d, retrying on DC%d",
                   home_migrate, home_migrate);

        DcSession xdc;
        if (dc_session_open(home_migrate, &xdc) != 0) return -1;
        if (dc_session_ensure_authorized(&xdc, cfg, s, t) != 0) {
            dc_session_close(&xdc);
            return -1;
        }

        uint8_t id2_buf[8] = {0};
        if (crypto_rand_bytes(id2_buf, 8) != 0) {
            dc_session_close(&xdc); return -1;
        }
        memcpy(&file_id, id2_buf, 8);

        int fdc_migrate = 0;
        int rc2 = upload_all_parts(cfg, &xdc.session, &xdc.transport, fp,
                                    is_big, total, file_id, total_parts,
                                    chunk, &part_idx, &fdc_migrate);
        dc_session_close(&xdc);
        if (rc2 != 0) {
            logger_log(LOG_ERROR, "upload: retry on DC%d also failed",
                       home_migrate);
            return -1;
        }
    }

    uf->file_id    = file_id;
    uf->part_count = part_idx;
    uf->is_big     = is_big;
    const char *bn = basename_of(file_path);
    size_t bn_n = strlen(bn);
    if (bn_n >= sizeof(uf->file_name)) bn_n = sizeof(uf->file_name) - 1;
    memcpy(uf->file_name, bn, bn_n);
    uf->file_name[bn_n] = '\0';
    return 0;
}

/* Serialise an InputFile / InputFileBig reference body into @p w. */
static void write_input_file(TlWriter *w, const UploadedFile *uf) {
    if (uf->is_big) {
        tl_write_uint32(w, CRC_inputFileBig);
        tl_write_int64 (w, uf->file_id);
        tl_write_int32 (w, uf->part_count);
        tl_write_string(w, uf->file_name);
    } else {
        tl_write_uint32(w, CRC_inputFile);
        tl_write_int64 (w, uf->file_id);
        tl_write_int32 (w, uf->part_count);
        tl_write_string(w, uf->file_name);
        tl_write_string(w, "");                   /* md5_checksum */
    }
}

/* Dispatch messages.sendMedia and drain the response. */
static int send_media_call(const ApiConfig *cfg,
                            MtProtoSession *s, Transport *t,
                            const uint8_t *query, size_t qlen,
                            RpcError *err) {
    uint8_t resp[4096]; size_t resp_len = 0;
    if (api_call(cfg, s, t, query, qlen, resp, sizeof(resp), &resp_len) != 0) {
        logger_log(LOG_ERROR, "upload: sendMedia api_call failed");
        return -1;
    }
    if (resp_len < 4) return -1;
    uint32_t top; memcpy(&top, resp, 4);
    if (top == TL_rpc_error) {
        if (err) rpc_parse_error(resp, resp_len, err);
        return -1;
    }
    if (top == TL_updates || top == TL_updatesCombined
        || top == TL_updateShort) {
        return 0;
    }
    logger_log(LOG_WARN, "upload: unexpected sendMedia top 0x%08x", top);
    return 0;
}

int domain_send_file(const ApiConfig *cfg,
                      MtProtoSession *s, Transport *t,
                      const HistoryPeer *peer,
                      const char *file_path,
                      const char *caption,
                      const char *mime_type,
                      RpcError *err) {
    if (!cfg || !s || !t || !peer || !file_path) return -1;
    if (!mime_type) mime_type = "application/octet-stream";
    if (!caption)   caption   = "";

    UploadedFile uf = {0};
    if (upload_chunk_phase(cfg, s, t, file_path, &uf) != 0) return -1;

    uint8_t rand_rnd[8] = {0};
    int64_t random_id = 0;
    if (crypto_rand_bytes(rand_rnd, 8) == 0) memcpy(&random_id, rand_rnd, 8);

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_sendMedia);
    tl_write_uint32(&w, 0);                       /* flags = 0 */
    if (write_input_peer(&w, peer) != 0) {
        tl_writer_free(&w);
        return -1;
    }
    /* media: InputMediaUploadedDocument */
    tl_write_uint32(&w, CRC_inputMediaUploadedDocument);
    tl_write_uint32(&w, 0);                       /* inner flags */
    write_input_file(&w, &uf);
    tl_write_string(&w, mime_type);
    /* attributes: Vector<DocumentAttribute> with a single filename. */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 1);
    tl_write_uint32(&w, CRC_documentAttributeFilename);
    tl_write_string(&w, uf.file_name);

    tl_write_string(&w, caption);                 /* message */
    tl_write_int64 (&w, random_id);

    RAII_STRING uint8_t *query = (uint8_t *)malloc(w.len);
    if (!query) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    return send_media_call(cfg, s, t, query, qlen, err);
}

int domain_send_photo(const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       const HistoryPeer *peer,
                       const char *file_path,
                       const char *caption,
                       RpcError *err) {
    if (!cfg || !s || !t || !peer || !file_path) return -1;
    if (!caption) caption = "";

    UploadedFile uf = {0};
    if (upload_chunk_phase(cfg, s, t, file_path, &uf) != 0) return -1;

    uint8_t rand_rnd[8] = {0};
    int64_t random_id = 0;
    if (crypto_rand_bytes(rand_rnd, 8) == 0) memcpy(&random_id, rand_rnd, 8);

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_sendMedia);
    tl_write_uint32(&w, 0);                       /* outer flags = 0 */
    if (write_input_peer(&w, peer) != 0) {
        tl_writer_free(&w);
        return -1;
    }
    /* media: InputMediaUploadedPhoto#1e287d04 flags:# spoiler:flags.2?true
     *   file:InputFile stickers:flags.0?Vector<InputDocument>
     *   ttl_seconds:flags.1?int
     * All optional flags are clear here; server rescales. */
    tl_write_uint32(&w, CRC_inputMediaUploadedPhoto);
    tl_write_uint32(&w, 0);                       /* inner flags */
    write_input_file(&w, &uf);

    tl_write_string(&w, caption);
    tl_write_int64 (&w, random_id);

    RAII_STRING uint8_t *query = (uint8_t *)malloc(w.len);
    if (!query) { tl_writer_free(&w); return -1; }
    memcpy(query, w.data, w.len);
    size_t qlen = w.len;
    tl_writer_free(&w);

    return send_media_call(cfg, s, t, query, qlen, err);
}
