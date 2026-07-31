// mode_ps5.c — Sony DualSense emulated controller (ported from OpenPuck
// mode_ps5.cpp). Report id 0x01 (63B) + gyro/accel + split trackpad; feature
// reports 0x03/0x05/0x09/0x20; rumble on output report 0x02.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "gamepad_util.h"
#include "puck/relay.h"
#include "sys/settings.h"
#include <string.h>
#include "hid_reports.h"

#define PS5_TOUCH_H 1080
#define PS5_STATUS_USB 0x1A
#define ET_DS5 3 // per-type config index (matches OpenPuck ET_DS5)

static uint16_t ps5_build(int slot, uint8_t *out, uint8_t *rid)
{
	*rid = 0x01;
	uint32_t b = g_in[slot].buttons;
	bool lt_touch = (b & TB_LPADT) || (b & TB_LPADC);
	bool rt_touch = (b & TB_RPADT) || (b & TB_RPADC);
	memset(out, 0, 63);
	out[0] = sw_stick(g_in[slot].lx, false);
	out[1] = sw_stick(g_in[slot].ly, true);
	out[2] = sw_stick(g_in[slot].rx, false);
	out[3] = sw_stick(g_in[slot].ry, true);
	out[4] = g_in[slot].lt;
	out[5] = g_in[slot].rt;
	static uint8_t seq;
	out[6] = seq++;
	out[7] = ps_hat_nibble(b) |
		 ps_face_nibble(b, settings()->type[ET_DS5].ab_swap);
	out[8] = ps_shoulders_byte(b, g_in[slot].lt, g_in[slot].rt);
	out[9] = ((b & TB_STEAM) ? 0x01 : 0) |
		 ((b & (TB_TOUCH | TB_LPADC | TB_RPADC)) ? 0x02 : 0) |
		 ((b & TB_MUTE) ? 0x04 : 0);
	out[15] = g_in[slot].gx & 0xFF;
	out[16] = g_in[slot].gx >> 8;
	out[17] = g_in[slot].gz & 0xFF;
	out[18] = g_in[slot].gz >> 8;
	out[19] = (-g_in[slot].gy) & 0xFF;
	out[20] = (-g_in[slot].gy) >> 8;
	out[21] = g_in[slot].ax & 0xFF;
	out[22] = g_in[slot].ax >> 8;
	out[23] = g_in[slot].ay & 0xFF;
	out[24] = g_in[slot].ay >> 8;
	out[25] = g_in[slot].az & 0xFF;
	out[26] = g_in[slot].az >> 8;
	uint16_t lx, ly, rx, ry;
	steam_pads_to_touch(b, PS5_TOUCH_H, g_in[slot].lpx, g_in[slot].lpy,
			    g_in[slot].rpx, g_in[slot].rpy, &lx, &ly, &rx, &ry);
	touch_pack_pads(out + 32, lt_touch, rt_touch, lx, ly, rx, ry);
	out[52] = PS5_STATUS_USB;
	return 63;
}

static uint16_t ps5_get(int slot, uint8_t rid, uint8_t type, uint8_t *buf,
			uint16_t reqlen)
{
	if (type != PP_HID_FEATURE || reqlen == 0)
		return 0;
	memset(buf, 0, reqlen);
	uint8_t mac[6] = {
		0x00, 0x1B, 0xDC, 0x4F, 0x55, (uint8_t)(0x60 + slot)
	};
	switch (rid) {
	case 0x03:
		if (reqlen < 47)
			return 0;
		buf[0] = 0x00;
		buf[1] = 0x28;
		buf[2] = 0x01;
		buf[3] = 0x00;
		buf[4] = 0x0E;
		return 47;
	case 0x05:
		if (reqlen < 40)
			return 0;
		ps_neutral_calib(buf);
		return 40;
	case 0x09:
		if (reqlen < 19)
			return 0;
		memcpy(buf, mac, 6);
		return 19;
	case 0x20:
		if (reqlen < 63)
			return 0;
		buf[23] = 0x01;
		buf[27] = 0x01;
		return 63;
	default:
		return 0;
	}
}

static void ps5_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b,
		    uint16_t n)
{
	if (type != PP_HID_OUTPUT)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid. Normalise to p = bytes AFTER the id.
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		if (n < 1 || b[0] != 0x02)
			return;
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else if (rid == 0x02) {
		p = b;
		pn = n;
	} else {
		return;
	}
	if (pn < 4)
		return;
	// DualSense output 0x02 body: [2]=right(high) motor, [3]=left(low) motor.
	puck_rumble(slot, (uint16_t)p[3] * 257u, (uint16_t)p[2] * 257u);
}

const emu_mode_t emu_ps5 = {
	.vid = 0x054C,
	.pid = 0x0CE6,
	.bcd = 0x0110,
	.product = "DualSense Wireless Controller",
	.report_desc = PS5_HID_DESC,
	.report_desc_len = sizeof(PS5_HID_DESC),
	.build = ps5_build,
	.get_report = ps5_get,
	.set_report = ps5_set,
	.poll_ms = 4,
};
