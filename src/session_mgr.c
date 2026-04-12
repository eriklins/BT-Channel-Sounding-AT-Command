#include "session_mgr.h"
#include "app_settings.h"
#include "at_cmd.h"
#include "iq_output.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/ras.h>
#include <bluetooth/gatt_dm.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(session_mgr, LOG_LEVEL_INF);

#define CS_CONFIG_ID       0
#define NUM_MODE_0_STEPS   3
/* BLE connection interval unit = 1.25 ms */
#define MS_TO_CONN_UNITS(ms) ((ms) * 4 / 5)
#define PROCEDURE_COUNTER_NONE (-1)

#define LOCAL_PROCEDURE_MEM                                                    \
	((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) + \
	 (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))

struct ranging_session {
	bool in_use;
	bool active;
	uint8_t id;
	uint16_t interval_ms;
	bt_addr_le_t peer_addr;
	struct bt_conn *conn;

	/* Setup synchronization */
	struct k_sem sem_connected;
	struct k_sem sem_security;
	struct k_sem sem_mtu_done;
	struct k_sem sem_discovery_done;
	struct k_sem sem_ras_features;
	struct k_sem sem_remote_caps;
	struct k_sem sem_config_created;
	struct k_sem sem_cs_security;

	/* CS state */
	struct bt_conn_le_cs_config cs_config;
	uint32_t ras_feature_bits;
	uint8_t remote_num_antennas;
	int32_t most_recent_local_counter;
	int32_t dropped_counter;
	int32_t pending_peer_counter;
	int16_t pending_freq_compensation;
	struct k_sem sem_local_steps;

	/* Step data buffers */
	struct net_buf_simple local_steps;
	uint8_t local_steps_buf[LOCAL_PROCEDURE_MEM];
	struct net_buf_simple peer_steps;
	uint8_t peer_steps_buf[BT_RAS_PROCEDURE_MEM];

	/* Diagnostics */
	uint32_t cnt_procedures;
	uint32_t cnt_dropped;
	uint32_t cnt_peer_ready;
	uint32_t cnt_peer_data;
	uint32_t cnt_iq_out;
	uint32_t cnt_data_err;
	uint32_t cnt_data_mismatch;
	uint32_t cnt_data_empty;

	int setup_err;
};

static struct ranging_session sessions[SESSION_MGR_MAX_SESSIONS];
static session_mgr_disconnect_cb_t session_disconnect_cb;

/* Setup thread — processes one session setup at a time */
#define SETUP_THREAD_STACK_SIZE 4096
#define SETUP_THREAD_PRIORITY   7
static K_THREAD_STACK_DEFINE(setup_stack, SETUP_THREAD_STACK_SIZE);
static struct k_thread setup_thread_data;
K_MSGQ_DEFINE(setup_msgq, sizeof(uint8_t), SESSION_MGR_MAX_SESSIONS, 1);

/* --- Helpers --- */

static struct ranging_session *session_by_conn(struct bt_conn *conn)
{
	for (int i = 0; i < SESSION_MGR_MAX_SESSIONS; i++) {
		if (sessions[i].in_use && sessions[i].conn == conn) {
			return &sessions[i];
		}
	}
	return NULL;
}

static struct ranging_session *session_by_id(uint8_t id)
{
	if (id < 1 || id > SESSION_MGR_MAX_SESSIONS) {
		return NULL;
	}
	struct ranging_session *s = &sessions[id - 1];

	return s->in_use ? s : NULL;
}

static void session_buf_init(struct ranging_session *s)
{
	net_buf_simple_init_with_data(&s->local_steps, s->local_steps_buf,
				     sizeof(s->local_steps_buf));
	net_buf_simple_reset(&s->local_steps);

	net_buf_simple_init_with_data(&s->peer_steps, s->peer_steps_buf,
				     sizeof(s->peer_steps_buf));
	net_buf_simple_reset(&s->peer_steps);
}

static void session_send_status(struct ranging_session *s, const char *status)
{
	char resp[32];

	snprintk(resp, sizeof(resp), "+RANGE:%u %s", s->id, status);
	at_cmd_respond(resp);
}

