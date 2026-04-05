#ifndef SESSION_MGR_H_
#define SESSION_MGR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>

#define SESSION_MGR_MAX_SESSIONS 4

/**
 * Initialize the session manager. Call once after bt_enable().
 */
int session_mgr_init(void);

/**
 * Start a ranging session with the given peer.
 * Connects, sets up CS, and begins ranging procedures.
 * @param addr        Peer BLE address.
 * @param interval_ms Desired interval between CS procedures in milliseconds.
 * @param session_id  Output: assigned session ID (1-based).
 * @return 0 on success (setup queued), negative errno on failure.
 */
int session_mgr_start(const bt_addr_le_t *addr, uint16_t interval_ms, uint8_t *session_id);

/**
 * Stop a ranging session and disconnect.
 * @param session_id Session ID returned by session_mgr_start().
 * @return 0 on success, negative errno on failure.
 */
int session_mgr_stop(uint8_t session_id);

/**
 * Check if a connection belongs to a ranging session.
 */
bool session_mgr_owns_conn(struct bt_conn *conn);

/**
 * Print per-session diagnostic counters via AT interface.
 */
void session_mgr_diag(void);

#endif /* SESSION_MGR_H_ */
