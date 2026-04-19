/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/media.c
 * @brief upload.getFile chunked download (P6-01).
 */

#include "domain/read/media.h"

#include "app/dc_session.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mtproto_rpc.h"
#include "media_index.h"
#include "logger.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRC_upload_getFile                 0xbe5335beu
#define CRC_inputPhotoFileLocation         0x40181ffeu
#define CRC_inputDocumentFileLocation      0xbad07584u
#define CRC_upload_file                    0x096a18d5u
#define CRC_upload_fileCdnRedirect         0xf18cda44u

/** Default chunk size — must be a multiple of 4 KB per Telegram spec. */
#define CHUNK_SIZE (128 * 1024)

/* Build a upload.getFile request whose InputFileLocation is derived
 * from @p info.kind. Photos use inputPhotoFileLocation with the
 * largest thumb_size; documents use inputDocumentFileLocation with
 * an empty thumb_size to fetch the full file. */
static int build_getfile_request(const MediaInfo *info,
                                  int64_t offset, int32_t limit,
                                  uint8_t *out, size_t cap, size_t *out_len) {
    TlWriter w;
    tl_writer_init(&w);

    tl_write_uint32(&w, CRC_upload_getFile);
    tl_write_uint32(&w, 0);                             /* flags */
    if (info->kind == MEDIA_DOCUMENT) {
        /* inputDocumentFileLocation#bad07584 id:long access_hash:long
         *                                    file_reference:bytes thumb_size:string */
        tl_write_uint32(&w, CRC_inputDocumentFileLocation);
        tl_write_int64 (&w, info->document_id);
        tl_write_int64 (&w, info->access_hash);
        tl_write_bytes (&w, info->file_reference,
                            info->file_reference_len);
        tl_write_string(&w, "");                        /* full file */
    } else {
        /* inputPhotoFileLocation#40181ffe id:long access_hash:long
         *                                 file_reference:bytes thumb_size:string */
        tl_write_uint32(&w, CRC_inputPhotoFileLocation);
        tl_write_int64 (&w, info->photo_id);
        tl_write_int64 (&w, info->access_hash);
        tl_write_bytes (&w, info->file_reference,
                            info->file_reference_len);
        tl_write_string(&w, info->thumb_type[0] ? info->thumb_type : "y");
    }
    tl_write_int64 (&w, offset);
    tl_write_int32 (&w, limit);

    int rc = -1;
    if (w.len <= cap) {
        memcpy(out, w.data, w.len);
        *out_len = w.len;
        rc = 0;
    }
    tl_writer_free(&w);
    return rc;
}

/* Shared chunked download loop. Caller has already validated @p info. */
static int download_loop(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          const MediaInfo *info,
                          const char *out_path,
                          int *wrong_dc) {
    RAII_FILE FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        logger_log(LOG_ERROR, "media: cannot open %s for writing", out_path);
        return -1;
    }

    uint8_t query[1024];
    RAII_STRING uint8_t *resp = (uint8_t *)malloc(CHUNK_SIZE + 4096);
    if (!resp) return -1;

    int64_t offset = 0;
    for (;;) {
        size_t qlen = 0;
        if (build_getfile_request(info, offset, CHUNK_SIZE,
                                    query, sizeof(query), &qlen) != 0)
            return -1;

        size_t resp_len = 0;
        if (api_call(cfg, s, t, query, qlen,
                     resp, CHUNK_SIZE + 4096, &resp_len) != 0) {
            logger_log(LOG_ERROR, "media: api_call failed at offset %lld",
                       (long long)offset);
            return -1;
        }
        if (resp_len < 4) return -1;

        uint32_t top;
        memcpy(&top, resp, 4);
        if (top == TL_rpc_error) {
            RpcError err;
            rpc_parse_error(resp, resp_len, &err);
            if (err.migrate_dc > 0 && wrong_dc) *wrong_dc = err.migrate_dc;
            logger_log(LOG_ERROR, "media: RPC error %d: %s (migrate=%d)",
                       err.error_code, err.error_msg, err.migrate_dc);
            return -1;
        }
        if (top == CRC_upload_fileCdnRedirect) {
            logger_log(LOG_ERROR, "media: CDN redirect not supported");
            return -1;
        }
        if (top != CRC_upload_file) {
            logger_log(LOG_ERROR, "media: unexpected top 0x%08x", top);
            return -1;
        }

        TlReader r = tl_reader_init(resp, resp_len);
        tl_read_uint32(&r);                 /* top */
        tl_read_uint32(&r);                 /* storage.FileType crc */
        tl_read_int32(&r);                  /* mtime */
        size_t bytes_len = 0;
        RAII_STRING uint8_t *bytes = tl_read_bytes(&r, &bytes_len);
        if (!bytes && bytes_len != 0) return -1;

        if (bytes_len > 0) {
            if (fwrite(bytes, 1, bytes_len, fp) != bytes_len) {
                logger_log(LOG_ERROR, "media: fwrite failed");
                return -1;
            }
            offset += (int64_t)bytes_len;
        }

        if (bytes_len < CHUNK_SIZE) break;
    }

    logger_log(LOG_INFO, "media: saved %lld bytes to %s",
               (long long)offset, out_path);
    return 0;
}

