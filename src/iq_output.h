#ifndef IQ_OUTPUT_H_
#define IQ_OUTPUT_H_

#include <stdint.h>
#include <stdbool.h>

#define IQ_NUM_CHANNELS 75
#define IQ_MAX_ANTENNA_PATHS CONFIG_BT_RAS_MAX_ANTENNA_PATHS

struct iq_antenna_path {
	int16_t i_local[IQ_NUM_CHANNELS];
	int16_t q_local[IQ_NUM_CHANNELS];
	int16_t i_remote[IQ_NUM_CHANNELS];
	int16_t q_remote[IQ_NUM_CHANNELS];
	bool quality_ok;
};

struct iq_report {
	struct iq_antenna_path ap[IQ_MAX_ANTENNA_PATHS];
	uint8_t n_ap;
	int32_t rtt_half_ns;
	uint8_t rtt_count;
};

/**
 * Queue IQ tone data for output over the AT interface.
 * Called from BLE callbacks — returns immediately.
 *
 * @param session_id  Session ID for the output prefix.
 * @param report      Populated IQ report.
 */
void iq_output_report(uint8_t session_id, const struct iq_report *report);

#endif /* IQ_OUTPUT_H_ */
