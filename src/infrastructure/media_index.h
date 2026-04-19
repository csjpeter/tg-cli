/**
 * @file infrastructure/media_index.h
 * @brief Persistent index mapping media_id → local file path.
 *
 * Written by the `download` command after a successful download so
 * the `history` command can display `[photo: /path]` inline without
 * re-downloading.
 *
 * Format: one entry per line, tab-separated: "<media_id>\t<path>\n"
 * Location: ~/.cache/tg-cli/media.idx
 */

#ifndef MEDIA_INDEX_H
#define MEDIA_INDEX_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Record that @p media_id has been downloaded to @p local_path.
 *
 * Appends or updates the entry in the index file. Creates the file and
 * any missing parent directories on first use.
 *
 * @param media_id    The photo_id or document_id.
 * @param local_path  Absolute path to the locally cached file.
 * @return 0 on success, -1 on I/O error.
 */
int media_index_put(int64_t media_id, const char *local_path);

/**
 * @brief Look up a previously downloaded media file.
 *
 * @param media_id    The photo_id or document_id to search for.
 * @param out_path    Output buffer that receives the absolute path.
 * @param out_cap     Capacity of @p out_path in bytes.
 * @return 1 if found (path written to @p out_path), 0 if not cached,
 *         -1 on I/O error.
 */
int media_index_get(int64_t media_id, char *out_path, size_t out_cap);

#endif /* MEDIA_INDEX_H */
