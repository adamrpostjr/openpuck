// report_build.c — see report_build.h.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "report_build.h"
#include <string.h>

#define DS4_TOUCH_H 942
#define DS4_STATUS_USB 0x1B
#define PS5_TOUCH_H 1080
#define PS5_STATUS_USB 0x1A

uint32_t ps_buttons_remap(uint32_t raw, const report_cfg_t *cfg)
{
	uint32_t b = raw;
	if (cfg->qam && (b & TB_QAM)) {
		b &= ~(uint32_t)TB_QAM;
		b |= triton_from_code(cfg->qam);
	}
	if ((b & CHORD_BACK4) == CHORD_BACK4)
		b &= ~(uint32_t)(TB_A | TB_B | TB_X | TB_Y);
	if (b & TB_L4)
		b |= triton_from_code(cfg->back[0]);
	if (b & TB_R4)
		b |= triton_from_code(cfg->back[1]);
	if (b & TB_L5)
		b |= triton_from_code(cfg->back[2]);
	if (b & TB_R5)
		b |= triton_from_code(cfg->back[3]);
	return b;
}

uint16_t build_ds4(const puck_input_t *in, const report_cfg_t *cfg,
		   report_seq_t *seq, uint8_t out[63])
{
	uint32_t b = ps_buttons_remap(in->buttons, cfg);
	bool l_touch = (b & TB_LPADT) || (b & TB_LPADC);
	bool r_touch = (b & TB_RPADT) || (b & TB_RPADC);
	memset(out, 0, 63);
	out[0] = sw_stick(in->lx, false);
	out[1] = sw_stick(in->ly, true);
	out[2] = sw_stick(in->rx, false);
	out[3] = sw_stick(in->ry, true);
	out[4] = ps_hat_nibble(b) | ps_face_nibble(b, cfg->ab_swap);
	out[5] = ps_shoulders_byte(b, in->lt, in->rt);
	out[6] = ((seq->ctr++ & 0x0F) << 4) |
		 ((b & (TB_TOUCH | TB_LPADC | TB_RPADC)) ? 0x02 : 0) |
		 ((b & TB_STEAM) ? 0x01 : 0);
	out[7] = in->lt;
	out[8] = in->rt;
	out[12] = in->gx & 0xFF;
	out[13] = in->gx >> 8;
	out[14] = in->gz & 0xFF;
	out[15] = in->gz >> 8;
	out[16] = (-in->gy) & 0xFF;
	out[17] = (-in->gy) >> 8;
	out[18] = in->ax & 0xFF;
	out[19] = in->ax >> 8;
	out[20] = in->ay & 0xFF;
	out[21] = in->ay >> 8;
	out[22] = in->az & 0xFF;
	out[23] = in->az >> 8;
	out[29] = DS4_STATUS_USB;
	if (l_touch || r_touch) {
		uint16_t lx, ly, rx, ry;
		steam_pads_to_touch(b, DS4_TOUCH_H, in->lpx, in->lpy, in->rpx,
				    in->rpy, &lx, &ly, &rx, &ry);
		out[32] = 1;
		out[33] = seq->tstamp++;
		touch_pack_pads(out + 34, l_touch, r_touch, lx, ly, rx, ry);
	} else {
		out[32] = 0;
		touch_pack_pads(out + 34, false, false, 0, 0, 0, 0);
	}
	return 63;
}

