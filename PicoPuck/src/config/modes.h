// modes.h — USB presentation modes, matching OpenPuck's MODE_* values exactly.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_MODES_H
#define PICOPUCK_MODES_H

#include <stdint.h>
#include <stdbool.h>

#define MODE_STEAM 0     // Valve puck (transparent SC2 / synth for generic pads)
#define MODE_XBOX 1      // Xbox 360 XInput
#define MODE_SW_HORI 2   // Switch HORIPAD
#define MODE_LIZARD 3    // Puck HID, always keyboard+mouse
#define MODE_SW_PRO 4    // Nintendo Switch Pro Controller + gyro
#define MODE_PS5 5       // Sony DualSense + gyro + split trackpad
#define MODE_HIDGYRO 6   // DS4-layout generic HID gamepad + gyro
#define MODE_PS5_GAME 7  // DualSense, clean single-HID
#define MODE_DS4_GAME 8  // DS4, clean single-HID
#define MODE_PS3 9       // DualShock 3 / Sixaxis
#define MODE_MAX 9

// Modes that present the Valve puck HID (Steam personality + its 4 slots).
static inline bool mode_is_puck(uint8_t m)
{
	return m == MODE_STEAM || m == MODE_LIZARD;
}

// Xbox 360 XInput is a vendor USB class (not HID), served by its own custom
// TinyUSB class driver (xinput.c) rather than the emu_mode_t HID framework.
static inline bool mode_is_xinput(uint8_t m)
{
	return m == MODE_XBOX;
}

// The single-controller emulated modes present one HID gamepad built from the
// active controller's g_in (the "clean PS" modes drop the WebUSB/wake ifaces on
// OpenPuck; PicoPuck keeps WebUSB in every mode so the panel always works).
static inline bool mode_is_emulated(uint8_t m)
{
	return !mode_is_puck(m) && m <= MODE_MAX;
}

#endif // PICOPUCK_MODES_H