/* --- BT Connection Callbacks --- */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (err) {
		LOG_WRN("Session %u connection failed (err %u)", s->id, err);
		s->setup_err = -EIO;
		k_sem_give(&s->sem_connected);
		return;
	}

	LOG_INF("Session %u connected", s->id);
	k_sem_give(&s->sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	LOG_INF("Session %u disconnected (reason 0x%02x)", s->id, reason);

	bool was_active = s->active;

	s->active = false;
	bt_conn_unref(s->conn);
	s->conn = NULL;
	s->in_use = false;

	if (was_active) {
		session_send_status(s, "DISCONNECTED");

		if (session_disconnect_cb) {
			session_disconnect_cb(s->id);
		}
	}

	/* Release setup semaphores in case setup was in progress */
	k_sem_give(&s->sem_connected);
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	if (!session_by_conn(conn)) {
		return true;
	}
	/* Reject peer parameter changes for session connections */
	return false;
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (err) {
		LOG_ERR("Session %u security failed (err %d)", s->id, err);
		s->setup_err = -EACCES;
	}
	k_sem_give(&s->sem_security);
}

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

/* --- CS Callbacks --- */

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("Session %u CS capability exchange failed (0x%02x)", s->id, status);
		s->setup_err = -EIO;
	} else {
		s->remote_num_antennas = params->num_antennas_supported;
		LOG_INF("Session %u remote has %u antenna(s), %u max paths",
			s->id, params->num_antennas_supported,
			params->max_antenna_paths_supported);
	}
	k_sem_give(&s->sem_remote_caps);
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS) {
		s->cs_config = *config;
		LOG_INF("Session %u CS config created", s->id);
	} else {
		LOG_WRN("Session %u CS config failed (0x%02x)", s->id, status);
		s->setup_err = -EIO;
	}
	k_sem_give(&s->sem_config_created);
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("Session %u CS security failed (0x%02x)", s->id, status);
		s->setup_err = -EIO;
	}
	k_sem_give(&s->sem_cs_security);
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (status == BT_HCI_ERR_SUCCESS && params->state == 1) {
		LOG_INF("Session %u CS procedures enabled (interval: %u)",
			s->id, params->procedure_interval);
	} else if (status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("Session %u procedure enable failed (0x%02x)", s->id, status);
	}
}

/* --- Lightweight IQ Extraction via RAS subevent data parser --- */

#define TONE_QUALITY_OK_THRESHOLD 15

struct iq_parse_ctx {
	struct iq_report *report;
	uint8_t ok_tone_count[IQ_MAX_ANTENNA_PATHS];
};

static inline void iq_set_valid(struct iq_antenna_path *p, uint8_t tone)
{
	p->valid_mask[tone >> 3] |= (uint8_t)(1u << (tone & 0x7));
}

static inline void iq_set_quality(struct iq_antenna_path *p, uint8_t tone,
				  uint8_t quality)
{
	uint8_t shift = (tone & 0x3) * 2u;
	uint8_t idx = tone >> 2;

	p->tone_quality[idx] &= (uint8_t)~(0x3u << shift);
	p->tone_quality[idx] |= (uint8_t)((quality & 0x3u) << shift);
}

static bool iq_step_cb(struct bt_le_cs_subevent_step *local_step,
		       struct bt_le_cs_subevent_step *peer_step, void *user_data)
{
	struct iq_parse_ctx *ctx = user_data;
	struct iq_report *report = ctx->report;

