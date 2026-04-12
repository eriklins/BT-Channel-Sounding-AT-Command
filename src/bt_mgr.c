#include "bt_mgr.h"
#include "at_cmd.h"
#include "app_settings.h"
#include "session_mgr.h"

#include <ctype.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/ras.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_mgr, LOG_LEVEL_INF);

static enum bt_mgr_role current_role;
static bool advertising;
static bool scanning;
static struct bt_conn *connection;
static uint8_t remote_num_antennas;

/* Reflector CS setup */
static K_SEM_DEFINE(sem_reflector_config, 0, 1);

#define REFLECTOR_SETUP_STACK_SIZE 2048
#define REFLECTOR_SETUP_PRIORITY   7
static K_THREAD_STACK_DEFINE(reflector_setup_stack, REFLECTOR_SETUP_STACK_SIZE);
static struct k_thread reflector_setup_thread_data;

/* Track already-reported scan results to avoid duplicates */
#define SCAN_MAX_DEVICES 32

static bt_addr_le_t scan_seen[SCAN_MAX_DEVICES];
static int scan_seen_count;

static struct k_work_delayable scan_timeout_work;
static bt_mgr_scan_found_cb_t scan_found_cb;

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_RANGING_SERVICE_VAL)),
	BT_DATA(BT_DATA_NAME_COMPLETE, "", 0), /* filled at adv start */
};

/* --- Antenna configuration selection --- */

static enum bt_conn_le_cs_tone_antenna_config_selection
select_tone_antenna_config(uint8_t a_antennas, uint8_t b_antennas)
{
	if (a_antennas >= 2 && b_antennas >= 2) {
		return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B2;
	}

	if (a_antennas == 1) {
		switch (b_antennas) {
		case 4:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B4;
		case 3:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B3;
		case 2:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B2;
		default: return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1;
		}
	}

	switch (a_antennas) {
	case 4:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1;
	case 3:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1;
	case 2:  return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B1;
	default: return BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1;
	}
}

static uint8_t preferred_peer_antenna_mask(uint8_t peer_antennas)
{
	uint8_t mask = 0;

	for (uint8_t i = 0; i < peer_antennas && i < 4; i++) {
		mask |= BIT(i);
	}

	return mask;
}

/* --- Reflector CS setup --- */

static void reflector_setup_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct bt_le_cs_set_default_settings_param default_settings = {
		.enable_initiator_role = false,
		.enable_reflector_role = true,
		.cs_sync_antenna_selection =
			BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	int err = bt_le_cs_set_default_settings(connection, &default_settings);

	if (err) {
		LOG_ERR("Reflector CS default settings failed (err %d)", err);
		at_cmd_respond("+REFLECTOR ERROR");
		return;
	}

	LOG_INF("Reflector: waiting for CS config from initiator...");

	/* Wait for initiator to create CS config */
	if (k_sem_take(&sem_reflector_config, K_SECONDS(30)) != 0) {
		LOG_WRN("Reflector: CS config timeout");
		at_cmd_respond("+REFLECTOR TIMEOUT");
		return;
	}

	if (!connection) {
		return;
	}

	/* Set procedure parameters (wide range — initiator's values take precedence) */
	uint8_t peer_antennas = MAX(1, remote_num_antennas);
	uint8_t local_antennas = CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS;

	const struct bt_le_cs_set_procedure_parameters_param proc_params = {
		.config_id = 0,
		.max_procedure_len = 1000,
		.min_procedure_interval = 1,
		.max_procedure_interval = 100,
		.max_procedure_count = 0,
		.min_subevent_len = 10000,
		.max_subevent_len = 75000,
		.tone_antenna_config_selection =
			select_tone_antenna_config(peer_antennas, local_antennas),
		.phy = BT_LE_CS_PROCEDURE_PHY_2M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna =
			preferred_peer_antenna_mask(peer_antennas),
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	err = bt_le_cs_set_procedure_parameters(connection, &proc_params);
	if (err) {
		LOG_ERR("Reflector procedure params failed (err %d)", err);
		at_cmd_respond("+REFLECTOR ERROR");
		return;
	}

	LOG_INF("Reflector: CS setup complete, ready for ranging");
	at_cmd_respond("+REFLECTOR READY");
}

/* --- CS callbacks (reflector role) --- */

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	/* Only handle for reflector connections */
	if (conn != connection || current_role != BT_MGR_ROLE_REFLECTOR) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("Reflector: CS config created (id %u)", config->id);
		k_sem_give(&sem_reflector_config);
	} else {
		LOG_WRN("Reflector: CS config failed (0x%02x)", status);
	}
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	if (conn != connection || current_role != BT_MGR_ROLE_REFLECTOR) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("Reflector: CS security enabled");
	} else {
		LOG_WRN("Reflector: CS security failed (0x%02x)", status);
	}
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	if (conn != connection || current_role != BT_MGR_ROLE_REFLECTOR) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS && params->state == 1) {
		LOG_INF("Reflector: CS procedures enabled (interval %u)",
			params->procedure_interval);
	} else if (status == BT_HCI_ERR_SUCCESS && params->state == 0) {
		LOG_INF("Reflector: CS procedures disabled");
	} else {
		LOG_WRN("Reflector: procedure enable failed (0x%02x)", status);
	}
}

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	if (conn != connection || current_role != BT_MGR_ROLE_REFLECTOR) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS) {
		remote_num_antennas = params->num_antennas_supported;
		LOG_INF("Reflector: CS capability exchange completed, "
			"remote has %u antenna(s)", params->num_antennas_supported);
	} else {
		LOG_WRN("Reflector: CS capability exchange failed (0x%02x)", status);
	}
}

