// gamepad_util.h — cross-mode report-building math shared by OpenPuck and
// PicoPuck: analog-stick rescaling, Steam-trackpad → absolute-touch mapping, and
// the PlayStation-layout button/hat/face/shoulder packers. Pure functions on the
// canonical TB_* input — no config globals, no transport — so both firmwares link
// the same code. (Config-coupled remapping — back-paddle / QAM / chord — stays in
// each firmware since it reads that firmware's own settings source.)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_GAMEPAD_UTIL_H
#define OPK_COMMON_GAMEPAD_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include "triton_masks.h"

#ifdef __cplusplus
extern "C" {
#endif

// Steam trackpad width; split into left/right halves so both pads co-exist as
// two contacts on one DualSense/DS4 touchpad.
#define TOUCH_PAD_W 1920u

// Write a signed 16-bit value little-endian.
static inline void le16(uint8_t *p, int16_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}

// Configurable button code (1..20) → TB_* flag (shared code map).
static inline uint32_t triton_from_code(uint8_t c)
{
	switch (c) {
	case 1:
		return TB_A;
	case 2:
		return TB_B;
	case 3:
		return TB_X;
	case 4:
		return TB_Y;
	case 5:
		return TB_LB;
	case 6:
		return TB_RB;
	case 7:
		return TB_L3;
	case 8:
		return TB_R3;
	case 9:
		return TB_VIEW;
	case 10:
		return TB_QAM;
	case 11:
		return TB_STEAM;
	case 12:
		return TB_DUP;
	case 13:
		return TB_DDN;
	case 14:
		return TB_DLF;
	case 15:
		return TB_DRT;
	case 16:
		return TB_TOUCH;
	case 17:
		return TB_MUTE;
	case 19:
		return TB_L2; // left trigger (LT / L2 / ZL)
	case 20:
		return TB_R2; // right trigger (RT / R2 / ZR)
	default:
		return 0;
	}
}

uint8_t sw_stick(int16_t v, bool invert); // int16 center0 → u8 center0x80
void ps_neutral_calib(uint8_t *buf); // DS4/DualSense motion-cal payload

uint8_t ps_hat_nibble(uint32_t b); // d-pad → 8-way hat (8 = neutral)
uint8_t ps_face_nibble(uint32_t b,
		       bool ab_swap); // PS face nibble (opt A/B+X/Y swap)
uint8_t ps_shoulders_byte(uint32_t b, uint8_t lt, uint8_t rt);

void steam_pads_to_touch(uint32_t b, uint16_t touch_h, int16_t lpx, int16_t lpy,
			 int16_t rpx, int16_t rpy, uint16_t *lx, uint16_t *ly,
			 uint16_t *rx, uint16_t *ry);
void touch_pack_pads(uint8_t *pts, bool l_touch, bool r_touch, uint16_t lx,
		     uint16_t ly, uint16_t rx, uint16_t ry);

#ifdef __cplusplus
}
#endif

#endif // OPK_COMMON_GAMEPAD_UTIL_H