	if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_1) {
		const struct bt_hci_le_cs_step_data_mode_1 *local_rtt =
			(const struct bt_hci_le_cs_step_data_mode_1 *)local_step->data;
		const struct bt_hci_le_cs_step_data_mode_1 *peer_rtt =
			(const struct bt_hci_le_cs_step_data_mode_1 *)peer_step->data;

		if (local_rtt->packet_quality_aa_check ==
			    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL &&
		    local_rtt->packet_rssi != BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE &&
		    local_rtt->toa_tod_initiator !=
			    BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE &&
		    peer_rtt->packet_quality_aa_check ==
			    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL &&
		    peer_rtt->packet_rssi != BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE &&
		    peer_rtt->tod_toa_reflector !=
			    BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE) {
			report->rtt_half_ns += local_rtt->toa_tod_initiator -
					       peer_rtt->tod_toa_reflector;
			report->rtt_count++;
		}
		return true;
	}

	if (local_step->mode == 0) {
		/* The PDF (p.16) calls this out: mode-0 produces the FFO that the
		 * Initiator uses to align timing/phase. We expose the controller's
		 * measured frequency offset (initiator side) for downstream sanity
		 * checks; the per-procedure value from the subevent header is
		 * preferred when available, this is the per-step fallback.
		 */
		const struct bt_hci_le_cs_step_data_mode_0_initiator *m0i =
			(const struct bt_hci_le_cs_step_data_mode_0_initiator *)
				local_step->data;

		if (report->freq_compensation == IQ_FREQ_COMP_NA &&
		    m0i->measured_freq_offset !=
			    BT_HCI_LE_CS_SUBEVENT_RESULT_FREQ_COMPENSATION_NOT_AVAILABLE) {
			report->freq_compensation = (int16_t)m0i->measured_freq_offset;
		}
		return true;
	}

	if (local_step->mode != BT_HCI_OP_LE_CS_MAIN_MODE_2) {
		return true;
	}

	/* CS channels 2-76 → array indices 0-74 */
	if (local_step->channel < 2 || local_step->channel > 76) {
		return true;
	}
	uint8_t idx = local_step->channel - 2;

	/* Mode 2: [antenna_permutation_index:1][tone_info:4*n_ap] */
	uint8_t perm_idx = local_step->data[0];
	const struct bt_hci_le_cs_step_data_tone_info *local_ti =
		(const struct bt_hci_le_cs_step_data_tone_info *)(local_step->data + 1);
	const struct bt_hci_le_cs_step_data_tone_info *peer_ti =
		(const struct bt_hci_le_cs_step_data_tone_info *)(peer_step->data + 1);

	for (uint8_t t = 0; t < report->n_ap; t++) {
		int ap = bt_le_cs_get_antenna_path(report->n_ap, perm_idx, t);

		if (ap < 0 || ap >= IQ_MAX_ANTENNA_PATHS) {
			continue;
		}

		if (local_ti[t].quality_indicator == BT_HCI_LE_CS_TONE_QUALITY_HIGH &&
		    peer_ti[t].quality_indicator == BT_HCI_LE_CS_TONE_QUALITY_HIGH) {
			ctx->ok_tone_count[ap]++;
		}

		/* PCT == 0xFFFFFF means "not available" — leave the tone marked
		 * invalid (the caller pre-fills tone_quality with UNAVAILABLE).
		 */
		const uint8_t *lpct = local_ti[t].phase_correction_term;
		const uint8_t *ppct = peer_ti[t].phase_correction_term;
		bool local_na = (lpct[0] == 0xFF && lpct[1] == 0xFF && lpct[2] == 0xFF);
		bool peer_na = (ppct[0] == 0xFF && ppct[1] == 0xFF && ppct[2] == 0xFF);

		if (local_na || peer_na) {
			continue;
		}

		struct bt_le_cs_iq_sample local_iq = bt_le_cs_parse_pct(lpct);
		struct bt_le_cs_iq_sample peer_iq = bt_le_cs_parse_pct(ppct);

		report->ap[ap].i_local[idx] = local_iq.i;
		report->ap[ap].q_local[idx] = local_iq.q;
		report->ap[ap].i_remote[idx] = peer_iq.i;
		report->ap[ap].q_remote[idx] = peer_iq.q;
		iq_set_valid(&report->ap[ap], idx);

		/* Per-tone quality = worst (numerically max) of local/peer */
		uint8_t qmax = MAX(local_ti[t].quality_indicator,
				   peer_ti[t].quality_indicator);
		iq_set_quality(&report->ap[ap], idx, qmax);
	}

	return true;
}

static bool iq_ranging_header_cb(struct ras_ranging_header *ranging_header,
				 void *user_data)
{
	struct iq_parse_ctx *ctx = user_data;

	ctx->report->n_ap = ((ranging_header->antenna_paths_mask & BIT(0)) +
			     ((ranging_header->antenna_paths_mask & BIT(1)) >> 1) +
			     ((ranging_header->antenna_paths_mask & BIT(2)) >> 2) +
			     ((ranging_header->antenna_paths_mask & BIT(3)) >> 3));
	return true;
}

/* --- Ranging Data Callbacks --- */

