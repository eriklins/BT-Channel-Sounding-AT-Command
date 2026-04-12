#ifndef BT_MGR_H_
#define BT_MGR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/addr.h>

enum bt_mgr_role {
	BT_MGR_ROLE_NONE,
	BT_MGR_ROLE_INITIATOR,
	BT_MGR_ROLE_REFLECTOR,
};

/**
 * Initialize the Bluetooth stack.
 * Returns 0 on success, negative errno on failure.
 */
int bt_mgr_init(void);

/**
 * Set the device role (initiator or reflector).
 * This only stores the role internally — it does not start
 * advertising or scanning.
 * Returns 0 on success, negative errno on failure.
 */
int bt_mgr_set_role(enum bt_mgr_role role);

/** Get the current role. */
enum bt_mgr_role bt_mgr_get_role(void);

/**
 * Start advertising with the RAS UUID (reflector role).
 * Returns -EINVAL if role is not reflector, -EALREADY if already
 * advertising, or a BT error code.
 */
int bt_mgr_adv_start(void);

/**
 * Stop advertising.
 * Returns 0 on success or if not advertising.
 */
int bt_mgr_adv_stop(void);

/** Returns true if currently advertising. */
bool bt_mgr_is_advertising(void);

/**
 * Start scanning for devices advertising the RAS UUID.
 * Each new device found is reported as "+SCAN:<addr>,<rssi>".
 * Scanning stops after timeout_sec seconds, or runs indefinitely
 * if timeout_sec is 0. Sends "+SCANDONE" when finished.
 * Returns -EINVAL if role is not initiator.
 */
int bt_mgr_scan_start(uint16_t timeout_sec);

/**
 * Stop an ongoing scan.
 * Returns 0 on success or if not scanning.
 */
int bt_mgr_scan_stop(void);

/** Returns true if currently scanning. */
bool bt_mgr_is_scanning(void);

/**
 * Look up a scanned device address by MAC hex string (e.g. "AABBCCDDEEFF").
 * Returns the full bt_addr_le_t with correct address type from scan results.
 * If not found, fills addr with the parsed MAC and BT_ADDR_LE_RANDOM type.
 * Returns 0 on success, -EINVAL if MAC string is malformed.
 */
int bt_mgr_scan_lookup(const char *mac_hex, bt_addr_le_t *addr);

/** Callback type invoked for each new scan result. mac_hex is 12-char uppercase. */
typedef void (*bt_mgr_scan_found_cb_t)(const char *mac_hex);

/** Register a callback invoked for each new scan result. */
void bt_mgr_set_scan_found_cb(bt_mgr_scan_found_cb_t cb);

#endif /* BT_MGR_H_ */