/* --- Connection callbacks --- */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (session_mgr_owns_conn(conn)) {
		return;
	}

	if (err) {
		LOG_WRN("Connection failed (err %u)", err);
		at_cmd_respond("+CONNECT FAIL");
		return;
	}

	connection = bt_conn_ref(conn);
	advertising = false;

	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));
	LOG_INF("Connected: %s", addr_str);
	at_cmd_respond("+CONNECTED");

	/* If we're in reflector role, start CS setup */
	if (current_role == BT_MGR_ROLE_REFLECTOR) {
		k_sem_reset(&sem_reflector_config);
		k_thread_create(&reflector_setup_thread_data,
				reflector_setup_stack,
				K_THREAD_STACK_SIZEOF(reflector_setup_stack),
				reflector_setup_thread, NULL, NULL, NULL,
				REFLECTOR_SETUP_PRIORITY, 0, K_NO_WAIT);
		k_thread_name_set(&reflector_setup_thread_data,
				  "refl_setup");
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	if (session_mgr_owns_conn(conn)) {
		return;
	}

	LOG_INF("Disconnected (reason 0x%02x)", reason);

	/* Unblock reflector setup thread if waiting */
	k_sem_give(&sem_reflector_config);

	if (connection) {
		bt_conn_unref(connection);
		connection = NULL;
	}
	remote_num_antennas = 0;

	at_cmd_respond("+DISCONNECTED");

	/* Auto-restart advertising if configured */
	if (current_role == BT_MGR_ROLE_REFLECTOR &&
	    app_settings_get_adv_autostart()) {
		int adv_err = bt_mgr_adv_start();

		if (adv_err) {
			LOG_WRN("Adv autostart failed (err %d)", adv_err);
		}
	}
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
};

int bt_mgr_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth initialized");
	return 0;
}

int bt_mgr_set_role(enum bt_mgr_role role)
{
	if (connection) {
		LOG_WRN("Cannot change role while connected");
		return -EBUSY;
	}

	if (advertising) {
		LOG_WRN("Stop advertising before changing role");
		return -EBUSY;
	}

	if (scanning) {
		LOG_WRN("Stop scanning before changing role");
		return -EBUSY;
	}

	current_role = role;
	return 0;
}

enum bt_mgr_role bt_mgr_get_role(void)
{
	return current_role;
}

int bt_mgr_adv_start(void)
{
	if (current_role != BT_MGR_ROLE_REFLECTOR) {
		LOG_WRN("Advertising requires reflector role");
		return -EINVAL;
	}

	if (advertising) {
		return -EALREADY;
	}

	if (connection) {
		return -EBUSY;
	}

	/* Update ad data with current device name */
	const char *name = app_settings_get_name();

	ad[2].data = name;
	ad[2].data_len = strlen(name);

	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		LOG_ERR("bt_le_adv_start failed (err %d)", err);
		return err;
	}

	advertising = true;
	LOG_INF("Advertising as \"%s\"", name);
	return 0;
}

int bt_mgr_adv_stop(void)
{
	if (!advertising) {
		return 0;
	}

	int err = bt_le_adv_stop();

	if (err) {
		LOG_ERR("bt_le_adv_stop failed (err %d)", err);
		return err;
	}

	advertising = false;
	LOG_INF("Advertising stopped");
	return 0;
}

bool bt_mgr_is_advertising(void)
{
	return advertising;
}

/* --- Scanning --- */

