#include "app_settings.h"
#include "bt_mgr.h"

#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

#define SETTINGS_KEY_NAME      "cs_at/name"
#define SETTINGS_KEY_ROLE      "cs_at/role"
#define SETTINGS_KEY_ADV_AUTO  "cs_at/advauto"
#define SETTINGS_KEY_CONN_INT  "cs_at/connint"
#define SETTINGS_KEY_BAUDRATE  "cs_at/baud"
#define NAME_MAX_LEN           CONFIG_BT_DEVICE_NAME_MAX
#define ROLE_MAX_LEN           16
#define CONN_INT_DEFAULT_MS    400
#define CONN_INT_MIN_MS        10
#define CONN_INT_MAX_MS        400
#define BAUDRATE_DEFAULT       115200

static char device_name[NAME_MAX_LEN + 1];
static bool name_loaded;

static char role_str[ROLE_MAX_LEN + 1];
static bool role_loaded;

static bool adv_autostart;
static bool adv_autostart_loaded;

static uint16_t conn_interval_ms = CONN_INT_DEFAULT_MS;
static bool conn_interval_loaded;

static uint32_t baudrate = BAUDRATE_DEFAULT;
static bool baudrate_loaded;

static int settings_set_cb(const char *name, size_t len,
			   settings_read_cb read_cb, void *cb_arg)
{
	if (!strcmp(name, "name")) {
		if (len > NAME_MAX_LEN) {
			len = NAME_MAX_LEN;
		}
		int rc = read_cb(cb_arg, device_name, len);

		if (rc >= 0) {
			device_name[rc] = '\0';
			name_loaded = true;
			LOG_INF("Loaded device name: \"%s\"", device_name);
		}
		return 0;
	}

	if (!strcmp(name, "advauto")) {
		char val;

		if (len >= 1) {
			int rc = read_cb(cb_arg, &val, 1);

			if (rc >= 1) {
				adv_autostart = (val == 'y');
				adv_autostart_loaded = true;
				LOG_INF("Loaded adv_autostart: %c", val);
			}
		}
		return 0;
	}

	if (!strcmp(name, "connint")) {
		uint16_t val;

		if (len == sizeof(val)) {
			int rc = read_cb(cb_arg, &val, sizeof(val));

			if (rc == sizeof(val) &&
			    val >= CONN_INT_MIN_MS && val <= CONN_INT_MAX_MS) {
				conn_interval_ms = val;
				conn_interval_loaded = true;
				LOG_INF("Loaded conn_int: %u ms", val);
			}
		}
		return 0;
	}

	if (!strcmp(name, "baud")) {
		uint32_t val;

		if (len == sizeof(val)) {
			int rc = read_cb(cb_arg, &val, sizeof(val));

			if (rc == sizeof(val) && val > 0) {
				baudrate = val;
				baudrate_loaded = true;
				LOG_INF("Loaded baudrate: %u", val);
			}
		}
		return 0;
	}

	if (!strcmp(name, "role")) {
		if (len > ROLE_MAX_LEN) {
			len = ROLE_MAX_LEN;
		}
		int rc = read_cb(cb_arg, role_str, len);

		if (rc >= 0) {
			role_str[rc] = '\0';
			role_loaded = true;
			LOG_INF("Loaded role: \"%s\"", role_str);
		}
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(cs_at, "cs_at", NULL, settings_set_cb, NULL, NULL);

int app_settings_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("settings_subsys_init failed (err %d)", err);
		return err;
	}

	err = settings_load();
	if (err) {
		LOG_ERR("settings_load failed (err %d)", err);
		return err;
	}

	if (!name_loaded) {
		strncpy(device_name, CONFIG_BT_DEVICE_NAME, NAME_MAX_LEN);
		device_name[NAME_MAX_LEN] = '\0';
	}

	/* Apply the name to the BT stack */
	err = bt_set_name(device_name);
	if (err) {
		LOG_ERR("bt_set_name failed (err %d)", err);
		return err;
	}

	/* Apply the saved role */
	if (!role_loaded) {
		strncpy(role_str, "none", ROLE_MAX_LEN);
	}

	enum bt_mgr_role role = BT_MGR_ROLE_NONE;

	if (strcasecmp(role_str, "initiator") == 0) {
		role = BT_MGR_ROLE_INITIATOR;
	} else if (strcasecmp(role_str, "reflector") == 0) {
		role = BT_MGR_ROLE_REFLECTOR;
	}

	bt_mgr_set_role(role);

	/* Auto-start advertising if configured and role is reflector */
	if (adv_autostart && role == BT_MGR_ROLE_REFLECTOR) {
		int adv_err = bt_mgr_adv_start();

		if (adv_err) {
			LOG_WRN("Adv autostart failed (err %d)", adv_err);
		} else {
			LOG_INF("Adv autostart: advertising started");
		}
	}

	return 0;
}