static void ranging_data_cb(struct bt_conn *conn, uint16_t ranging_counter, int err)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	s->cnt_peer_data++;

	if (err) {
		s->cnt_data_err++;
		LOG_ERR("Session %u ranging data error (counter %u, err %d)",
			s->id, ranging_counter, err);
		net_buf_simple_reset(&s->local_steps);
		k_sem_give(&s->sem_local_steps);
		return;
	}

	if (ranging_counter != s->most_recent_local_counter) {
		s->cnt_data_mismatch++;
		LOG_INF("Session %u ranging counter mismatch (peer: %u, local: %d)",
			s->id, ranging_counter, s->most_recent_local_counter);
		net_buf_simple_reset(&s->local_steps);
		k_sem_give(&s->sem_local_steps);
		return;
	}

	if (s->local_steps.len == 0) {
		s->cnt_data_empty++;
		LOG_WRN("Session %u all subevents aborted", s->id);
		net_buf_simple_reset(&s->local_steps);
		k_sem_give(&s->sem_local_steps);
		if (!(s->ras_feature_bits & RAS_FEAT_REALTIME_RD)) {
			net_buf_simple_reset(&s->peer_steps);
		}
		return;
	}

	/* Parse IQ data directly — lightweight, no IFFT/distance computation */
	static struct iq_report report;
	struct iq_parse_ctx ctx = { .report = &report };

	memset(&report, 0, sizeof(report));
	memset(ctx.ok_tone_count, 0, sizeof(ctx.ok_tone_count));

	/* Mark every tone as UNAVAILABLE quality until iq_step_cb proves
	 * otherwise; valid_mask stays all-zero by the same memset.
	 */
	for (uint8_t ap = 0; ap < IQ_MAX_ANTENNA_PATHS; ap++) {
		memset(report.ap[ap].tone_quality,
		       (BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE << 6) |
			       (BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE << 4) |
			       (BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE << 2) |
			       BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE,
		       IQ_TONE_QUALITY_BYTES);
	}

	/* Seed the per-procedure FFO captured in subevent_result_cb. */
	report.freq_compensation = s->pending_freq_compensation;
	s->pending_freq_compensation = IQ_FREQ_COMP_NA;

	bt_ras_rreq_rd_subevent_data_parse(&s->peer_steps, &s->local_steps,
					   BT_CONN_LE_CS_ROLE_INITIATOR,
					   iq_ranging_header_cb, NULL,
					   iq_step_cb, &ctx);

	for (uint8_t ap = 0; ap < report.n_ap; ap++) {
		report.ap[ap].quality_ok =
			(ctx.ok_tone_count[ap] >= TONE_QUALITY_OK_THRESHOLD);
	}

	net_buf_simple_reset(&s->local_steps);
	if (!(s->ras_feature_bits & RAS_FEAT_REALTIME_RD)) {
		net_buf_simple_reset(&s->peer_steps);
	}
	k_sem_give(&s->sem_local_steps);

	s->cnt_iq_out++;
	iq_output_report(s->id, &report);
}

static void fetch_peer_ranging_data(struct ranging_session *s,
				    uint16_t ranging_counter);