uint16_t build_ps5(const puck_input_t *in, const report_cfg_t *cfg,
		   report_seq_t *seq, uint8_t out[63])
{
	uint32_t b = ps_buttons_remap(in->buttons, cfg);
	bool l_touch = (b & TB_LPADT) || (b & TB_LPADC);
	bool r_touch = (b & TB_RPADT) || (b & TB_RPADC);
	memset(out, 0, 63);
	out[0] = sw_stick(in->lx, false);
	out[1] = sw_stick(in->ly, true);
	out[2] = sw_stick(in->rx, false);
	out[3] = sw_stick(in->ry, true);
	out[4] = in->lt;
	out[5] = in->rt;
	out[6] = seq->ctr++;
	out[7] = ps_hat_nibble(b) | ps_face_nibble(b, cfg->ab_swap);
	out[8] = ps_shoulders_byte(b, in->lt, in->rt);
	out[9] = ((b & TB_STEAM) ? 0x01 : 0) |
		 ((b & (TB_TOUCH | TB_LPADC | TB_RPADC)) ? 0x02 : 0) |
		 ((b & TB_MUTE) ? 0x04 : 0);
	out[15] = in->gx & 0xFF;
	out[16] = in->gx >> 8;
	out[17] = in->gz & 0xFF;
	out[18] = in->gz >> 8;
	out[19] = (-in->gy) & 0xFF;
	out[20] = (-in->gy) >> 8;
	out[21] = in->ax & 0xFF;
	out[22] = in->ax >> 8;
	out[23] = in->ay & 0xFF;
	out[24] = in->ay >> 8;
	out[25] = in->az & 0xFF;
	out[26] = in->az >> 8;
	uint16_t lx, ly, rx, ry;
	steam_pads_to_touch(b, PS5_TOUCH_H, in->lpx, in->lpy, in->rpx, in->rpy,
			    &lx, &ly, &rx, &ry);
	touch_pack_pads(out + 32, l_touch, r_touch, lx, ly, rx, ry);
	out[52] = PS5_STATUS_USB;
	return 63;
}

// SC2 IMU int16 (center 0) → DS3 10-bit unsigned (center 511), little-endian.
static void ds3_imu(uint8_t *out, int16_t v)
{
	int32_t e = 511 + ((int32_t)v >> 6);
	if (e < 0)
		e = 0;
	if (e > 1023)
		e = 1023;
	out[0] = (uint8_t)(e & 0xFF);
	out[1] = (uint8_t)((e >> 8) & 0xFF);
}

uint16_t build_ds3(const puck_input_t *in, const report_cfg_t *cfg,
		   uint8_t out[48])
{
	uint32_t b = ps_buttons_remap(in->buttons, cfg);
	bool l2 = (in->lt > SW_TRIG_ON) || (b & TB_L2);
	bool r2 = (in->rt > SW_TRIG_ON) || (b & TB_R2);
	memset(out, 0, 48);

	// out[1]: Select L3 R3 Start Up Right Down Left. SC2 View→Start, Menu→Select.
	out[1] = ((b & TB_MENU) ? 0x01 : 0) | ((b & TB_L3) ? 0x02 : 0) |
		 ((b & TB_R3) ? 0x04 : 0) | ((b & TB_VIEW) ? 0x08 : 0) |
		 ((b & TB_DUP) ? 0x10 : 0) | ((b & TB_DRT) ? 0x20 : 0) |
		 ((b & TB_DDN) ? 0x40 : 0) | ((b & TB_DLF) ? 0x80 : 0);

	// out[2]: L2 R2 L1 R1 Triangle Circle Cross Square (ab_swap swaps A/B + X/Y).
	uint8_t tri, cir, crs, sqr;
	if (cfg->ab_swap) {
		tri = (b & TB_A) ? 0x10 : 0;
		cir = (b & TB_B) ? 0x20 : 0;
		crs = (b & TB_X) ? 0x40 : 0;
		sqr = (b & TB_Y) ? 0x80 : 0;
	} else {
		tri = (b & TB_Y) ? 0x10 : 0;
		cir = (b & TB_B) ? 0x20 : 0;
		crs = (b & TB_A) ? 0x40 : 0;
		sqr = (b & TB_X) ? 0x80 : 0;
	}
	out[2] = (l2 ? 0x01 : 0) | (r2 ? 0x02 : 0) | ((b & TB_LB) ? 0x04 : 0) |
		 ((b & TB_RB) ? 0x08 : 0) | tri | cir | crs | sqr;

	out[3] = (b & TB_STEAM) ? 0x01 : 0; // PS button

	out[5] = sw_stick(in->lx, false);
	out[6] = sw_stick(in->ly, true);
	out[7] = sw_stick(in->rx, false);
	out[8] = sw_stick(in->ry, true);

	// out[13..24]: analog pressures Up Right Down Left L2 R2 L1 R1 Tri Cir Crs Sqr.
	out[13] = (b & TB_DUP) ? 0xFF : 0;
	out[14] = (b & TB_DRT) ? 0xFF : 0;
	out[15] = (b & TB_DDN) ? 0xFF : 0;
	out[16] = (b & TB_DLF) ? 0xFF : 0;
	out[17] = in->lt;
	out[18] = in->rt;
	out[19] = (b & TB_LB) ? 0xFF : 0;
	out[20] = (b & TB_RB) ? 0xFF : 0;
	out[21] = tri ? 0xFF : 0;
	out[22] = cir ? 0xFF : 0;
	out[23] = crs ? 0xFF : 0;
	out[24] = sqr ? 0xFF : 0;

	out[28] = 0x00;
	out[29] = 0x05; // battery level: full (cosmetic over USB)

	// out[40..47]: accel X, accel Z, accel Y, gyro Z — each 10-bit LE, center 511.
	ds3_imu(out + 40, in->ax);
	ds3_imu(out + 42, in->az);
	ds3_imu(out + 44, in->ay);
	ds3_imu(out + 46, in->gz);
	return 48;
}

