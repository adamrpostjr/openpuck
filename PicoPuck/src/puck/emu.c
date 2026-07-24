// emu.c — emulated-controller mode registry (see emu.h).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "config/modes.h"
#include <stddef.h>

// Each emulated mode exposes an emu_mode_t from its own file.
extern const emu_mode_t emu_ds4;          // MODE_HIDGYRO / MODE_DS4_GAME
extern const emu_mode_t emu_ps5;          // MODE_PS5 / MODE_PS5_GAME
extern const emu_mode_t emu_switch_hori;  // MODE_SW_HORI
extern const emu_mode_t emu_switch_pro;   // MODE_SW_PRO
extern const emu_mode_t emu_ps3;          // MODE_PS3

// Not yet ported (needs machinery beyond the plain-HID framework):
//   MODE_XBOX — XInput is a vendor interface class, not HID.
const emu_mode_t *emu_mode_for(uint8_t mode)
{
	switch (mode) {
	case MODE_SW_HORI: return &emu_switch_hori;
	case MODE_SW_PRO: return &emu_switch_pro;
	case MODE_PS5:
	case MODE_PS5_GAME: return &emu_ps5;
	case MODE_HIDGYRO:
	case MODE_DS4_GAME: return &emu_ds4;
	case MODE_PS3: return &emu_ps3;
	default: return NULL;  // puck fallback until ported
	}
}
