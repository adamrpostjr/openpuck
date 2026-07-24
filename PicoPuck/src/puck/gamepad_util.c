// gamepad_util.c — shared report-building helpers (see gamepad_util.h).
//
// Ported from OpenPuck/gamepad_util.cpp. PicoPuck omits the configurable
// back-paddle / QAM / A-B-swap remapping for now (chord.c already masks the
// switch chord out of g_in), so these use the base PlayStation layout.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/gamepad_util.h"

static inline void le16(uint8_t *p, int16_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}

uint8_t sw_stick(int16_t v, bool invert)
{
	int32_t a = 0x80 + (invert ? -((int32_t)v >> 8) : ((int32_t)v >> 8));
	if (a < 0)
		a = 0;
	if (a > 255)
		a = 255;
	return (uint8_t)a;
}

void ps_neutral_calib(uint8_t *buf)
{
	le16(buf + 6, 2844);  le16(buf + 8, -2844);   // gyro pitch +/-
	le16(buf + 10, 2844); le16(buf + 12, -2844);  // gyro yaw +/-
	le16(buf + 14, 2844); le16(buf + 16, -2844);  // gyro roll +/-
	le16(buf + 18, 2844); le16(buf + 20, 2844);   // gyro speed +/-
	le16(buf + 22, 8192); le16(buf + 24, -8192);  // accel X +/-
	le16(buf + 26, 8192); le16(buf + 28, -8192);  // accel Y +/-
	le16(buf + 30, 8192); le16(buf + 32, -8192);  // accel Z +/-
}

uint8_t ps_hat_nibble(uint32_t b)
{
	bool u = b & TB_DUP, d = b & TB_DDN, l = b & TB_DLF, r = b & TB_DRT;
	if (u && r) return 1;
	if (r && d) return 3;
	if (d && l) return 5;
	if (l && u) return 7;
	if (u) return 0;
	if (r) return 2;
	if (d) return 4;
	if (l) return 6;
	return 8;
}

uint8_t ps_face_nibble(uint32_t b)
{
	uint8_t f = 0;
	if (b & TB_A) f |= 0x20;  // cross
	if (b & TB_B) f |= 0x40;  // circle
	if (b & TB_X) f |= 0x10;  // square
	if (b & TB_Y) f |= 0x80;  // triangle
	return f;
}

uint8_t ps_shoulders_byte(uint32_t b, uint8_t lt, uint8_t rt)
{
	return ((b & TB_LB) ? 0x01 : 0) | ((b & TB_RB) ? 0x02 : 0) |
	       ((lt > SW_TRIG_ON || (b & TB_L2)) ? 0x04 : 0) |
	       ((rt > SW_TRIG_ON || (b & TB_R2)) ? 0x08 : 0) |
	       ((b & TB_MENU) ? 0x10 : 0) | ((b & TB_VIEW) ? 0x20 : 0) |
	       ((b & TB_L3) ? 0x40 : 0) | ((b & TB_R3) ? 0x80 : 0);
}

static uint16_t pad_norm_u16(int16_t v, uint16_t maxv)
{
	int32_t t = (int32_t)v + 32768;
	if (t < 0) t = 0;
	if (t > 65535) t = 65535;
	return (uint16_t)((t * (int32_t)maxv) / 65535);
}
static uint16_t touch_half_x(int16_t v, bool right_half)
{
	uint16_t x = pad_norm_u16(v, TOUCH_PAD_W / 2 - 1);
	return right_half ? (uint16_t)(TOUCH_PAD_W / 2 + x) : x;
}
static uint16_t touch_y_inv(int16_t v, uint16_t height)
{
	uint16_t maxy = height - 1;
	return (uint16_t)(maxy - pad_norm_u16(v, maxy));
}
static void touch_pack_point(uint8_t *base, int finger, bool touch, uint16_t x,
			     uint16_t y)
{
	uint8_t *f = base + finger * 4;
	if (!touch) {
		f[0] = 0x80; f[1] = 0; f[2] = 0; f[3] = 0;
		return;
	}
	f[0] = (uint8_t)(finger & 0x7F);
	f[1] = (uint8_t)(x & 0xFF);
	f[2] = (uint8_t)(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
	f[3] = (uint8_t)((y >> 4) & 0xFF);
}
void touch_pack_pads(uint8_t *pts, bool l_touch, bool r_touch, uint16_t lx,
		     uint16_t ly, uint16_t rx, uint16_t ry)
{
	touch_pack_point(pts, 0, false, 0, 0);
	touch_pack_point(pts, 1, false, 0, 0);
	if (l_touch && r_touch) {
		touch_pack_point(pts, 0, true, lx, ly);
		touch_pack_point(pts, 1, true, rx, ry);
	} else if (l_touch) {
		touch_pack_point(pts, 0, true, lx, ly);
	} else if (r_touch) {
		touch_pack_point(pts, 0, true, rx, ry);
	}
}
void steam_pads_to_touch(uint32_t b, uint16_t touch_h, int16_t lpx, int16_t lpy,
			 int16_t rpx, int16_t rpy, uint16_t *lx, uint16_t *ly,
			 uint16_t *rx, uint16_t *ry)
{
	bool lt = (b & TB_LPADT) || (b & TB_LPADC);
	bool rt = (b & TB_RPADT) || (b & TB_RPADC);
	*lx = touch_half_x(lpx, false);
	*ly = touch_y_inv(lpy, touch_h);
	*rx = touch_half_x(rpx, true);
	*ry = touch_y_inv(rpy, touch_h);
	if (lt && !(b & TB_LPADT)) { *lx = TOUCH_PAD_W / 4; *ly = touch_h / 2; }
	if (rt && !(b & TB_RPADT)) { *rx = TOUCH_PAD_W / 4 * 3; *ry = touch_h / 2; }
}