static void subevent_result_cb(struct bt_conn *conn,
			       struct bt_conn_le_cs_subevent_result *result)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (s->dropped_counter == result->header.procedure_counter) {
		return;
	}

	if (s->most_recent_local_counter !=
	    bt_ras_rreq_get_ranging_counter(result->header.procedure_counter)) {
		int sem_state = k_sem_take(&s->sem_local_steps, K_NO_WAIT);

		if (sem_state < 0) {
			s->dropped_counter = result->header.procedure_counter;
			s->cnt_dropped++;
			LOG_DBG("Session %u dropped subevent (waiting for peer)", s->id);
			return;
		}

		s->most_recent_local_counter =
			bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);

		/* New procedure starting — capture FFO from the subevent header.
		 * Per spec, this is the controller's frequency compensation value
		 * (0.01 ppm units) and is only valid on the Initiator role.
		 */
		if (result->header.frequency_compensation !=
		    BT_HCI_LE_CS_SUBEVENT_RESULT_FREQ_COMPENSATION_NOT_AVAILABLE) {
			s->pending_freq_compensation =
				(int16_t)result->header.frequency_compensation;
		} else {
			s->pending_freq_compensation = IQ_FREQ_COMP_NA;
		}
	}

	if (result->header.subevent_done_status == BT_CONN_LE_CS_SUBEVENT_ABORTED) {
		/* Skip aborted subevents */
	} else if (result->step_data_buf) {
		if (result->step_data_buf->len <=
		    net_buf_simple_tailroom(&s->local_steps)) {
			uint16_t len = result->step_data_buf->len;
			uint8_t *data = net_buf_simple_pull_mem(result->step_data_buf, len);

			net_buf_simple_add_mem(&s->local_steps, data, len);
		} else {
			LOG_ERR("Session %u step buffer overflow", s->id);
			net_buf_simple_reset(&s->local_steps);
			s->dropped_counter = result->header.procedure_counter;
			return;
		}
	}

	s->dropped_counter = PROCEDURE_COUNTER_NONE;

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE) {
		s->cnt_procedures++;
		s->most_recent_local_counter =
			bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);

		/* Check if peer data ready notification arrived before local
		 * procedure completed (race with ranging_data_ready_cb).
		 */
		if (s->pending_peer_counter == s->most_recent_local_counter) {
			s->pending_peer_counter = PROCEDURE_COUNTER_NONE;
			fetch_peer_ranging_data(s, s->most_recent_local_counter);
		}
	} else if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED) {
		LOG_WRN("Session %u procedure %u aborted",
			s->id, result->header.procedure_counter);
		net_buf_simple_reset(&s->local_steps);
		k_sem_give(&s->sem_local_steps);
	}
}

static void fetch_peer_ranging_data(struct ranging_session *s, uint16_t ranging_counter)
{
	int err = bt_ras_rreq_cp_get_ranging_data(s->conn, &s->peer_steps,
						  ranging_counter,
						  ranging_data_cb);
	if (err) {
		LOG_ERR("Session %u get ranging data failed (err %d)", s->id, err);
		net_buf_simple_reset(&s->local_steps);
		net_buf_simple_reset(&s->peer_steps);
		k_sem_give(&s->sem_local_steps);
	}
}

static void ranging_data_ready_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	s->cnt_peer_ready++;

	if (ranging_counter == s->most_recent_local_counter) {
		fetch_peer_ranging_data(s, ranging_counter);
	} else {
		/* Local procedure not complete yet — save for later */
		s->pending_peer_counter = ranging_counter;
	}
}

static void ranging_data_overwritten_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	struct ranging_session *s = session_by_conn(conn);

	if (s) {
		LOG_INF("Session %u ranging data overwritten (counter %u)",
			s->id, ranging_counter);
	}
}

/* --- GATT Discovery Callbacks --- */

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		bt_gatt_dm_data_release(dm);
		return;
	}

	LOG_INF("Session %u GATT discovery complete", s->id);

	int err = bt_ras_rreq_alloc_and_assign_handles(dm, conn);

	if (err) {
		LOG_ERR("RAS RREQ handle assignment failed (err %d)", err);
		s->setup_err = -EIO;
	}

	bt_gatt_dm_data_release(dm);
	k_sem_give(&s->sem_discovery_done);
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *context)
{
	struct ranging_session *s = session_by_conn(conn);

	if (s) {
		LOG_WRN("Session %u RAS service not found", s->id);
		s->setup_err = -ENOENT;
		k_sem_give(&s->sem_discovery_done);
	}
}

static void discovery_error_found_cb(struct bt_conn *conn, int err, void *context)
{
	struct ranging_session *s = session_by_conn(conn);

	if (s) {
		LOG_ERR("Session %u discovery error (err %d)", s->id, err);
		s->setup_err = -EIO;
		k_sem_give(&s->sem_discovery_done);
	}
}

static struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

/* --- RAS Feature Read Callback --- */

static void ras_features_read_cb(struct bt_conn *conn, uint32_t feature_bits, int err)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (err) {
		LOG_WRN("Session %u RAS features read failed (err %d)", s->id, err);
	} else {
		LOG_INF("Session %u RAS features: 0x%x", s->id, feature_bits);
		s->ras_feature_bits = feature_bits;
	}
	k_sem_give(&s->sem_ras_features);
}

/* --- MTU Exchange Callback --- */

