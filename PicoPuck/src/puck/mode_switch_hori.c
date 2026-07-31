// mode_switch_hori.c — Switch HORIPAD emulated controller (ported from OpenPuck).
//
// 8-byte report, no report id: [btn_lo][btn_hi][hat][LX][LY][RX][RY][vendor].
// Nintendo face layout (A↔B / X↔Y swapped). No rumble/features.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "gamepad_util.h"
#include "hid_reports.h"

static uint16_t hori_build(int slot, uint8_t *out, uint8_t *rid)
{
	*rid = 0;
	uint32_t b = g_in[slot].buttons;
	uint16_t btn = 0;
	// Nintendo layout (abSwap): A→B(0x02) B→A(0x04) X→Y(0x01) Y→X(0x08).
	if (b & TB_A)
		btn |= 0x02;
	if (b & TB_B)
		btn |= 0x04;
	if (b & TB_X)
		btn |= 0x01;
	if (b & TB_Y)
		btn |= 0x08;
	if (b & TB_LB)
		btn |= 0x10;
	if (b & TB_RB)
		btn |= 0x20;
	if (g_in[slot].lt >= SW_TRIG_ON || (b & TB_L2))
		btn |= 0x40; // ZL
	if (g_in[slot].rt >= SW_TRIG_ON || (b & TB_R2))
		btn |= 0x80; // ZR
	if (b & TB_MENU)
		btn |= 0x100; // Minus
	if (b & TB_VIEW)
		btn |= 0x200; // Plus
	if (b & TB_L3)
		btn |= 0x400;
	if (b & TB_R3)
		btn |= 0x800;
	if (b & TB_STEAM)
		btn |= 0x1000; // Home
	if (b & TB_QAM)
		btn |= 0x2000; // Capture

	bool u = b & TB_DUP, d = b & TB_DDN, l = b & TB_DLF, r = b & TB_DRT;
	uint8_t hat = 8;
	if (u && r)
		hat = 1;
	else if (r && d)
		hat = 3;
	else if (d && l)
		hat = 5;
	else if (l && u)
		hat = 7;
	else if (u)
		hat = 0;
	else if (r)
		hat = 2;
	else if (d)
		hat = 4;
	else if (l)
		hat = 6;

	out[0] = (uint8_t)(btn & 0xFF);
	out[1] = (uint8_t)(btn >> 8);
	out[2] = hat;
	out[3] = sw_stick(g_in[slot].lx, false);
	out[4] = sw_stick(g_in[slot].ly, true);
	out[5] = sw_stick(g_in[slot].rx, false);
	out[6] = sw_stick(g_in[slot].ry, true);
	out[7] = 0;
	return 8;
}

const emu_mode_t emu_switch_hori = {
	.vid = 0x0F0D,
	.pid = 0x0092,
	.bcd = 0x0210,
	.product = "POKKEN CONTROLLER",
	.report_desc = SWITCH_HID_DESC,
	.report_desc_len = sizeof(SWITCH_HID_DESC),
	.build = hori_build,
	.get_report = NULL,
	.set_report = NULL,
	.poll_ms = 4,
};
