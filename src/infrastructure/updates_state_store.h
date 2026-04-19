/**
 * @file infrastructure/updates_state_store.h
 * @brief Persist UpdatesState (pts/qts/date/seq) to ~/.cache/tg-cli/updates.state.
 *
 * The file is written in a simple INI format with mode 0600 so that
 * consecutive `tg-cli-ro watch` invocations resume from the last known
 * high-water mark without re-issuing updates.getState.
 */

#ifndef INFRASTRUCTURE_UPDATES_STATE_STORE_H
#define INFRASTRUCTURE_UPDATES_STATE_STORE_H

#include "domain/read/updates.h"

/**
 * @brief Load UpdatesState from disk.
 *
 * Reads ~/.cache/tg-cli/updates.state (INI key=value pairs).
 *
 * @param out  Filled on success.
 * @return  0  on success,
 *         -1  if the file is missing or unreadable (not an error — caller
 *              should fall back to updates.getState),
 *         -2  on a parse error.
 */
int updates_state_load(UpdatesState *out);

/**
 * @brief Save UpdatesState to disk with mode 0600.
 *
 * Atomically writes ~/.cache/tg-cli/updates.state.
 *
 * @param st  State to persist.
 * @return 0 on success, -1 on failure.
 */
int updates_state_save(const UpdatesState *st);

#endif /* INFRASTRUCTURE_UPDATES_STATE_STORE_H */