static struct bt_gatt_exchange_params mtu_params;

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	struct ranging_session *s = session_by_conn(conn);

	if (!s) {
		return;
	}

	if (err) {
		LOG_ERR("Session %u MTU exchange failed (err %d)", s->id, err);
		s->setup_err = -EIO;
	} else {
		LOG_INF("Session %u MTU: %u", s->id, bt_gatt_get_mtu(conn));
	}
	k_sem_give(&s->sem_mtu_done);
}

/* --- Register All Callbacks --- */

BT_CONN_CB_DEFINE(session_conn_cbs) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.le_param_req = le_param_req,
	.security_changed = security_changed,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
	.le_cs_subevent_data_available = subevent_result_cb,
};

/* --- Setup Sequence --- */

#define SETUP_STEP(s, call, msg)                                    \
	do {                                                        \
		int _err = (call);                                  \
		if (_err) {                                         \
			LOG_ERR("Session %u: " msg " (err %d)",    \
				(s)->id, _err);                     \
			session_send_status((s), "ERROR " msg);     \
			goto fail;                                  \
		}                                                   \
	} while (0)

#define SETUP_WAIT_TIMEOUT(s, sem, timeout, msg)                            \
	do {                                                               \
		if (k_sem_take(&(s)->sem, timeout) != 0) {                 \
			LOG_ERR("Session %u: " msg " timeout", (s)->id);  \
			session_send_status((s), "ERROR " msg " timeout"); \
			goto fail;                                         \
		}                                                          \
		if ((s)->setup_err) {                                      \
			LOG_ERR("Session %u: " msg " failed", (s)->id);   \
			session_send_status((s), "ERROR " msg);             \
			goto fail;                                         \
		}                                                          \
		if (!(s)->conn) {                                          \
			LOG_ERR("Session %u: disconnected during " msg,    \
				(s)->id);                                   \
			session_send_status((s), "ERROR " msg);             \
			return;                                            \
		}                                                          \
	} while (0)

#define SETUP_WAIT(s, sem, msg) SETUP_WAIT_TIMEOUT(s, sem, K_SECONDS(10), msg)