// ---- Switch HORIPAD -------------------------------------------------------
// Back-paddle / QAM code → Switch button-field bit (dpad codes 12..15 go to the
// hat via switch_hat_dirs instead, so return 0 here).
static uint16_t code_to_switch(uint8_t c, uint16_t fA, uint16_t fB, uint16_t fX,
			       uint16_t fY)
{
	switch (c) {
	case 1:
		return fA;
	case 2:
		return fB;
	case 3:
		return fX;
	case 4:
		return fY;
	case 5:
		return 0x10;
	case 6:
		return 0x20;
	case 7:
		return 0x400;
	case 8:
		return 0x800;
	case 9:
		return 0x100;
	case 10:
		return 0x200;
	case 11:
		return 0x1000;
	case 18:
		return 0x2000; // Capture / Screenshot
	case 19:
		return 0x40; // ZL
	case 20:
		return 0x80; // ZR
	default:
		return 0;
	}
}
static void switch_hat_dirs(uint8_t c, bool *u, bool *d, bool *l, bool *r)
{
	if (c == 12)
		*u = true;
	else if (c == 13)
		*d = true;
	else if (c == 14)
		*l = true;
	else if (c == 15)
		*r = true;
}

uint16_t build_switch_hori(const puck_input_t *in, const report_cfg_t *cfg,
			   uint8_t out[8])
{
	uint32_t b = in->buttons;
	uint16_t btn = 0;
	bool qam = cfg->qam && (b & TB_QAM);
	// Mode-switch chord (all 4 back + a face): don't leak the face to the console.
	if ((b & CHORD_BACK4) == CHORD_BACK4)
		b &= ~(uint32_t)(TB_A | TB_X | TB_Y);
	uint16_t fY = cfg->ab_swap ? 0x08 : 0x01,
		 fB = cfg->ab_swap ? 0x04 : 0x02,
		 fA = cfg->ab_swap ? 0x02 : 0x04,
		 fX = cfg->ab_swap ? 0x01 : 0x08;
	if (b & TB_Y)
		btn |= fY;
	if (b & TB_B)
		btn |= fB;
	if (b & TB_A)
		btn |= fA;
	if (b & TB_X)
		btn |= fX;
	if (b & TB_LB)
		btn |= 0x10;
	if (b & TB_RB)
		btn |= 0x20;
	if ((in->lt >= SW_TRIG_ON) || (b & TB_L2))
		btn |= 0x40; // ZL
	if ((in->rt >= SW_TRIG_ON) || (b & TB_R2))
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
	if (b & TB_L4)
		btn |= code_to_switch(cfg->back[0], fA, fB, fX, fY);
	if (b & TB_R4)
		btn |= code_to_switch(cfg->back[1], fA, fB, fX, fY);
	if (b & TB_L5)
		btn |= code_to_switch(cfg->back[2], fA, fB, fX, fY);
	if (b & TB_R5)
		btn |= code_to_switch(cfg->back[3], fA, fB, fX, fY);
	if (qam)
		btn |= code_to_switch(cfg->qam, fA, fB, fX, fY);

	bool u = b & TB_DUP, d = b & TB_DDN, l = b & TB_DLF, r = b & TB_DRT;
	if (b & TB_L4)
		switch_hat_dirs(cfg->back[0], &u, &d, &l, &r);
	if (b & TB_R4)
		switch_hat_dirs(cfg->back[1], &u, &d, &l, &r);
	if (b & TB_L5)
		switch_hat_dirs(cfg->back[2], &u, &d, &l, &r);
	if (b & TB_R5)
		switch_hat_dirs(cfg->back[3], &u, &d, &l, &r);
	if (qam)
		switch_hat_dirs(cfg->qam, &u, &d, &l, &r);
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
	out[3] = sw_stick(in->lx, false);
	out[4] = sw_stick(in->ly, true);
	out[5] = sw_stick(in->rx, false);
	out[6] = sw_stick(in->ry, true);
	out[7] = 0;
	return 8;
}

