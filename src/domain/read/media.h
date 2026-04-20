/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/media.h
 * @brief P6-01 — download media (photos) via upload.getFile.
 *
 * Covers photos and documents (files, video, audio). Cross-DC downloads
 * (FILE_MIGRATE_X) are transparently handled by domain_download_media_cross_dc.
 * The module surfaces the migrate_dc hint on -1 so callers can at least
 * display an actionable error.
 */

#ifndef DOMAIN_READ_MEDIA_H
#define DOMAIN_READ_MEDIA_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "tl_skip.h"          /* MediaInfo */

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Download a photo referenced by a MediaInfo into @p out_path.
 *
 * Makes a chain of upload.getFile calls until an end-of-file chunk
 * (shorter than @p chunk_size bytes) arrives. Returns the photo DC id
 * via @p wrong_dc when the server rejects with FILE_MIGRATE_X so the
 * caller can surface it.
 *
 * @param cfg        API config.
 * @param s          Session with auth_key.
 * @param t          Connected transport.
 * @param info       MediaInfo filled by tl_skip_message_media_ex (must have
 *                   kind == MEDIA_PHOTO, non-zero id + access_hash +
 *                   file_reference_len).
 * @param out_path   Filesystem path to write the photo to (overwritten).
 * @param wrong_dc   Optional; set to the server-suggested DC when the
 *                   request fails with FILE_MIGRATE_X, 0 otherwise.
 * @return 0 on success, -1 on error.
 */
int domain_download_photo(const ApiConfig *cfg,
                           MtProtoSession *s, Transport *t,
                           const MediaInfo *info,
                           const char *out_path,
                           int *wrong_dc);

/**
 * @brief Download a document (file / video / audio / etc.) referenced
 *        by a MediaInfo into @p out_path.
 *
 * Same chunked upload.getFile flow as domain_download_photo, except the
 * input location is inputDocumentFileLocation with thumb_size = "".
 * Works for MIME types we can represent as a plain file. Caller is
 * responsible for picking @p out_path — MediaInfo.document_filename
 * (from DocumentAttributeFilename) is a good default.
 */
int domain_download_document(const ApiConfig *cfg,
                              MtProtoSession *s, Transport *t,
                              const MediaInfo *info,
                              const char *out_path,
                              int *wrong_dc);

/**
 * @brief Download a photo or document, transparently following
 *        FILE_MIGRATE_X to a secondary DC if required.
 *
 * Thin wrapper over domain_download_photo / domain_download_document
 * that inspects the @p wrong_dc hint on the first attempt, opens a
 * DcSession on the target DC via dc_session_open(), and retries the
 * download there. The secondary session is closed before return.
 *
 * The caller's @p home_s / @p home_t are untouched; the retry uses
 * a fresh transport internally.
 *
 * @param cfg       API config.
 * @param home_s    Session pinned to the home DC.
 * @param home_t    Transport for the home DC.
 * @param info      MediaInfo (MEDIA_PHOTO or MEDIA_DOCUMENT).
 * @param out_path  Destination path.
 * @return 0 on success, -1 if home and secondary both failed.
 */
int domain_download_media_cross_dc(const ApiConfig *cfg,
                                    MtProtoSession *home_s, Transport *home_t,
                                    const MediaInfo *info,
                                    const char *out_path);

#endif /* DOMAIN_READ_MEDIA_H */