const char *app_settings_get_name(void)
{
	return device_name;
}

int app_settings_set_name(const char *name)
{
	size_t len = strlen(name);

	if (len == 0 || len > NAME_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(device_name, name, NAME_MAX_LEN);
	device_name[NAME_MAX_LEN] = '\0';

	int err = bt_set_name(device_name);

	if (err) {
		LOG_ERR("bt_set_name failed (err %d)", err);
		return err;
	}

	err = settings_save_one(SETTINGS_KEY_NAME, device_name, strlen(device_name));
	if (err) {
		LOG_ERR("settings_save_one failed (err %d)", err);
		return err;
	}

	LOG_INF("Device name set to \"%s\"", device_name);
	return 0;
}

static enum bt_mgr_role parse_role(const char *str)
{
	if (strcasecmp(str, "initiator") == 0) {
		return BT_MGR_ROLE_INITIATOR;
	} else if (strcasecmp(str, "reflector") == 0) {
		return BT_MGR_ROLE_REFLECTOR;
	} else if (strcasecmp(str, "none") == 0) {
		return BT_MGR_ROLE_NONE;
	}
	return -1;
}

static const char *role_to_str(enum bt_mgr_role role)
{
	switch (role) {
	case BT_MGR_ROLE_INITIATOR:
		return "initiator";
	case BT_MGR_ROLE_REFLECTOR:
		return "reflector";
	default:
		return "none";
	}
}

const char *app_settings_get_role_str(void)
{
	return role_to_str(bt_mgr_get_role());
}

int app_settings_set_role(const char *str)
{
	int role = parse_role(str);

	if (role == -1) {
		return -EINVAL;
	}

	int err = bt_mgr_set_role(role);

	if (err) {
		return err;
	}

	const char *canonical = role_to_str(role);

	strncpy(role_str, canonical, ROLE_MAX_LEN);

	err = settings_save_one(SETTINGS_KEY_ROLE, canonical, strlen(canonical));
	if (err) {
		LOG_ERR("settings_save_one failed (err %d)", err);
		return err;
	}

	LOG_INF("Role set to \"%s\"", canonical);
	return 0;
}

bool app_settings_get_adv_autostart(void)
{
	return adv_autostart;
}

int app_settings_set_adv_autostart(const char *val)
{
	if (strcasecmp(val, "y") == 0) {
		adv_autostart = true;
	} else if (strcasecmp(val, "n") == 0) {
		adv_autostart = false;
	} else {
		return -EINVAL;
	}

	char c = adv_autostart ? 'y' : 'n';
	int err = settings_save_one(SETTINGS_KEY_ADV_AUTO, &c, 1);

	if (err) {
		LOG_ERR("settings_save_one failed (err %d)", err);
		return err;
	}

	LOG_INF("adv_autostart set to %c", c);
	return 0;
}

uint16_t app_settings_get_conn_interval_ms(void)
{
	return conn_interval_ms;
}

int app_settings_set_conn_interval_ms(uint16_t ms)
{
	if (ms < CONN_INT_MIN_MS || ms > CONN_INT_MAX_MS) {
		return -EINVAL;
	}

	conn_interval_ms = ms;

	int err = settings_save_one(SETTINGS_KEY_CONN_INT, &ms, sizeof(ms));

	if (err) {
		LOG_ERR("settings_save_one failed (err %d)", err);
		return err;
	}

	LOG_INF("conn_int set to %u ms", ms);
	return 0;
}

static const uint32_t supported_baudrates[] = {
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600,
};

uint32_t app_settings_get_baudrate(void)
{
	return baudrate;
}

int app_settings_set_baudrate(uint32_t baud)
{
	bool valid = false;

	for (size_t i = 0; i < ARRAY_SIZE(supported_baudrates); i++) {
		if (baud == supported_baudrates[i]) {
			valid = true;
			break;
		}
	}

	if (!valid) {
		return -EINVAL;
	}

	baudrate = baud;

	int err = settings_save_one(SETTINGS_KEY_BAUDRATE, &baud, sizeof(baud));

	if (err) {
		LOG_ERR("settings_save_one failed (err %d)", err);
		return err;
	}

	LOG_INF("baudrate set to %u", baud);
	return 0;
}
