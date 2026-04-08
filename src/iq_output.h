#ifndef IQ_OUTPUT_H_
#define IQ_OUTPUT_H_

#include <stdint.h>
#include <stdbool.h>

#define IQ_NUM_CHANNELS 75
#define IQ_MAX_ANTENNA_PATHS CONFIG_BT_RAS_MAX_ANTENNA_PATHS

/* Per-tone validity mask: 1 bit per tone, packed LSB-first.
 * Tone n lives in bit (n % 8) of byte (n / 8). 10 bytes covers 80 bits >= 75.
 */
#define IQ_VALID_MASK_BYTES 10

/* Per-tone quality: 2 bits per tone, packed LSB-first.
 * Tone n lives in bits ((n % 4) * 2) of byte (n / 4). 19 bytes covers 76 tones.
 * Values match BT_HCI_LE_CS_TONE_QUALITY_*: 0=HIGH, 1=MED, 2=LOW, 3=UNAVAILABLE.
 */
#define IQ_TONE_QUALITY_BYTES 19

struct iq_antenna_path {
	int16_t i_local[IQ_NUM_CHANNELS];
	int16_t q_local[IQ_NUM_CHANNELS];
	int16_t i_remote[IQ_NUM_CHANNELS];
	int16_t q_remote[IQ_NUM_CHANNELS];
	uint8_t valid_mask[IQ_VALID_MASK_BYTES];
	uint8_t tone_quality[IQ_TONE_QUALITY_BYTES];
	bool quality_ok;
};

/* Sentinel value for freq_compensation when not available (matches HCI). */
#define IQ_FREQ_COMP_NA ((int16_t)0xC000)

struct iq_report {
	struct iq_antenna_path ap[IQ_MAX_ANTENNA_PATHS];
	uint8_t n_ap;
	int32_t rtt_half_ns;
	uint8_t rtt_count;
	int16_t freq_compensation; /* 0.01 ppm units, IQ_FREQ_COMP_NA if absent */
};

void iq_output_set_enabled(bool enabled);
bool iq_output_is_enabled(void);

/**
 * Queue IQ tone data for output over the AT interface.
 * Called from BLE callbacks — returns immediately.
 *
 * @param session_id  Session ID for the output prefix.
 * @param report      Populated IQ report.
 */
void iq_output_report(uint8_t session_id, const struct iq_report *report);

#endif /* IQ_OUTPUT_H_ */
