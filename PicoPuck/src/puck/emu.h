// emu.h — emulated-controller mode interface (Xbox/Switch/PS5/PS3/…).
//
// A puck mode (Steam/Lizard) presents the Valve puck HID; every other mode
// presents ONE HID gamepad built from the active controller's g_in. Each such
// mode provides its USB identity, HID report descriptor, an input-report builder
// (g_in → bytes), and an optional output-report handler (rumble/LED). This is
// the PicoPuck analogue of OpenPuck's mode_*.cpp report builders.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_EMU_H
#define PICOPUCK_EMU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "puck/triton.h"

// HID report types (match TinyUSB's hid_report_type_t values), so mode files
// don't need to pull in tusb.h.
#define PP_HID_INPUT 1
#define PP_HID_OUTPUT 2
#define PP_HID_FEATURE 3

typedef struct emu_mode {
	uint16_t vid, pid, bcd;
	const char *product;
	const uint8_t *report_desc;
	uint16_t report_desc_len;

	// Build the input report for the controller on `slot` into out[] (body,
	// no report id). Returns body length; sets *report_id (0 = no id).
	uint16_t (*build)(int slot, uint8_t *out, uint8_t *report_id);

	// Answer a host feature/input GET_REPORT (calibration/MAC/fw). Fill buf,
	// return length (0 = unhandled). May be NULL.
	uint16_t (*get_report)(int slot, uint8_t report_id, uint8_t type,
			       uint8_t *buf, uint16_t reqlen);

	// Handle a host output/feature SET_REPORT (rumble/LED). May be NULL.
	void (*set_report)(int slot, uint8_t report_id, uint8_t type,
			   const uint8_t *buf, uint16_t len);

	uint8_t poll_ms;  // report cadence (0 → every loop)
} emu_mode_t;

// The emulated mode for `mode` (MODE_* value), or NULL for puck/unknown modes.
const emu_mode_t *emu_mode_for(uint8_t mode);

#endif // PICOPUCK_EMU_H
