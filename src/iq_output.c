#include "iq_output.h"
#include "at_cmd.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(iq_output, LOG_LEVEL_INF);

/*
 * Output format (one line per antenna path per report):
 *   +IQ:<sid>,ap:<ap>,rtt:<half_ns>,rn:<count>,<tq>,ffo:<int|na>,
 *       m:<hex>,q:<hex>,il:[...],ql:[...],ir:[...],qr:[...]
 *
 * Fields:
 *   ffo  – per-procedure frequency compensation in 0.01 ppm units (signed),
 *          or "na" if the controller did not report it.
 *   m    – 75-bit per-tone validity bitmap, packed LSB-first into bytes,
 *          printed as 20 hex chars (low nibble of byte 0 = tones 0..3).
 *   q    – per-tone quality (2 bits/tone), packed LSB-first, printed as 38
 *          hex chars. Values: 0=HIGH, 1=MED, 2=LOW, 3=UNAVAILABLE.
 *
 * IQ values are integers (12-bit signed PCT, range -2048..2047).
 * 75 values per array × 4 arrays = 300 integers per line.
 *
 * Worst case per value: "-2048," = 6 chars. 300 × 6 = 1800 chars for data,
 * plus headers and metadata ≈ 130 bytes, plus brackets = ~1950 bytes per line.
 */
#define IQ_BUF_SIZE 2304

static char iq_buf[IQ_BUF_SIZE];

/* Queue individual antenna-path entries to keep RAM usage flat */
#define IQ_QUEUE_DEPTH 4

struct iq_queue_entry {
	uint8_t session_id;
	uint8_t ap_index;
	int32_t rtt_half_ns;
	uint8_t rtt_count;
	int16_t freq_compensation;
	struct iq_antenna_path path;
};

static struct iq_queue_entry iq_queue[IQ_QUEUE_DEPTH];
static uint8_t iq_prod_idx;
static uint8_t iq_cons_idx;
static K_SEM_DEFINE(iq_queue_sem, 0, IQ_QUEUE_DEPTH);

static bool iq_output_enabled;

void iq_output_set_enabled(bool enabled)
{
	iq_output_enabled = enabled;
}

bool iq_output_is_enabled(void)
{
	return iq_output_enabled;
}

#define IQ_OUTPUT_STACK_SIZE 2048
static K_THREAD_STACK_DEFINE(iq_output_stack, IQ_OUTPUT_STACK_SIZE);
static struct k_thread iq_output_thread_data;

static int append_hex_bytes(char *buf, int pos, int max, const char *label,
			    const uint8_t *bytes, int count)
{
	pos += snprintk(buf + pos, max - pos, "%s", label);

	for (int i = 0; i < count; i++) {
		pos += snprintk(buf + pos, max - pos, "%02x", bytes[i]);
		if (pos >= max - 1) {
			return pos;
		}
	}
	return pos;
}

static int append_int16_array(char *buf, int pos, int max, const char *label,
			      const int16_t *values, int count)
{
	pos += snprintk(buf + pos, max - pos, "%s[", label);

	for (int i = 0; i < count; i++) {
		if (i > 0) {
			pos += snprintk(buf + pos, max - pos, ",%d", values[i]);
		} else {
			pos += snprintk(buf + pos, max - pos, "%d", values[i]);
		}

		if (pos >= max - 1) {
			return pos;
		}
	}

	pos += snprintk(buf + pos, max - pos, "]");
	return pos;
}

static void do_output(const struct iq_queue_entry *e)
{
	const struct iq_antenna_path *path = &e->path;
	const char *tq = path->quality_ok ? "ok" : "bad";
	int pos = 0;

	pos += snprintk(iq_buf + pos, IQ_BUF_SIZE - pos,
			"+IQ:%u,ap:%u,rtt:%d,rn:%u,%s,",
			e->session_id, e->ap_index, e->rtt_half_ns,
			e->rtt_count, tq);

	if (e->freq_compensation == IQ_FREQ_COMP_NA) {
		pos += snprintk(iq_buf + pos, IQ_BUF_SIZE - pos, "ffo:na,");
	} else {
		pos += snprintk(iq_buf + pos, IQ_BUF_SIZE - pos, "ffo:%d,",
				e->freq_compensation);
	}

	pos = append_hex_bytes(iq_buf, pos, IQ_BUF_SIZE, "m:",
			       path->valid_mask, IQ_VALID_MASK_BYTES);
	if (pos < IQ_BUF_SIZE - 1) {
		iq_buf[pos++] = ',';
	}

	pos = append_hex_bytes(iq_buf, pos, IQ_BUF_SIZE, "q:",
			       path->tone_quality, IQ_TONE_QUALITY_BYTES);
	if (pos < IQ_BUF_SIZE - 1) {
		iq_buf[pos++] = ',';
	}

	pos = append_int16_array(iq_buf, pos, IQ_BUF_SIZE,
				 "il:", path->i_local, IQ_NUM_CHANNELS);
	if (pos < IQ_BUF_SIZE - 1) {
		iq_buf[pos++] = ',';
	}

	pos = append_int16_array(iq_buf, pos, IQ_BUF_SIZE,
				 "ql:", path->q_local, IQ_NUM_CHANNELS);
	if (pos < IQ_BUF_SIZE - 1) {
		iq_buf[pos++] = ',';
	}

	pos = append_int16_array(iq_buf, pos, IQ_BUF_SIZE,
				 "ir:", path->i_remote, IQ_NUM_CHANNELS);
	if (pos < IQ_BUF_SIZE - 1) {
		iq_buf[pos++] = ',';
	}

	pos = append_int16_array(iq_buf, pos, IQ_BUF_SIZE,
				 "qr:", path->q_remote, IQ_NUM_CHANNELS);

	iq_buf[MIN(pos, IQ_BUF_SIZE - 1)] = '\0';

	at_cmd_respond(iq_buf);
}

static void iq_output_thread(void *p1, void *p2, void *p3)
{
	while (true) {
		k_sem_take(&iq_queue_sem, K_FOREVER);

		uint8_t idx = iq_cons_idx % IQ_QUEUE_DEPTH;

		do_output(&iq_queue[idx]);
		iq_cons_idx++;
	}
}

void iq_output_report(uint8_t session_id, const struct iq_report *report)
{
	if (!iq_output_enabled) {
		return;
	}

	for (uint8_t ap = 0; ap < report->n_ap; ap++) {
		if ((uint8_t)(iq_prod_idx - iq_cons_idx) >= IQ_QUEUE_DEPTH) {
			LOG_WRN("IQ output queue full, dropping report");
			return;
		}

		uint8_t idx = iq_prod_idx % IQ_QUEUE_DEPTH;

		iq_queue[idx].session_id = session_id;
		iq_queue[idx].ap_index = ap;
		iq_queue[idx].rtt_half_ns = report->rtt_half_ns;
		iq_queue[idx].rtt_count = report->rtt_count;
		iq_queue[idx].freq_compensation = report->freq_compensation;
		memcpy(&iq_queue[idx].path, &report->ap[ap],
		       sizeof(struct iq_antenna_path));
		iq_prod_idx++;

		k_sem_give(&iq_queue_sem);
	}
}

/* Auto-start the output thread */
static int iq_output_init(void)
{
	k_thread_create(&iq_output_thread_data, iq_output_stack,
			K_THREAD_STACK_SIZEOF(iq_output_stack),
			iq_output_thread, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&iq_output_thread_data, "iq_output");
	return 0;
}

SYS_INIT(iq_output_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