// ---- Switch Pro Controller -------------------------------------------------
enum {
	JC_BTN_Y = 1u << 0,
	JC_BTN_X = 1u << 1,
	JC_BTN_B = 1u << 2,
	JC_BTN_A = 1u << 3,
	JC_BTN_R = 1u << 6,
	JC_BTN_ZR = 1u << 7,
	JC_BTN_MINUS = 1u << 8,
	JC_BTN_PLUS = 1u << 9,
	JC_BTN_RSTICK = 1u << 10,
	JC_BTN_LSTICK = 1u << 11,
	JC_BTN_HOME = 1u << 12,
	JC_BTN_CAPTURE = 1u << 13,
	JC_BTN_DOWN = 1u << 16,
	JC_BTN_UP = 1u << 17,
	JC_BTN_RIGHT = 1u << 18,
	JC_BTN_LEFT = 1u << 19,
	JC_BTN_L = 1u << 22,
	JC_BTN_ZL = 1u << 23,
};
static uint32_t code_to_jc(uint8_t c, uint32_t fA, uint32_t fB, uint32_t fX,
			   uint32_t fY)
{
	switch (c) {
	case 1:
		return fA;
	case 2:
		return fB;
	case 3:
		return fX;
	case 4:
		return fY;
	case 5:
		return JC_BTN_L;
	case 6:
		return JC_BTN_R;
	case 7:
		return JC_BTN_LSTICK;
	case 8:
		return JC_BTN_RSTICK;
	case 9:
		return JC_BTN_MINUS;
	case 10:
		return JC_BTN_PLUS;
	case 11:
		return JC_BTN_HOME;
	case 18:
		return JC_BTN_CAPTURE;
	case 19:
		return JC_BTN_ZL;
	case 20:
		return JC_BTN_ZR;
	case 12:
		return JC_BTN_UP;
	case 13:
		return JC_BTN_DOWN;
	case 14:
		return JC_BTN_LEFT;
	case 15:
		return JC_BTN_RIGHT;
	default:
		return 0;
	}
}
uint32_t switch_pro_buttons(const puck_input_t *in, const report_cfg_t *cfg)
{
	uint32_t b = in->buttons;
	bool qam = cfg->qam && (b & TB_QAM);
	if ((b & CHORD_BACK4) == CHORD_BACK4)
		b &= ~(uint32_t)(TB_A | TB_B | TB_X | TB_Y);
	uint32_t fA = cfg->ab_swap ? JC_BTN_B : JC_BTN_A;
	uint32_t fB = cfg->ab_swap ? JC_BTN_A : JC_BTN_B;
	uint32_t fX = cfg->ab_swap ? JC_BTN_Y : JC_BTN_X;
	uint32_t fY = cfg->ab_swap ? JC_BTN_X : JC_BTN_Y;
	uint32_t jc = 0;
	if (b & TB_Y)
		jc |= fY;
	if (b & TB_B)
		jc |= fB;
	if (b & TB_A)
		jc |= fA;
	if (b & TB_X)
		jc |= fX;
	if (b & TB_LB)
		jc |= JC_BTN_L;
	if (b & TB_RB)
		jc |= JC_BTN_R;
	if ((in->lt >= SW_TRIG_ON) || (b & TB_L2))
		jc |= JC_BTN_ZL;
	if ((in->rt >= SW_TRIG_ON) || (b & TB_R2))
		jc |= JC_BTN_ZR;
	if (b & TB_VIEW)
		jc |= JC_BTN_PLUS;
	if (b & TB_MENU)
		jc |= JC_BTN_MINUS;
	if (b & TB_L3)
		jc |= JC_BTN_LSTICK;
	if (b & TB_R3)
		jc |= JC_BTN_RSTICK;
	if (b & TB_STEAM)
		jc |= JC_BTN_HOME;
	if (b & TB_DDN)
		jc |= JC_BTN_DOWN;
	if (b & TB_DUP)
		jc |= JC_BTN_UP;
	if (b & TB_DRT)
		jc |= JC_BTN_RIGHT;
	if (b & TB_DLF)
		jc |= JC_BTN_LEFT;
	if (b & TB_L4)
		jc |= code_to_jc(cfg->back[0], fA, fB, fX, fY);
	if (b & TB_R4)
		jc |= code_to_jc(cfg->back[1], fA, fB, fX, fY);
	if (b & TB_L5)
		jc |= code_to_jc(cfg->back[2], fA, fB, fX, fY);
	if (b & TB_R5)
		jc |= code_to_jc(cfg->back[3], fA, fB, fX, fY);
	if (qam)
		jc |= code_to_jc(cfg->qam, fA, fB, fX, fY);
	return jc;
}

