#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "at_cmd.h"
#include "app_settings.h"
#include "bt_mgr.h"
#include "session_mgr.h"
#include "version.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static void at_test_handler(const char *args)
{
	at_cmd_respond("OK");
}

static void ati_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	if (strncasecmp(args, "version", 7) == 0) {
		at_cmd_respond(APP_VERSION);
		at_cmd_respond("OK");
	} else if (strncasecmp(args, "board", 5) == 0) {
		char resp[32];
		snprintk(resp, sizeof(resp), "%s - %d antenna%s",
				CONFIG_BOARD, CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS,
				CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS>1?"s":"");
		at_cmd_respond(resp);
		at_cmd_respond("OK");
	} else {
		at_cmd_respond("ERROR");
	}
}

static void atz_handler(const char *args)
{
	at_cmd_respond("OK");
	sys_reboot(SYS_REBOOT_COLD);
}

static void at_scan_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	if (strcasecmp(args, "stop") == 0) {
		int err = bt_mgr_scan_stop();

		at_cmd_respond(err ? "ERROR" : "OK");
		return;
	}

	uint16_t timeout = 0;

	if (*args != '\0') {
		timeout = (uint16_t)atoi(args);
		if (timeout == 0) {
			at_cmd_respond("ERROR");
			return;
		}
	}

	int err = bt_mgr_scan_start(timeout);

	at_cmd_respond(err ? "ERROR" : "OK");
}

static void at_adv_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	if (strcasecmp(args, "start") == 0) {
		int err = bt_mgr_adv_start();

		at_cmd_respond(err ? "ERROR" : "OK");
	} else if (strcasecmp(args, "stop") == 0) {
		int err = bt_mgr_adv_stop();

		at_cmd_respond(err ? "ERROR" : "OK");
	} else {
		at_cmd_respond("ERROR");
	}
}

static void ats_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	/* ATS role=? or ATS role=<value> */
	if (strncasecmp(args, "role", 4) == 0) {
		const char *val = args + 4;

		if (*val != '=') {
			at_cmd_respond("ERROR");
			return;
		}
		val++;

		if (*val == '?') {
			at_cmd_respond(app_settings_get_role_str());
			at_cmd_respond("OK");
			return;
		}

		int err = app_settings_set_role(val);

		at_cmd_respond(err ? "ERROR" : "OK");
		return;
	}

	/* ATS adv_autostart=? or ATS adv_autostart=<y|n> */
	if (strncasecmp(args, "adv_autostart", 13) == 0) {
		const char *val = args + 13;

		if (*val != '=') {
			at_cmd_respond("ERROR");
			return;
		}
		val++;

		if (*val == '?') {
			at_cmd_respond(app_settings_get_adv_autostart()
				       ? "y" : "n");
			at_cmd_respond("OK");
			return;
		}

		int err = app_settings_set_adv_autostart(val);

		at_cmd_respond(err ? "ERROR" : "OK");
		return;
	}

	/* ATS devicename=? or ATS devicename=<value> */
	if (strncasecmp(args, "devicename", 10) == 0) {
		const char *val = args + 10;

		if (*val != '=') {
			at_cmd_respond("ERROR");
			return;
		}
		val++;

		if (*val == '?') {
			char resp[48];

			snprintk(resp, sizeof(resp), "devicename=\"%s\"",
				 app_settings_get_name());
			at_cmd_respond(resp);
			at_cmd_respond("OK");
			return;
		}

		/* Strip optional quotes */
		size_t len = strlen(val);
		char name_buf[CONFIG_BT_DEVICE_NAME_MAX + 1];

		if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
			if (len - 2 > CONFIG_BT_DEVICE_NAME_MAX || len < 3) {
				at_cmd_respond("ERROR");
				return;
			}
			memcpy(name_buf, val + 1, len - 2);
			name_buf[len - 2] = '\0';
		} else {
			if (len > CONFIG_BT_DEVICE_NAME_MAX || len == 0) {
				at_cmd_respond("ERROR");
				return;
			}
			memcpy(name_buf, val, len);
			name_buf[len] = '\0';
		}

		int err = app_settings_set_name(name_buf);

		at_cmd_respond(err ? "ERROR" : "OK");
		return;
	}

	/* ATS baudrate=? or ATS baudrate=<num> */
	if (strncasecmp(args, "baudrate", 8) == 0) {
		const char *val = args + 8;

		if (*val != '=') {
			at_cmd_respond("ERROR");
			return;
		}
		val++;

		if (*val == '?') {
			char resp[32];

			snprintk(resp, sizeof(resp), "baudrate=%u",
				 app_settings_get_baudrate());
			at_cmd_respond(resp);
			at_cmd_respond("OK");
			return;
		}

		char *end;
		unsigned long baud = strtoul(val, &end, 10);

		if (end == val || *end != '\0' || baud == 0 || baud > UINT32_MAX) {
			at_cmd_respond("ERROR");
			return;
		}

		int err = app_settings_set_baudrate((uint32_t)baud);

		if (err) {
			at_cmd_respond("ERROR");
			return;
		}

		/* Send OK at the old baudrate before switching */
		at_cmd_respond("OK");

		/* Brief delay to let the OK transmit fully */
		k_msleep(50);

		at_cmd_set_baudrate((uint32_t)baud);
		return;
	}

	/* ATS conn_int=? or ATS conn_int=<ms> */
	if (strncasecmp(args, "conn_int", 8) == 0) {
		const char *val = args + 8;

		if (*val != '=') {
			at_cmd_respond("ERROR");
			return;
		}
		val++;

		if (*val == '?') {
			char resp[32];

			snprintk(resp, sizeof(resp), "conn_int=%u",
				 app_settings_get_conn_interval_ms());
			at_cmd_respond(resp);
			at_cmd_respond("OK");
			return;
		}

		char *end;
		unsigned long ms = strtoul(val, &end, 10);

		if (end == val || *end != '\0' || ms > UINT16_MAX) {
			at_cmd_respond("ERROR");
			return;
		}

		int err = app_settings_set_conn_interval_ms((uint16_t)ms);

		at_cmd_respond(err ? "ERROR" : "OK");
		return;
	}

	at_cmd_respond("ERROR");
}