int domain_download_photo(const ApiConfig *cfg,
                           MtProtoSession *s, Transport *t,
                           const MediaInfo *info,
                           const char *out_path,
                           int *wrong_dc) {
    if (wrong_dc) *wrong_dc = 0;
    if (!cfg || !s || !t || !info || !out_path) return -1;
    if (info->kind != MEDIA_PHOTO) {
        logger_log(LOG_ERROR, "media: download_photo needs MEDIA_PHOTO");
        return -1;
    }
    if (info->photo_id == 0 || info->access_hash == 0
        || info->file_reference_len == 0) {
        logger_log(LOG_ERROR, "media: missing id / access_hash / file_reference");
        return -1;
    }

    /* Cache hit: if the file is already indexed and still exists on disk,
     * copy/use the cached path rather than issuing upload.getFile again. */
    char cached[4096];
    if (media_index_get(info->photo_id, cached, sizeof(cached)) == 1) {
        FILE *fp = fopen(cached, "rb");
        if (fp) {
            fclose(fp);
            /* If the caller wants the same path that is already cached,
             * we're done.  Otherwise copy to out_path so the caller can
             * rely on it being at the requested location. */
            if (strcmp(cached, out_path) != 0) {
                RAII_FILE FILE *src = fopen(cached, "rb");
                RAII_FILE FILE *dst = fopen(out_path, "wb");
                if (src && dst) {
                    uint8_t buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, n, dst);
                }
            }
            logger_log(LOG_INFO, "media: cache hit for photo_id %lld → %s",
                       (long long)info->photo_id, cached);
            return 0;
        }
    }

    int rc = download_loop(cfg, s, t, info, out_path, wrong_dc);
    if (rc == 0)
        media_index_put(info->photo_id, out_path);
    return rc;
}

int domain_download_document(const ApiConfig *cfg,
                              MtProtoSession *s, Transport *t,
                              const MediaInfo *info,
                              const char *out_path,
                              int *wrong_dc) {
    if (wrong_dc) *wrong_dc = 0;
    if (!cfg || !s || !t || !info || !out_path) return -1;
    if (info->kind != MEDIA_DOCUMENT) {
        logger_log(LOG_ERROR, "media: download_document needs MEDIA_DOCUMENT");
        return -1;
    }
    if (info->document_id == 0 || info->access_hash == 0
        || info->file_reference_len == 0) {
        logger_log(LOG_ERROR,
                   "media: document missing id / access_hash / file_reference");
        return -1;
    }

    /* Cache hit: avoid re-downloading an already cached document. */
    char cached[4096];
    if (media_index_get(info->document_id, cached, sizeof(cached)) == 1) {
        FILE *fp = fopen(cached, "rb");
        if (fp) {
            fclose(fp);
            if (strcmp(cached, out_path) != 0) {
                RAII_FILE FILE *src = fopen(cached, "rb");
                RAII_FILE FILE *dst = fopen(out_path, "wb");
                if (src && dst) {
                    uint8_t buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, n, dst);
                }
            }
            logger_log(LOG_INFO, "media: cache hit for document_id %lld → %s",
                       (long long)info->document_id, cached);
            return 0;
        }
    }

    int rc = download_loop(cfg, s, t, info, out_path, wrong_dc);
    if (rc == 0)
        media_index_put(info->document_id, out_path);
    return rc;
}

/* Dispatch on MediaKind and call the right per-type entry point so the
 * cross-DC wrapper does not need to know the argument validation rules. */
static int download_any(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         const MediaInfo *info,
                         const char *out_path,
                         int *wrong_dc) {
    if (info->kind == MEDIA_PHOTO)
        return domain_download_photo(cfg, s, t, info, out_path, wrong_dc);
    if (info->kind == MEDIA_DOCUMENT)
        return domain_download_document(cfg, s, t, info, out_path, wrong_dc);
    logger_log(LOG_ERROR, "media: download_any unsupported kind=%d", info->kind);
    return -1;
}

int domain_download_media_cross_dc(const ApiConfig *cfg,
                                    MtProtoSession *home_s, Transport *home_t,
                                    const MediaInfo *info,
                                    const char *out_path) {
    if (!cfg || !home_s || !home_t || !info || !out_path) return -1;

    int wrong_dc = 0;
    if (download_any(cfg, home_s, home_t, info, out_path, &wrong_dc) == 0) {
        return 0;                                /* home DC had the file */
    }
    if (wrong_dc <= 0) return -1;                /* not a migration — hard fail */

    logger_log(LOG_INFO, "media: FILE_MIGRATE_%d, retrying on DC%d",
               wrong_dc, wrong_dc);

    DcSession xdc;
    if (dc_session_open(wrong_dc, &xdc) != 0) {
        logger_log(LOG_ERROR, "media: cannot open DC%d session", wrong_dc);
        return -1;
    }

    /* Freshly handshaked foreign sessions are not yet authorized.
     * dc_session_ensure_authorized() runs export/import; on a cached
     * session it is a no-op. */
    if (dc_session_ensure_authorized(&xdc, cfg, home_s, home_t) != 0) {
        logger_log(LOG_ERROR,
                   "media: cross-DC authorization setup failed for DC%d",
                   wrong_dc);
        dc_session_close(&xdc);
        return -1;
    }

    int dummy = 0;
    int rc = download_any(cfg, &xdc.session, &xdc.transport,
                          info, out_path, &dummy);
    if (rc != 0) {
        logger_log(LOG_ERROR, "media: retry on DC%d still failed", wrong_dc);
    }
    dc_session_close(&xdc);
    return rc;
}
