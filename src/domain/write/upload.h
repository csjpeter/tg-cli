/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/upload.h
 * @brief US-P6-02 — upload a local file and send it as a document.
 *
 * Chunked upload via upload.saveFilePart (small files) or
 * upload.saveBigFilePart (files >= UPLOAD_BIG_THRESHOLD). The latter
 * uses InputFileBig (no md5) on messages.sendMedia. Cap at
 * UPLOAD_MAX_SIZE (1.5 GiB).
 */

#ifndef DOMAIN_WRITE_UPLOAD_H
#define DOMAIN_WRITE_UPLOAD_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer */

#include <stdint.h>

/** Upload chunk size — 512 KiB per Telegram recommendation. */
#define UPLOAD_CHUNK_SIZE   (512 * 1024)
/** Files >= this go through upload.saveBigFilePart + InputFileBig. */
#define UPLOAD_BIG_THRESHOLD (10 * 1024 * 1024)
/** Hard cap (Telegram server-side limit is ~2 GiB). */
#define UPLOAD_MAX_SIZE     ((int64_t)1536 * 1024 * 1024)

/**
 * @brief Upload @p file_path and attach it to @p peer as a document.
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Connected transport.
 * @param peer      Destination peer.
 * @param file_path Local filesystem path.
 * @param caption   Optional caption text; NULL = empty.
 * @param mime_type Optional mime type override; NULL = "application/octet-stream".
 * @param err       Optional RPC error output.
 * @return 0 on success, -1 on error.
 */
int domain_send_file(const ApiConfig *cfg,
                      MtProtoSession *s, Transport *t,
                      const HistoryPeer *peer,
                      const char *file_path,
                      const char *caption,
                      const char *mime_type,
                      RpcError *err);

/**
 * @brief Upload @p file_path and attach it as a photo (scaled InputMedia).
 *
 * The server rescales / recompresses the image — no client-side
 * decoding happens. The file is chunk-uploaded like a document, then
 * sent via messages.sendMedia + inputMediaUploadedPhoto so Telegram
 * clients render it inline instead of as a file.
 *
 * @return 0 on success, -1 on error.
 */
int domain_send_photo(const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       const HistoryPeer *peer,
                       const char *file_path,
                       const char *caption,
                       RpcError *err);

/**
 * @brief Heuristic: does the path's extension look like an image?
 *
 * Returns 1 for .jpg/.jpeg/.png/.webp/.gif (case-insensitive),
 * 0 otherwise. Used by the CLI/TUI `upload` command to dispatch
 * photo-upload vs document-upload automatically.
 */
int domain_path_is_image(const char *path);

#endif /* DOMAIN_WRITE_UPLOAD_H */
