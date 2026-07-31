// gamepad_util.h -- OpenPuck's thin adapter over the SHARED report-building math
// (src/common/gamepad_util.{h,c}, linked by both OpenPuck and PicoPuck). The pure
// packers live in the shared file under snake_case names; the camelCase wrappers
// here keep OpenPuck's existing call sites unchanged, and the config-coupled
// remapping (psButtonsFromSteam / psFaceNibble, which read g_back/g_qamMap/
// g_abSwap) stays OpenPuck-local in gamepad_util.cpp.
#pragma once
#include <stdint.h>
#include "triton.h"
#include "src/common/gamepad_util.h"

// camelCase compatibility wrappers -> shared snake_case implementations.
static inline uint8_t swStick(int16_t v, bool invert)
{
	return sw_stick(v, invert);
}
static inline void psNeutralCalib(uint8_t *buf)
{
	ps_neutral_calib(buf);
}
static inline uint8_t psHatNibble(uint32_t b)
{
	return ps_hat_nibble(b);
}
static inline uint8_t psShouldersByte(uint32_t b, uint8_t lt, uint8_t rt)
{
	return ps_shoulders_byte(b, lt, rt);
}
static inline void touchPackPads(uint8_t *pts, bool lTouch, bool rTouch,
				 uint16_t lx, uint16_t ly, uint16_t rx,
				 uint16_t ry)
{
	touch_pack_pads(pts, lTouch, rTouch, lx, ly, rx, ry);
}
static inline void steamPadsToTouch(uint32_t b, uint16_t touchH, int16_t lpx,
				    int16_t lpy, int16_t rpx, int16_t rpy,
				    uint16_t *lx, uint16_t *ly, uint16_t *rx,
				    uint16_t *ry)
{
	steam_pads_to_touch(b, touchH, lpx, lpy, rpx, rpy, lx, ly, rx, ry);
}
static inline uint32_t tritonFromCode(uint8_t c)
{
	return triton_from_code(c);
}

// Config-coupled (defined in gamepad_util.cpp; read g_abSwap / g_back / g_qamMap):
uint8_t psFaceNibble(uint32_t b); // PS face nibble with A/B + X/Y swap
uint32_t psButtonsFromSteam(uint32_t); // back-paddle + chord-guard + QAM remap
