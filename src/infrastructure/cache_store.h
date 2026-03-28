#ifndef CACHE_STORE_H
#define CACHE_STORE_H

#include <stddef.h>

/**
 * @file cache_store.h
 * @brief Local cache for Telegram content (messages, media).
 *
 * Messages: ~/.cache/tg-cli/messages/<chat_id>/<msg_id>.json
 * Media:    ~/.cache/tg-cli/media/<file_id>.<ext>
 */

/**
 * @brief Checks whether a cached file exists in the given category.
 * @param category Cache subdirectory (e.g. "messages", "media").
 * @param key      Unique key (e.g. "chat_id/msg_id" or "file_id").
 * @return 1 if the file exists, 0 otherwise.
 */
int cache_exists(const char *category, const char *key);

/**
 * @brief Writes content to the local cache.
 * @param category Cache subdirectory.
 * @param key      Unique key.
 * @param content  Raw bytes to cache.
 * @param len      Number of bytes to write.
 * @return 0 on success, -1 on failure.
 */
int cache_save(const char *category, const char *key,
               const char *content, size_t len);

/**
 * @brief Reads a cached file from disk.
 * @param category Cache subdirectory.
 * @param key      Unique key.
 * @return Heap-allocated NUL-terminated string, or NULL. Caller must free.
 */
char *cache_load(const char *category, const char *key);

/**
 * @brief Returns the full filesystem path for a cache entry.
 * @param category Cache subdirectory.
 * @param key      Unique key.
 * @return Heap-allocated path string, or NULL. Caller must free.
 */
char *cache_path(const char *category, const char *key);

/**
 * @brief Removes cached files whose key is not in @p keep_keys.
 *
 * @param category   Cache subdirectory.
 * @param keep_keys  Array of keys that are still valid.
 * @param keep_count Number of entries in @p keep_keys.
 */
void cache_evict_stale(const char *category,
                       const char **keep_keys, int keep_count);

#endif /* CACHE_STORE_H */
