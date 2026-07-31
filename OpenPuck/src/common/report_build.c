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