static bool scan_already_seen(const bt_addr_le_t *addr)
{
	for (int i = 0; i < scan_seen_count; i++) {
		if (bt_addr_le_cmp(&scan_seen[i], addr) == 0) {
			return true;
		}
	}
	return false;
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	if (!scanning) {
		return;
	}

	const bt_addr_le_t *addr = device_info->recv_info->addr;

	if (scan_already_seen(addr)) {
		return;
	}

	if (scan_seen_count < SCAN_MAX_DEVICES) {
		bt_addr_le_copy(&scan_seen[scan_seen_count++], addr);
	}

	/* Extract device name from advertisement data if present */
	const char *name = NULL;
	uint8_t name_len = 0;
	struct net_buf_simple *ad = device_info->adv_data;
	struct net_buf_simple_state state;

	net_buf_simple_save(ad, &state);

	while (ad->len > 1) {
		uint8_t len = net_buf_simple_pull_u8(ad);
		uint8_t type;

		if (len == 0 || len > ad->len) {
			break;
		}

		type = net_buf_simple_pull_u8(ad);
		if (type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) {
			name = (const char *)ad->data;
			name_len = len - 1;
			break;
		}
		net_buf_simple_pull(ad, len - 1);
	}

	net_buf_simple_restore(ad, &state);

	const uint8_t *a = addr->a.val;
	char resp[64];

	if (name && name_len > 0) {
		int off = snprintk(resp, sizeof(resp),
				   "+SCAN:%02X%02X%02X%02X%02X%02X,%d,",
				   a[5], a[4], a[3], a[2], a[1], a[0],
				   device_info->recv_info->rssi);
		uint8_t copy_len = MIN(name_len, sizeof(resp) - off - 1);

		memcpy(resp + off, name, copy_len);
		resp[off + copy_len] = '\0';
	} else {
		snprintk(resp, sizeof(resp), "+SCAN:%02X%02X%02X%02X%02X%02X,%d",
			 a[5], a[4], a[3], a[2], a[1], a[0],
			 device_info->recv_info->rssi);
	}
	at_cmd_respond(resp);

	if (scan_found_cb) {
		char mac_hex[13];

		snprintk(mac_hex, sizeof(mac_hex),
			 "%02X%02X%02X%02X%02X%02X",
			 a[5], a[4], a[3], a[2], a[1], a[0]);
		scan_found_cb(mac_hex);
	}
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, NULL, NULL);

static void scan_timeout_handler(struct k_work *work)
{
	bt_mgr_scan_stop();
}

static int scan_module_init(void)
{
	static bool initialized;

	if (initialized) {
		return 0;
	}

	struct bt_scan_init_param param = {
		.connect_if_match = 0,
	};

	bt_scan_init(&param);
	bt_scan_cb_register(&scan_cb);

	int err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID,
				     BT_UUID_RANGING_SERVICE);
	if (err) {
		LOG_ERR("Scan filter add failed (err %d)", err);
		return err;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_ERR("Scan filter enable failed (err %d)", err);
		return err;
	}

	k_work_init_delayable(&scan_timeout_work, scan_timeout_handler);
	initialized = true;
	return 0;
}

int bt_mgr_scan_start(uint16_t timeout_sec)
{
	if (current_role != BT_MGR_ROLE_INITIATOR) {
		LOG_WRN("Scanning requires initiator role");
		return -EINVAL;
	}

	if (scanning) {
		return -EALREADY;
	}

	int err = scan_module_init();

	if (err) {
		return err;
	}

	scan_seen_count = 0;

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
	if (err) {
		LOG_ERR("bt_scan_start failed (err %d)", err);
		return err;
	}

	scanning = true;
	LOG_INF("Scanning started");

	if (timeout_sec > 0) {
		k_work_schedule(&scan_timeout_work, K_SECONDS(timeout_sec));
	}

	return 0;
}

int bt_mgr_scan_stop(void)
{
	if (!scanning) {
		return 0;
	}

	k_work_cancel_delayable(&scan_timeout_work);

	int err = bt_scan_stop();

	if (err) {
		LOG_ERR("bt_scan_stop failed (err %d)", err);
		return err;
	}

	scanning = false;
	LOG_INF("Scanning stopped");
	at_cmd_respond("+SCANDONE");
	return 0;
}

bool bt_mgr_is_scanning(void)
{
	return scanning;
}

/* --- Address Lookup --- */

static int parse_hex_mac(const char *hex, bt_addr_t *addr)
{
	if (strlen(hex) != 12) {
		return -EINVAL;
	}

	for (int i = 0; i < 6; i++) {
		char hi = hex[i * 2];
		char lo = hex[i * 2 + 1];

		if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) {
			return -EINVAL;
		}

		char byte_str[3] = {hi, lo, '\0'};
		/* BLE addresses are stored LSB first */
		addr->val[5 - i] = (uint8_t)strtoul(byte_str, NULL, 16);
	}

	return 0;
}

int bt_mgr_scan_lookup(const char *mac_hex, bt_addr_le_t *addr)
{
	bt_addr_t parsed;
	int err = parse_hex_mac(mac_hex, &parsed);

	if (err) {
		return err;
	}

	/* Search scan results for matching address to get correct type */
	for (int i = 0; i < scan_seen_count; i++) {
		if (bt_addr_cmp(&scan_seen[i].a, &parsed) == 0) {
			bt_addr_le_copy(addr, &scan_seen[i]);
			return 0;
		}
	}

	/* Not found in scan results — assume random address */
	addr->type = BT_ADDR_LE_RANDOM;
	bt_addr_copy(&addr->a, &parsed);
	return 0;
}

void bt_mgr_set_scan_found_cb(bt_mgr_scan_found_cb_t cb)
{
	scan_found_cb = cb;
}
