#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize app settings (loads from NVS).
 * Call after bt_enable() so bt_set_name() works.
 */
int app_settings_init(void);

/**
 * Get the current device name.
 */
const char *app_settings_get_name(void);

/**
 * Set and persist the device name.
 * Takes effect on next AT+ADV start.
 * Returns 0 on success.
 */
int app_settings_set_name(const char *name);

/**
 * Get the current role as a string ("none", "initiator", "reflector").
 */
const char *app_settings_get_role_str(void);

/**
 * Set and persist the role by string.
 * Also calls bt_mgr_set_role() to apply immediately.
 * Returns 0 on success, -EINVAL for unknown role string,
 * or -EBUSY if role change is not allowed right now.
 */
int app_settings_set_role(const char *role_str);

/** Get adv_autostart setting. */
bool app_settings_get_adv_autostart(void);

/** Set and persist adv_autostart ("y" or "n"). */
int app_settings_set_adv_autostart(const char *val);

/** Get connection interval in ms. */
uint16_t app_settings_get_conn_interval_ms(void);

/** Set and persist connection interval in ms (10-400). */
int app_settings_set_conn_interval_ms(uint16_t ms);

/** Get the current baudrate. */
uint32_t app_settings_get_baudrate(void);

/**
 * Set and persist the UART baudrate.
 * Does NOT reconfigure the UART — caller is responsible for that.
 * Returns 0 on success, -EINVAL for unsupported rate.
 */
int app_settings_set_baudrate(uint32_t baud);

#endif /* APP_SETTINGS_H_ */
