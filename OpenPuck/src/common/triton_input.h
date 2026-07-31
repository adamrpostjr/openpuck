// triton_input.h — the canonical decoded controller input (one per slot), shared
// by OpenPuck and PicoPuck. Every controller decodes into this and every output
// personality reads it, so it must be one definition. (The decoders that FILL it
// differ per transport — RF report 0x45 vs BT — and stay in each triton.{c,cpp}.)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_TRITON_INPUT_H
#define OPK_COMMON_TRITON_INPUT_H

#include <stdint.h>

typedef struct {
	uint32_t buttons; // TB_* bits (see triton_masks.h)
	int16_t lx, ly, rx, ry; // sticks, center 0
	uint8_t lt, rt; // triggers 0..255
	int16_t lpx, lpy, rpx, rpy; // trackpads (SC2 only; 0 for generic pads)
	int16_t ax, ay, az; // accelerometer (0 if none)
	int16_t gx, gy, gz; // gyroscope (0 if none)
} puck_input_t;

#endif // OPK_COMMON_TRITON_INPUT_H
