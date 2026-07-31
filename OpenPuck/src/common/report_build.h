// report_build.h — shared PlayStation-family USB report builders (DS4 / DualSense
// / DS3). The byte layout (g_in → report bytes) is identical across OpenPuck and
// PicoPuck; only the CONFIG SOURCE differs (OpenPuck globals vs PicoPuck
// settings), so it's passed in via report_cfg_t. Each firmware just fills the cfg
// and calls these.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_REPORT_BUILD_H
#define OPK_COMMON_REPORT_BUILD_H

#include <stdint.h>
#include <stdbool.h>
#include "triton_input.h"
#include "gamepad_util.h"

#ifdef __cplusplus
extern "C" {
#endif

// Per-mode remap config (the pieces the builders need that live in each
// firmware's settings): A/B+X/Y swap, the four back-paddle codes, the QAM code.
typedef struct {
	bool ab_swap;
	uint8_t back[4]; // L4/R4/L5/R5 → button code (0 = none)
	uint8_t qam; // QAM (…) → button code (0 = none)
} report_cfg_t;

// Free-running per-interface sequence state (report counter + touch timestamp).
typedef struct {
	uint8_t ctr;
	uint8_t tstamp;
} report_seq_t;

// Apply the QAM remap, the all-four-back-paddles chord guard, and the four
// back-paddle remaps to a raw Triton button field (uses the shared code→TB_ map).
uint32_t ps_buttons_remap(uint32_t raw, const report_cfg_t *cfg);

// Build a report body into out[]. Return the body length. rid is fixed per mode
// (0x01 for all three) and set by the caller.
uint16_t build_ds4(const puck_input_t *in, const report_cfg_t *cfg,
		   report_seq_t *seq, uint8_t out[63]); // DS4 / HID-gyro
uint16_t build_ps5(const puck_input_t *in, const report_cfg_t *cfg,
		   report_seq_t *seq, uint8_t out[63]); // DualSense
uint16_t build_ds3(const puck_input_t *in, const report_cfg_t *cfg,
		   uint8_t out[48]); // DualShock 3 / Sixaxis
uint16_t build_switch_hori(const puck_input_t *in, const report_cfg_t *cfg,
			   uint8_t out[8]); // Switch HORIPAD (8-byte, no id)

// Switch Pro Controller pieces (the handshake / battery / timer state stays in
// each firmware's mode_switch_pro; these are the shared button-field + IMU math).
// switch_pro_buttons packs the 24-bit JC button field from g_in + remap config.
uint32_t switch_pro_buttons(const puck_input_t *in, const report_cfg_t *cfg);
// switch_pro_imu writes the 36-byte IMU region (3 samples of accel+gyro) of the
// 0x30 report; gyro_scale10 is the user gyro sensitivity ×10 (10 = 1.0×).
void switch_pro_imu(const puck_input_t *in, uint8_t gyro_scale10,
		    uint8_t out36[36]);

#ifdef __cplusplus
}
#endif

#endif // OPK_COMMON_REPORT_BUILD_H
