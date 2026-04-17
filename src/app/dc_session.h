/**
 * @file app/dc_session.h
 * @brief Open an MTProto session on an arbitrary DC, reusing a cached
 *        auth_key when one is persisted.
 *
 * Foundation for cross-DC media routing (P10-03): a `FILE_MIGRATE_X` /
 * `NETWORK_MIGRATE_X` reply can be followed by `dc_session_open(X, ...)`
 * and the RPC retried on the returned transport.
 *
 * This layer does not perform `auth.importAuthorization`. A freshly
 * opened session on a non-home DC is usable for public operations; for
 * authorized media methods (`upload.getFile`, `upload.saveFilePart`)
 * the caller must still run the export/import dance (P10-04).
 */

#ifndef APP_DC_SESSION_H
#define APP_DC_SESSION_H

#include "mtproto_session.h"
#include "transport.h"

/** @brief A connected MTProto session pinned to a single DC. */
typedef struct {
    int            dc_id;
    Transport      transport;
    MtProtoSession session;
    int            authorized;   /**< 0 until auth.importAuthorization runs. */
} DcSession;

/**
 * @brief Bring up an MTProto session on @p dc_id.
 *
 * Fast path: load the persisted auth_key via session_store_load_dc()
 * and just open the TCP transport. Slow path: full DH handshake via
 * auth_flow_connect_dc(), then persist via session_store_save_dc()
 * for the next run.
 *
 * @param dc_id Target DC id (1..5).
 * @param out   Receives a connected session on success. Caller owns it.
 * @return 0 on success, -1 on connect / handshake / DC-lookup failure.
 */
int dc_session_open(int dc_id, DcSession *out);

/**
 * @brief Close the transport; session state stays in memory + on disk.
 *
 * After dc_session_close() the caller may still read @p s->session (for
 * example to compare auth_keys in tests), but must not use @p s->transport
 * without a fresh dc_session_open().
 */
void dc_session_close(DcSession *s);

#endif /* APP_DC_SESSION_H */
