// emu_present.h — drive the active emulated controller from g_in.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_EMU_PRESENT_H
#define PICOPUCK_EMU_PRESENT_H

#include <stdint.h>

// Build + send the emulated controller's input report each loop (rate-limited).
void emu_present_task(void);

// HID feature/output dispatch for emulated modes (routed from the shared
// tud_hid callbacks when not in a puck mode).
uint16_t emu_get_report(uint8_t report_id, uint8_t type, uint8_t *buf,
			uint16_t reqlen);
void emu_set_report(uint8_t report_id, uint8_t type, const uint8_t *buf,
		    uint16_t len);

#endif // PICOPUCK_EMU_PRESENT_H
