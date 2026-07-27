// gamepad_util.h — shared report-building helpers (ported from OpenPuck).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_GAMEPAD_UTIL_H
#define PICOPUCK_GAMEPAD_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include "puck/triton.h" // brings in SW_TRIG_ON via the shared triton_masks.h

#define TOUCH_PAD_W 1920u

uint8_t sw_stick(int16_t v, bool invert); // int16 center0 → u8 center0x80
void ps_neutral_calib(uint8_t *buf); // DS4/DualSense motion calib payload

uint8_t ps_hat_nibble(uint32_t b); // d-pad → 8-way hat (8 = neutral)
uint8_t ps_face_nibble(uint32_t b); // PlayStation face nibble
uint8_t ps_shoulders_byte(uint32_t b, uint8_t lt, uint8_t rt);

void steam_pads_to_touch(uint32_t b, uint16_t touch_h, int16_t lpx, int16_t lpy,
			 int16_t rpx, int16_t rpy, uint16_t *lx, uint16_t *ly,
			 uint16_t *rx, uint16_t *ry);
void touch_pack_pads(uint8_t *pts, bool l_touch, bool r_touch, uint16_t lx,
		     uint16_t ly, uint16_t rx, uint16_t ry);

#endif // PICOPUCK_GAMEPAD_UTIL_H