static void at_range_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	/* AT+RANGE mac=<addr>[,int=<ms>] */
	if (bt_mgr_get_role() != BT_MGR_ROLE_INITIATOR) {
		at_cmd_respond("ERROR");
		return;
	}

	if (strncasecmp(args, "mac=", 4) != 0) {
		at_cmd_respond("ERROR");
		return;
	}

	const char *mac_start = args + 4;
	const char *comma = strchr(mac_start, ',');
	size_t mac_len = comma ? (size_t)(comma - mac_start) : strlen(mac_start);

	if (mac_len != 12) {
		at_cmd_respond("ERROR");
		return;
	}

	char mac_str[13];

	memcpy(mac_str, mac_start, 12);
	mac_str[12] = '\0';

	uint16_t interval_ms = 1000;

	if (comma) {
		const char *int_param = comma + 1;

		while (*int_param == ' ') {
			int_param++;
		}
		if (strncasecmp(int_param, "int=", 4) != 0) {
			at_cmd_respond("ERROR");
			return;
		}
		interval_ms = (uint16_t)atoi(int_param + 4);
		if (interval_ms == 0) {
			at_cmd_respond("ERROR");
			return;
		}
	}

	bt_addr_le_t addr;
	int err = bt_mgr_scan_lookup(mac_str, &addr);

	if (err) {
		at_cmd_respond("ERROR");
		return;
	}

	/* Stop scanning if active — controller can't scan and connect */
	if (bt_mgr_is_scanning()) {
		bt_mgr_scan_stop();
	}

	uint8_t session_id;

	err = session_mgr_start(&addr, interval_ms, &session_id);
	if (err) {
		at_cmd_respond("ERROR");
		return;
	}

	char resp[16];

	snprintk(resp, sizeof(resp), "+RANGE:%u", session_id);
	at_cmd_respond(resp);
	at_cmd_respond("OK");
}

static void at_diag_handler(const char *args)
{
	session_mgr_diag();
	at_cmd_respond("OK");
}

static void at_rangex_handler(const char *args)
{
	while (*args == ' ') {
		args++;
	}

	uint8_t id = (uint8_t)atoi(args);

	if (id == 0) {
		at_cmd_respond("ERROR");
		return;
	}

	int err = session_mgr_stop(id);

	at_cmd_respond(err ? "ERROR" : "OK");
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(uart)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	at_cmd_init(uart);
	at_cmd_register("AT+ADV", at_adv_handler);
	at_cmd_register("AT+SCAN", at_scan_handler);
	at_cmd_register("AT+DIAG", at_diag_handler);
	at_cmd_register("AT+RANGEX", at_rangex_handler);
	at_cmd_register("AT+RANGE", at_range_handler);
	at_cmd_register("ATS", ats_handler);
	at_cmd_register("ATI", ati_handler);
	at_cmd_register("ATZ", atz_handler);
	at_cmd_register("AT", at_test_handler);

	int err = bt_mgr_init();

	if (err) {
		LOG_ERR("BT init failed (err %d)", err);
		return -1;
	}

	err = app_settings_init();
	if (err) {
		LOG_WRN("Settings init failed (err %d), using defaults", err);
	}

	/* Apply saved baudrate (default 115200 if not set) */
	uint32_t baud = app_settings_get_baudrate();

	if (baud != 115200) {
		at_cmd_set_baudrate(baud);
	}

	session_mgr_init();

	at_cmd_respond("OK");

	return 0;
}
