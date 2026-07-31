// mode_ds4.c — DualShock 4 / HID-gyro emulated controller (ported from OpenPuck
// mode_hidgyro.cpp). Report id 0x01 (63B) + gyro/accel + split trackpad; feature
// reports for motion calibration / MAC / fw; rumble on output report 0x05.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "gamepad_util.h"
#include "puck/relay.h"
#include "sys/settings.h"
#include <string.h>
#include "hid_reports.h"

#define DS4_TOUCH_H 942
#define DS4_STATUS_USB 0x1B
#define ET_DS4 2 // per-type config index (matches OpenPuck ET_DS4)

static uint16_t ds4_build(int slot, uint8_t *out, uint8_t *rid)
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
	out[4] = ps_hat_nibble(b) |
		 ps_face_nibble(b, settings()->type[ET_DS4].ab_swap);
	out[5] = ps_shoulders_byte(b, g_in[slot].lt, g_in[slot].rt);
	static uint8_t ctr;
	out[6] = ((ctr++ & 0x0F) << 4) |
		 ((b & (TB_TOUCH | TB_LPADC | TB_RPADC)) ? 0x02 : 0) |
		 ((b & TB_STEAM) ? 0x01 : 0);
	out[7] = g_in[slot].lt;
	out[8] = g_in[slot].rt;
	out[12] = g_in[slot].gx & 0xFF;
	out[13] = g_in[slot].gx >> 8;
	out[14] = g_in[slot].gz & 0xFF;
	out[15] = g_in[slot].gz >> 8;
	out[16] = (-g_in[slot].gy) & 0xFF;
	out[17] = (-g_in[slot].gy) >> 8;
	out[18] = g_in[slot].ax & 0xFF;
	out[19] = g_in[slot].ax >> 8;
	out[20] = g_in[slot].ay & 0xFF;
	out[21] = g_in[slot].ay >> 8;
	out[22] = g_in[slot].az & 0xFF;
	out[23] = g_in[slot].az >> 8;
	out[29] = DS4_STATUS_USB;
	if (lt_touch || rt_touch) {
		uint16_t lx, ly, rx, ry;
		steam_pads_to_touch(b, DS4_TOUCH_H, g_in[slot].lpx,
				    g_in[slot].lpy, g_in[slot].rpx,
				    g_in[slot].rpy, &lx, &ly, &rx, &ry);
		static uint8_t tstamp;
		out[32] = 1;
		out[33] = tstamp++;
		touch_pack_pads(out + 34, lt_touch, rt_touch, lx, ly, rx, ry);
	} else {
		out[32] = 0;
		touch_pack_pads(out + 34, false, false, 0, 0, 0, 0);
	}
	return 63;
}

static uint16_t ds4_get(int slot, uint8_t rid, uint8_t type, uint8_t *buf,
			uint16_t reqlen)
{
	if (type != PP_HID_FEATURE || reqlen == 0)
		return 0;
	memset(buf, 0, reqlen);
	uint8_t mac[6] = {
		0x00, 0x1B, 0xDC, 0x4F, 0x55, (uint8_t)(0x50 + slot)
	};
	switch (rid) {
	case 0x02:
		if (reqlen < 36)
			return 0;
		ps_neutral_calib(buf);
		return 36;
	case 0x12:
		if (reqlen < 15)
			return 0;
		memcpy(buf, mac, 6);
		return 15;
	case 0x81:
		if (reqlen < 6)
			return 0;
		memcpy(buf, mac, 6);
		return 6;
	case 0xA3:
		if (reqlen < 48)
			return 0;
		buf[0] = 0x01;
		return 48;
	default:
		return 0;
	}
}

static void ds4_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b,
		    uint16_t n)
{
	if (type != PP_HID_OUTPUT)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid. Normalise to p = bytes AFTER the id (effects @ [3]/[4]).
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		if (n < 1 || b[0] != 0x05)
			return;
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else if (rid == 0x05) {
		p = b;
		pn = n;
	} else {
		return;
	}
	if (pn < 5)
		return;
	puck_rumble(slot, (uint16_t)p[4] * 257u, (uint16_t)p[3] * 257u);
}

const emu_mode_t emu_ds4 = {
	.vid = 0x054C,
	.pid = 0x05C4,
	.bcd = 0x0120,
	.product = "Wireless Controller",
	.report_desc = GYRO_HID_DESC,
	.report_desc_len = sizeof(GYRO_HID_DESC),
	.build = ds4_build,
	.get_report = ds4_get,
	.set_report = ds4_set,
	.poll_ms = 4,
};