// SC2 accel is ±2g (16384/g); ÷4 → the genuine Pro ±8g (4096/g) the Switch cal
// expects. Gyro is scaled by gyro_scale10/10, clamped to int16.
#define SW_ACCEL_DIV 4
static int16_t sw_gscale(int16_t v, uint8_t g10)
{
	if (g10 == 10)
		return v;
	int32_t s = (int32_t)v * (int32_t)g10 / 10;
	if (s > 32767)
		s = 32767;
	else if (s < -32768)
		s = -32768;
	return (int16_t)s;
}
void switch_pro_imu(const puck_input_t *in, uint8_t gyro_scale10,
		    uint8_t out36[36])
{
	// accel X<-+ay, Y<--ax, Z<-+az (÷4); gyro roll<-+gy, pitch<--gx, yaw<-+gz.
	// Same signed permutation across both so the console's accel/gyro fusion keeps
	// a consistent handedness (see the OpenPuck handedness note).
	int16_t aX = (int16_t)(in->ay / SW_ACCEL_DIV);
	int16_t aY = (int16_t)((-(int16_t)in->ax) / SW_ACCEL_DIV);
	int16_t aZ = (int16_t)(in->az / SW_ACCEL_DIV);
	int16_t groll = sw_gscale((int16_t)in->gy, gyro_scale10);
	int16_t gpitch = sw_gscale((int16_t)(-(int16_t)in->gx), gyro_scale10);
	int16_t gyaw = sw_gscale((int16_t)in->gz, gyro_scale10);
	for (int k = 0; k < 3; k++) {
		int o = k * 12;
		out36[o + 0] = aX & 0xFF;
		out36[o + 1] = (aX >> 8) & 0xFF;
		out36[o + 2] = aY & 0xFF;
		out36[o + 3] = (aY >> 8) & 0xFF;
		out36[o + 4] = aZ & 0xFF;
		out36[o + 5] = (aZ >> 8) & 0xFF;
		out36[o + 6] = groll & 0xFF;
		out36[o + 7] = (groll >> 8) & 0xFF;
		out36[o + 8] = gpitch & 0xFF;
		out36[o + 9] = (gpitch >> 8) & 0xFF;
		out36[o + 10] = gyaw & 0xFF;
		out36[o + 11] = (gyaw >> 8) & 0xFF;
	}
}