static void session_setup(struct ranging_session *s)
{
	session_send_status(s, "CONNECTING");

	/* 1. Connect */
	uint16_t conn_int_ms = app_settings_get_conn_interval_ms();
	uint16_t conn_int_units = MS_TO_CONN_UNITS(conn_int_ms);

	struct bt_conn_le_create_param create_param =
		BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
					     BT_GAP_SCAN_FAST_INTERVAL,
					     BT_GAP_SCAN_FAST_WINDOW);
	struct bt_le_conn_param conn_param =
		*BT_LE_CONN_PARAM(conn_int_units, conn_int_units, 0,
				   BT_GAP_MS_TO_CONN_TIMEOUT(4000));

	SETUP_STEP(s, bt_conn_le_create(&s->peer_addr, &create_param,
					&conn_param, &s->conn),
		   "bt_conn_le_create");
	SETUP_WAIT(s, sem_connected, "connection");

	/* 2. Security */
	SETUP_STEP(s, bt_conn_set_security(s->conn, BT_SECURITY_L2), "security");
	SETUP_WAIT_TIMEOUT(s, sem_security, K_SECONDS(30), "security");

	/* 3. MTU exchange */
	mtu_params.func = mtu_exchange_cb;
	SETUP_STEP(s, bt_gatt_exchange_mtu(s->conn, &mtu_params), "MTU exchange");
	SETUP_WAIT(s, sem_mtu_done, "MTU exchange");

	/* 4. GATT discovery */
	SETUP_STEP(s, bt_gatt_dm_start(s->conn, BT_UUID_RANGING_SERVICE,
				       &discovery_cb, NULL),
		   "GATT discovery");
	SETUP_WAIT(s, sem_discovery_done, "GATT discovery");

	/* 5. CS default settings */
	const struct bt_le_cs_set_default_settings_param default_settings = {
		.enable_initiator_role = true,
		.enable_reflector_role = false,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	SETUP_STEP(s, bt_le_cs_set_default_settings(s->conn, &default_settings),
		   "CS default settings");

	/* 6. RAS features */
	SETUP_STEP(s, bt_ras_rreq_read_features(s->conn, ras_features_read_cb),
		   "RAS features read");
	SETUP_WAIT(s, sem_ras_features, "RAS features");

	/* 7. RAS subscriptions */
	bool realtime_rd = s->ras_feature_bits & RAS_FEAT_REALTIME_RD;

	if (realtime_rd) {
		SETUP_STEP(s, bt_ras_rreq_realtime_rd_subscribe(
				   s->conn, &s->peer_steps, ranging_data_cb),
			   "RAS realtime subscribe");
	} else {
		SETUP_STEP(s, bt_ras_rreq_rd_overwritten_subscribe(
				   s->conn, ranging_data_overwritten_cb),
			   "RAS overwritten subscribe");
		SETUP_STEP(s, bt_ras_rreq_rd_ready_subscribe(
				   s->conn, ranging_data_ready_cb),
			   "RAS ready subscribe");
		SETUP_STEP(s, bt_ras_rreq_on_demand_rd_subscribe(s->conn),
			   "RAS on-demand subscribe");
		SETUP_STEP(s, bt_ras_rreq_cp_subscribe(s->conn),
			   "RAS CP subscribe");
	}

	/* 8. CS capabilities exchange */
	SETUP_STEP(s, bt_le_cs_read_remote_supported_capabilities(s->conn),
		   "CS capabilities");
	SETUP_WAIT(s, sem_remote_caps, "CS capabilities");

	/* 9. CS config */
	struct bt_le_cs_create_config_params config_params = {
		.id = CS_CONFIG_ID,
		.mode = BT_CONN_LE_CS_MAIN_MODE_2_SUB_MODE_1,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 5,
		.main_mode_repetition = 0,
		.mode_0_steps = NUM_MODE_0_STEPS,
		.role = BT_CONN_LE_CS_ROLE_INITIATOR,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 1,
		.channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
		.ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
		.ch3c_jump = 2,
	};

	bt_le_cs_set_valid_chmap_bits(config_params.channel_map);

	SETUP_STEP(s, bt_le_cs_create_config(s->conn, &config_params,
					     BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE),
		   "CS config create");
	SETUP_WAIT(s, sem_config_created, "CS config");

	/* 10. CS security */
	SETUP_STEP(s, bt_le_cs_security_enable(s->conn), "CS security");
	SETUP_WAIT_TIMEOUT(s, sem_cs_security, K_SECONDS(30), "CS security");

	/* 11. Procedure parameters */
	uint16_t proc_interval = MAX(1, s->interval_ms / conn_int_ms);
	uint8_t local_antennas = CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS;
	uint8_t remote_antennas = MAX(1, s->remote_num_antennas);

	const struct bt_le_cs_set_procedure_parameters_param proc_params = {
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = 1000,
		.min_procedure_interval = proc_interval,
		.max_procedure_interval = proc_interval,
		.max_procedure_count = 0,
		.min_subevent_len = 5000,
		.max_subevent_len = 20000,
		.tone_antenna_config_selection =
			select_tone_antenna_config(local_antennas, remote_antennas),
		.phy = BT_LE_CS_PROCEDURE_PHY_2M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna =
			preferred_peer_antenna_mask(remote_antennas),
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	SETUP_STEP(s, bt_le_cs_set_procedure_parameters(s->conn, &proc_params),
		   "procedure parameters");

	/* 12. Enable procedures */
	struct bt_le_cs_procedure_enable_param enable_param = {
		.config_id = CS_CONFIG_ID,
		.enable = 1,
	};

	SETUP_STEP(s, bt_le_cs_procedure_enable(s->conn, &enable_param),
		   "procedure enable");

	s->active = true;
	session_send_status(s, "ACTIVE");
	LOG_INF("Session %u ranging active (interval %u ms, proc_interval %u)",
		s->id, s->interval_ms, proc_interval);
	return;

fail:
	if (s->conn) {
		bt_conn_disconnect(s->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(s->conn);
		s->conn = NULL;
	}
	s->in_use = false;
}

static void setup_thread(void *p1, void *p2, void *p3)
{
	uint8_t session_idx;

	while (true) {
		k_msgq_get(&setup_msgq, &session_idx, K_FOREVER);

		struct ranging_session *s = &sessions[session_idx];

		if (s->in_use) {
			session_setup(s);
		}
	}
}

/* --- Public API --- */

int session_mgr_init(void)
{
	for (int i = 0; i < SESSION_MGR_MAX_SESSIONS; i++) {
		sessions[i].id = i + 1;
	}

	k_thread_create(&setup_thread_data, setup_stack,
			K_THREAD_STACK_SIZEOF(setup_stack),
			setup_thread, NULL, NULL, NULL,
			SETUP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&setup_thread_data, "cs_setup");

	return 0;
}

int session_mgr_start(const bt_addr_le_t *addr, uint16_t interval_ms,
		      uint8_t *session_id)
{
	struct ranging_session *s = NULL;

	for (int i = 0; i < SESSION_MGR_MAX_SESSIONS; i++) {
		if (!sessions[i].in_use) {
			s = &sessions[i];
			break;
		}
	}

	if (!s) {
		LOG_WRN("No free session slots");
		return -ENOMEM;
	}

	/* Initialize session */
	s->in_use = true;
	s->active = false;
	s->interval_ms = interval_ms;
	s->setup_err = 0;
	s->conn = NULL;
	bt_addr_le_copy(&s->peer_addr, addr);
	s->most_recent_local_counter = PROCEDURE_COUNTER_NONE;
	s->dropped_counter = PROCEDURE_COUNTER_NONE;
	s->pending_peer_counter = PROCEDURE_COUNTER_NONE;
	s->pending_freq_compensation = IQ_FREQ_COMP_NA;
	s->ras_feature_bits = 0;

	k_sem_init(&s->sem_connected, 0, 1);
	k_sem_init(&s->sem_security, 0, 1);
	k_sem_init(&s->sem_mtu_done, 0, 1);
	k_sem_init(&s->sem_discovery_done, 0, 1);
	k_sem_init(&s->sem_ras_features, 0, 1);
	k_sem_init(&s->sem_remote_caps, 0, 1);
	k_sem_init(&s->sem_config_created, 0, 1);
	k_sem_init(&s->sem_cs_security, 0, 1);
	k_sem_init(&s->sem_local_steps, 1, 1);

	session_buf_init(s);

	*session_id = s->id;

	/* Queue setup for the setup thread */
	uint8_t idx = s - sessions;

	k_msgq_put(&setup_msgq, &idx, K_NO_WAIT);

	return 0;
}

int session_mgr_stop(uint8_t session_id)
{
	struct ranging_session *s = session_by_id(session_id);

	if (!s) {
		return -EINVAL;
	}

	if (s->active && s->conn) {
		/* Disable CS procedures */
		struct bt_le_cs_procedure_enable_param disable = {
			.config_id = CS_CONFIG_ID,
			.enable = 0,
		};

		bt_le_cs_procedure_enable(s->conn, &disable);
	}

	if (s->conn) {
		bt_conn_disconnect(s->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		/* Cleanup happens in disconnected_cb */
	} else {
		s->in_use = false;
	}

	session_send_status(s, "STOPPED");
	return 0;
}

bool session_mgr_has_active(void)
{
	for (int i = 0; i < SESSION_MGR_MAX_SESSIONS; i++) {
		if (sessions[i].in_use) {
			return true;
		}
	}
	return false;
}

bool session_mgr_owns_conn(struct bt_conn *conn)
{
	return session_by_conn(conn) != NULL;
}

void session_mgr_diag(void)
{
	for (int i = 0; i < SESSION_MGR_MAX_SESSIONS; i++) {
		struct ranging_session *s = &sessions[i];

		if (!s->in_use) {
			continue;
		}

		char resp[128];

		snprintk(resp, sizeof(resp),
			 "+DIAG:%u proc=%u drop=%u data=%u iq=%u "
			 "err=%u mismatch=%u empty=%u sem=%u",
			 s->id, s->cnt_procedures, s->cnt_dropped,
			 s->cnt_peer_data, s->cnt_iq_out,
			 s->cnt_data_err, s->cnt_data_mismatch,
			 s->cnt_data_empty,
			 k_sem_count_get(&s->sem_local_steps));
		at_cmd_respond(resp);
	}
}

void session_mgr_set_disconnect_cb(session_mgr_disconnect_cb_t cb)
{
	session_disconnect_cb = cb;
}
