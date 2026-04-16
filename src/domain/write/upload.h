/**
 * @file domain/write/upload.h
 * @brief US-P6-02 — upload a local file and send it as a document.
 *
 * Chunked upload via upload.saveFilePart, then messages.sendMedia with
 * an InputMediaUploadedDocument carrying just a DocumentAttributeFilename
 * (v1). Small files only — the upload.saveBigFilePart path (> 10 MB) is
 * a follow-up; we cap the request at UPLOAD_MAX_SIZE for now.
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
/** Max file size handled by this v1 (10 MiB — no saveBigFilePart yet). */
#define UPLOAD_MAX_SIZE     (10 * 1024 * 1024)

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

#endif /* DOMAIN_WRITE_UPLOAD_H */
