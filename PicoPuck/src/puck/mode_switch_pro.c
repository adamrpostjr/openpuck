// mode_switch_pro.c — Nintendo Switch Pro Controller (057E:2009) emulated
// controller, ported from OpenPuck mode_switch_pro.cpp. The console drives a
// subcommand handshake over the OUT pipe (report 0x80 USB init, report 0x01
// subcommands) and the controller replies on the INPUT pipe (0x81 / 0x21); only
// after subcommand 0x03 selects report mode 0x30 does input stream.
//
// PicoPuck presents ONE emulated HID interface (the active slot), so the
// per-slot handshake arrays of the OpenPuck original collapse to a single state
// machine here. Replies are queued from the SET_REPORT callback and drained one
// per input-build cycle (the emu framework has no separate task/ISR split).
// User-calibration SPI writes are mirrored in RAM only (no flash persistence);
// motion cal therefore re-seeds from the factory blocks after a reboot.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "puck/gamepad_util.h"
#include "puck/relay.h"
#include "puck/slots.h"
#include "sys/settings.h"
#include "config/modes.h"

#include <string.h>
#include <math.h>
#include "pico/time.h"

#define ET_SWITCH 1              // per-type config index (matches OpenPuck ET_SWITCH)
#define SW_PRO_REPORT_MS 15u     // 66 Hz
#define SW_PRO_REPORT_MS_120 8u  // ~120 Hz
#define SW_STREAM_MS 4u          // "full" (~250 Hz), matches emu_present cadence
#define SW_ACCEL_DIV 4           // SC2 ±2g (16384/g) → Pro ±8g (4096/g)
#define CHORD_BACK4 (TB_L4 | TB_R4 | TB_L5 | TB_R5)

static const uint8_t SWPRO_HID_DESC[] = {
	0x05, 0x01, 0x15, 0x00, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x30, 0x05, 0x01,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x0A, 0x55, 0x00, 0x65, 0x00, 0x81, 0x02, 0x05, 0x09, 0x19, 0x0B,
	0x29, 0x0E, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
	0x75, 0x01, 0x95, 0x02, 0x81, 0x03, 0x0B, 0x01, 0x00, 0x01, 0x00, 0xA1,
	0x00, 0x0B, 0x30, 0x00, 0x01, 0x00, 0x0B, 0x31, 0x00, 0x01, 0x00, 0x0B,
	0x32, 0x00, 0x01, 0x00, 0x0B, 0x35, 0x00, 0x01, 0x00, 0x15, 0x00, 0x27,
	0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0xC0, 0x0B,
	0x39, 0x00, 0x01, 0x00, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B,
	0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x05, 0x09, 0x19,
	0x0F, 0x29, 0x12, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81,
	0x02, 0x75, 0x08, 0x95, 0x34, 0x81, 0x03, 0x06, 0x00, 0xFF, 0x85, 0x21,
	0x09, 0x01, 0x75, 0x08, 0x95, 0x3F, 0x81, 0x03, 0x85, 0x81, 0x09, 0x02,
	0x75, 0x08, 0x95, 0x3F, 0x81, 0x03, 0x85, 0x01, 0x09, 0x03, 0x75, 0x08,
	0x95, 0x3F, 0x91, 0x83, 0x85, 0x10, 0x09, 0x04, 0x75, 0x08, 0x95, 0x3F,
	0x91, 0x83, 0x85, 0x80, 0x09, 0x05, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x83,
	0x85, 0x82, 0x09, 0x06, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x83, 0xC0
};

#define JC_BTN_Y (1u << 0)
#define JC_BTN_X (1u << 1)
#define JC_BTN_B (1u << 2)
#define JC_BTN_A (1u << 3)
#define JC_BTN_R (1u << 6)
#define JC_BTN_ZR (1u << 7)
#define JC_BTN_MINUS (1u << 8)
#define JC_BTN_PLUS (1u << 9)
#define JC_BTN_RSTICK (1u << 10)
#define JC_BTN_LSTICK (1u << 11)
#define JC_BTN_HOME (1u << 12)
#define JC_BTN_CAPTURE (1u << 13)
#define JC_BTN_DOWN (1u << 16)
#define JC_BTN_UP (1u << 17)
#define JC_BTN_RIGHT (1u << 18)
#define JC_BTN_LEFT (1u << 19)
#define JC_BTN_L (1u << 22)
#define JC_BTN_ZL (1u << 23)

static uint32_t code_to_jc(uint8_t c, uint32_t fA, uint32_t fB, uint32_t fX,
			   uint32_t fY)
{
	switch (c) {
	case 1: return fA;
	case 2: return fB;
	case 3: return fX;
	case 4: return fY;
	case 5: return JC_BTN_L;
	case 6: return JC_BTN_R;
	case 7: return JC_BTN_LSTICK;
	case 8: return JC_BTN_RSTICK;
	case 9: return JC_BTN_MINUS;
	case 10: return JC_BTN_PLUS;
	case 11: return JC_BTN_HOME;
	case 18: return JC_BTN_CAPTURE;
	case 19: return JC_BTN_ZL;
	case 20: return JC_BTN_ZR;
	case 12: return JC_BTN_UP;
	case 13: return JC_BTN_DOWN;
	case 14: return JC_BTN_LEFT;
	case 15: return JC_BTN_RIGHT;
	default: return 0;
	}
}

// Single controller MAC (one presented interface). OUI 7C:BB:8A like a genuine
// Pro Controller; the console reads it via subcommand 0x02 to identify the pad.
static const uint8_t g_jcMac[6] = { 0x7C, 0xBB, 0x8A, 0x00, 0x00, 0x10 };
static uint8_t g_jcTimer;
static uint8_t g_reportMode;  // 0 until subcommand 0x03 selects 0x30

// ---- HD-rumble amplitude decoder (see OpenPuck for the full protocol notes).
enum { HDR_AMP_MIN = -256, HDR_AMP_OFF = -256 };
static int16_t hdr_amp7(uint8_t code)
{
	if (code == 0) return HDR_AMP_MIN;
	if (code < 16) return (int16_t)(8 * (int)code - 248);
	if (code < 32) return (int16_t)(2 * (int)code - 158);
	return (int16_t)((int)code - 127);
}
static int16_t hdr_amp5(uint8_t code, int16_t cur)
{
	if (code == 0) return HDR_AMP_OFF;
	if (code <= 11) return (int16_t)(-16 * (int)(code - 1));
	int step = 0;
	if (code >= 17 && code <= 19) step = 4;
	else if (code >= 20 && code <= 22) step = 1;
	else if (code >= 26 && code <= 28) step = -1;
	else if (code >= 29 && code <= 31) step = -4;
	int v = (int)cur + step;
	return v < HDR_AMP_MIN ? HDR_AMP_MIN : (v > 0 ? 0 : (int16_t)v);
}
static uint16_t g_hdr_level[257];
static bool g_hdr_built;
static void hdr_build_levels(void)
{
	if (g_hdr_built) return;
	for (int u = HDR_AMP_MIN; u <= 0; u++) {
		float lin = (float)u / 32.0f;
		float amp = (lin >= -7.9375f) ? exp2f(lin) : 0.0f;
		if (amp > 1.0f) amp = 1.0f;
		uint32_t v = (uint32_t)(amp * 65535.0f + 0.5f);
		g_hdr_level[u - HDR_AMP_MIN] = (v > 0xFFFF) ? 0xFFFF : (uint16_t)v;
	}
	g_hdr_built = true;
}
static int16_t g_band_lo[2], g_band_hi[2];  // running amplitudes per motor (0=L,1=R)
static void hdr_reset(void)
{
	g_band_lo[0] = g_band_hi[0] = HDR_AMP_OFF;
	g_band_lo[1] = g_band_hi[1] = HDR_AMP_OFF;
}
static uint8_t hdr_field(uint32_t w, uint8_t shift, uint8_t width)
{
	return (uint8_t)((w >> shift) & ((1u << width) - 1u));
}
static uint16_t hdr_decode(uint8_t motor, const uint8_t b[4])
{
	int16_t *lo = &g_band_lo[motor], *hi = &g_band_hi[motor];
	uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
		     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	uint16_t peak = 0;
#define HDR_SAMPLE()                                          \
	do {                                                  \
		uint16_t la = g_hdr_level[*lo - HDR_AMP_MIN]; \
		uint16_t ha = g_hdr_level[*hi - HDR_AMP_MIN]; \
		uint16_t lv = la > ha ? la : ha;              \
		if (lv > peak) peak = lv;                     \
	} while (0)

	switch (hdr_field(w, 30, 2)) {
	case 0:
		HDR_SAMPLE();
		break;
	case 1:
		if ((w & 0xFFFFF) == 0) {
			*lo = hdr_amp5(hdr_field(w, 25, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 20, 5), *hi);
			HDR_SAMPLE();
		} else if ((w & 0x3) == 0) {
			*lo = hdr_amp7(hdr_field(w, 23, 7));
			*hi = hdr_amp7(hdr_field(w, 9, 7));
			HDR_SAMPLE();
		} else {
			bool want_hi = (w & 1) != 0;
			bool is_freq = ((w >> 2) & 1) != 0;
			if (!is_freq) {
				if (want_hi) *hi = hdr_amp7(hdr_field(w, 23, 7));
				else *lo = hdr_amp7(hdr_field(w, 23, 7));
			}
			HDR_SAMPLE();
			*lo = hdr_amp5(hdr_field(w, 18, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 13, 5), *hi);
			HDR_SAMPLE();
			*lo = hdr_amp5(hdr_field(w, 8, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 3, 5), *hi);
			HDR_SAMPLE();
		}
		break;
	case 2:
		if ((w & 0x3FF) == 0) {
			*lo = hdr_amp5(hdr_field(w, 25, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 20, 5), *hi);
			HDR_SAMPLE();
			*lo = hdr_amp5(hdr_field(w, 15, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 10, 5), *hi);
			HDR_SAMPLE();
		} else {
			if (w & 1) {
				*hi = hdr_amp7(hdr_field(w, 23, 7));
				*lo = hdr_amp5(hdr_field(w, 18, 5), *lo);
			} else {
				*lo = hdr_amp7(hdr_field(w, 23, 7));
				*hi = hdr_amp5(hdr_field(w, 18, 5), *hi);
			}
			HDR_SAMPLE();
			*lo = hdr_amp5(hdr_field(w, 13, 5), *lo);
			*hi = hdr_amp5(hdr_field(w, 8, 5), *hi);
			HDR_SAMPLE();
		}
		break;
	case 3:
		*lo = hdr_amp5(hdr_field(w, 25, 5), *lo);
		*hi = hdr_amp5(hdr_field(w, 20, 5), *hi);
		HDR_SAMPLE();
		*lo = hdr_amp5(hdr_field(w, 15, 5), *lo);
		*hi = hdr_amp5(hdr_field(w, 10, 5), *hi);
		HDR_SAMPLE();
		*lo = hdr_amp5(hdr_field(w, 5, 5), *lo);
		*hi = hdr_amp5(hdr_field(w, 0, 5), *hi);
		HDR_SAMPLE();
		break;
	}
#undef HDR_SAMPLE
	return peak;
}
static uint16_t g_jc_last_lo, g_jc_last_hi;
static void jc_rumble(int slot, const uint8_t *p, uint16_t pn)
{
	if (pn < 9) return;  // [timer][left x4][right x4]
	hdr_build_levels();
	uint16_t lo = hdr_decode(0, p + 1), hi = hdr_decode(1, p + 5);
	if (lo == g_jc_last_lo && hi == g_jc_last_hi)
		return;  // Switch streams rumble every frame; relay only on change
	g_jc_last_lo = lo;
	g_jc_last_hi = hi;
	puck_rumble(slot, lo, hi);
}

static int jc_stick12(int16_t v, bool inv)
{
	int a = 2048 + (inv ? -((int)v >> 4) : ((int)v >> 4));
	return a < 0 ? 0 : (a > 4095 ? 4095 : a);
}
static void jc_pack_stick(uint8_t s[3], int16_t x, int16_t y)
{
	int X = jc_stick12(x, false), Y = jc_stick12(y, false);
	s[0] = (uint8_t)(X & 0xFF);
	s[1] = (uint8_t)(((Y & 0x0F) << 4) | ((X >> 8) & 0x0F));
	s[2] = (uint8_t)((Y >> 4) & 0xFF);
}
static uint8_t jc_battery_nibble(int slot)
{
	uint8_t pct = g_battery[slot];
	uint8_t cap;
	if (pct >= 70) cap = 4;
	else if (pct >= 50) cap = 3;
	else if (pct >= 30) cap = 2;
	else if (pct >= 10) cap = 1;
	else cap = 0;
	return (uint8_t)(cap << 1);  // even nibble {0,2,4,6,8}; leaves bit4 (charging) clear
}

// Scale a gyro axis by sw_gyro10/10, clamped to int16.
static int16_t gscale(int16_t v)
{
	uint8_t g10 = settings()->sw_gyro10;
	if (g10 == 10) return v;
	int32_t s = (int32_t)v * (int32_t)g10 / 10;
	if (s > 32767) s = 32767;
	else if (s < -32768) s = -32768;
	return (int16_t)s;
}
static void jc_input_prefix(int slot, uint8_t *out)
{
	const pp_type_cfg_t *t = &settings()->type[ET_SWITCH];
	uint32_t b = g_in[slot].buttons;
	bool qam = t->qam && (b & TB_QAM);
	if ((b & CHORD_BACK4) == CHORD_BACK4)
		b &= ~(uint32_t)(TB_A | TB_B | TB_X | TB_Y);
	uint32_t fA = t->ab_swap ? JC_BTN_B : JC_BTN_A;
	uint32_t fB = t->ab_swap ? JC_BTN_A : JC_BTN_B;
	uint32_t fX = t->ab_swap ? JC_BTN_Y : JC_BTN_X;
	uint32_t fY = t->ab_swap ? JC_BTN_X : JC_BTN_Y;
	uint32_t jc = 0;
	if (b & TB_Y) jc |= fY;
	if (b & TB_B) jc |= fB;
	if (b & TB_A) jc |= fA;
	if (b & TB_X) jc |= fX;
	if (b & TB_LB) jc |= JC_BTN_L;
	if (b & TB_RB) jc |= JC_BTN_R;
	if ((g_in[slot].lt >= SW_TRIG_ON) || (b & TB_L2)) jc |= JC_BTN_ZL;
	if ((g_in[slot].rt >= SW_TRIG_ON) || (b & TB_R2)) jc |= JC_BTN_ZR;
	if (b & TB_VIEW) jc |= JC_BTN_PLUS;
	if (b & TB_MENU) jc |= JC_BTN_MINUS;
	if (b & TB_L3) jc |= JC_BTN_LSTICK;
	if (b & TB_R3) jc |= JC_BTN_RSTICK;
	if (b & TB_STEAM) jc |= JC_BTN_HOME;
	if (b & TB_DDN) jc |= JC_BTN_DOWN;
	if (b & TB_DUP) jc |= JC_BTN_UP;
	if (b & TB_DRT) jc |= JC_BTN_RIGHT;
	if (b & TB_DLF) jc |= JC_BTN_LEFT;
	if (b & TB_L4) jc |= code_to_jc(t->back[0], fA, fB, fX, fY);
	if (b & TB_R4) jc |= code_to_jc(t->back[1], fA, fB, fX, fY);
	if (b & TB_L5) jc |= code_to_jc(t->back[2], fA, fB, fX, fY);
	if (b & TB_R5) jc |= code_to_jc(t->back[3], fA, fB, fX, fY);
	if (qam) jc |= code_to_jc(t->qam, fA, fB, fX, fY);
	out[0] = g_jcTimer++;
	uint8_t chg = (g_battery_state[slot] == 1) ? 0x10 : 0x00;
	out[1] = (uint8_t)((jc_battery_nibble(slot) << 4) | chg);
	out[2] = (uint8_t)(jc);
	out[3] = (uint8_t)(jc >> 8);
	out[4] = (uint8_t)(jc >> 16);
	jc_pack_stick(out + 5, g_in[slot].lx, g_in[slot].ly);
	jc_pack_stick(out + 8, g_in[slot].rx, g_in[slot].ry);
	out[11] = 0x09;  // rumble_input_report echo (some Switch fw expects nonzero)
}

// ---- factory SPI dumps the host reads for calibration ----------------------
static const uint8_t SPI_IMU_CAL[24] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x3B, 0x34, 0x3B, 0x34, 0x3B, 0x34 };
static const uint8_t SPI_PARAMS1[24] = { 0x50, 0xFD, 0x00, 0x00, 0xC6, 0x0F,
					 0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3,
					 0xD4, 0x14, 0x54, 0x41, 0x15, 0x54,
					 0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63 };
static const uint8_t SPI_PARAMS2[18] = { 0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3,
					 0xD4, 0x14, 0x54, 0x41, 0x15, 0x54,
					 0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63 };
static const uint8_t SPI_COLOR[13] = { 0x32, 0x32, 0x32, 0xE6, 0xE6, 0xE6, 0x32,
				       0x32, 0x32, 0x32, 0x32, 0x32, 0xFF };
static uint8_t g_spi_stick_cal[18];
static bool g_stick_cal_built;
static void jc_pack12(uint8_t *o9, const uint16_t v[6])
{
	o9[0] = v[0] & 0xFF;
	o9[1] = ((v[1] & 0x0F) << 4) | ((v[0] >> 8) & 0x0F);
	o9[2] = (v[1] >> 4) & 0xFF;
	o9[3] = v[2] & 0xFF;
	o9[4] = ((v[3] & 0x0F) << 4) | ((v[2] >> 8) & 0x0F);
	o9[5] = (v[3] >> 4) & 0xFF;
	o9[6] = v[4] & 0xFF;
	o9[7] = ((v[5] & 0x0F) << 4) | ((v[4] >> 8) & 0x0F);
	o9[8] = (v[5] >> 4) & 0xFF;
}
static void jc_build_stick_cal(void)
{
	if (g_stick_cal_built) return;
	const uint16_t C = 2048, R = 1800;
	uint16_t L[6] = { R, R, C, C, R, R };
	uint16_t Rr[6] = { C, C, R, R, R, R };
	jc_pack12(&g_spi_stick_cal[0], L);
	jc_pack12(&g_spi_stick_cal[9], Rr);
	g_stick_cal_built = true;
}

// User-cal SPI mirror (0x8000-0x80FF), RAM only. 0xFF = blank → factory fallback.
static uint8_t g_user_cal[0x100];
static bool g_user_cal_init;
static void jc_spi_write(uint32_t addr, uint8_t len, const uint8_t *data, uint16_t avail)
{
	for (uint8_t i = 0; i < len && i < avail; i++) {
		uint32_t a = addr + i;
		if (a >= 0x8000 && a < 0x8100)
			g_user_cal[a - 0x8000] = data[i];
	}
}
static void spi_read(uint32_t addr, uint8_t len, uint8_t *dst)
{
	if (!g_user_cal_init) {
		memset(g_user_cal, 0xFF, sizeof(g_user_cal));
		g_user_cal_init = true;
	}
	for (uint8_t i = 0; i < len; i++) {
		uint32_t a = addr + i;
		uint8_t v = 0xFF;
		if (a >= 0x6020 && a < 0x6020 + 24) v = SPI_IMU_CAL[a - 0x6020];
		else if (a >= 0x603D && a < 0x603D + 18) v = g_spi_stick_cal[a - 0x603D];
		else if (a >= 0x6050 && a < 0x6050 + 13) v = SPI_COLOR[a - 0x6050];
		else if (a >= 0x6080 && a < 0x6080 + 24) v = SPI_PARAMS1[a - 0x6080];
		else if (a >= 0x6098 && a < 0x6098 + 18) v = SPI_PARAMS2[a - 0x6098];
		else if (a >= 0x8000 && a < 0x8100) v = g_user_cal[a - 0x8000];
		dst[i] = v;
	}
}

static const uint8_t BT_PAIR_2[31] = {
	0x02, 0xE5, 0xC8, 0xE4, 0x92, 0x05, 0xFF, 0xC9, 0x8A, 0x7D, 0xEA,
	0x15, 0xF6, 0x19, 0xBA, 0x82, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- reply FIFO (single interface) -----------------------------------------
#define JC_REPLEN 63
#define JCQ_N 8
typedef struct { uint8_t rid; uint8_t data[JC_REPLEN]; } jc_rep_t;
static jc_rep_t g_jcq[JCQ_N];
static uint8_t g_jcq_h, g_jcq_t;
static void jc_enq(uint8_t rid, const uint8_t *d, uint8_t len)
{
	uint8_t nt = (uint8_t)((g_jcq_t + 1) % JCQ_N);
	if (nt == g_jcq_h) return;  // full → drop; host re-requests on timeout
	if (len > JC_REPLEN) len = JC_REPLEN;
	g_jcq[g_jcq_t].rid = rid;
	memset(g_jcq[g_jcq_t].data, 0, JC_REPLEN);
	memcpy(g_jcq[g_jcq_t].data, d, len);
	g_jcq_t = nt;
}

// Build the 0x21 reply (input prefix + ACK + echoed subcommand + reply data).
static void jc_subcmd(int slot, uint8_t sub, const uint8_t *args, uint16_t alen)
{
	uint8_t p[JC_REPLEN];
	memset(p, 0, sizeof(p));
	jc_input_prefix(slot, p);
	p[13] = sub;
	switch (sub) {
	case 0x01: {  // manual BT pairing: 3-stage key exchange
		uint8_t ty = (alen >= 1) ? args[0] : 3;
		p[12] = 0x81;
		if (ty == 1) {
			p[14] = 0x01;
			memcpy(&p[15], g_jcMac, 6);
			p[21] = 0x00; p[22] = 0x25; p[23] = 0x08;
			static const uint8_t NAME[14] = {
				0x50, 0x72, 0x6F, 0x20, 0x43, 0x6F, 0x6E,
				0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72 };
			memcpy(&p[24], NAME, 14);
			p[43] = 0x68;
		} else if (ty == 2) {
			memcpy(&p[14], BT_PAIR_2, 31);
		} else {
			p[14] = 0x03;
		}
		break;
	}
	case 0x02:  // device info
		p[12] = 0x82;
		p[14] = 0x03;
		p[15] = 0x48;
		p[16] = 0x03;  // Pro Controller
		p[17] = 0x02;
		memcpy(&p[18], g_jcMac, 6);
		p[24] = 0x01;
		p[25] = 0x01;
		break;
	case 0x10: {  // SPI flash read → echo [addr][len] then data
		if (alen < 5) { p[12] = 0x80; break; }
		uint32_t a = (uint32_t)args[0] | ((uint32_t)args[1] << 8) |
			     ((uint32_t)args[2] << 16) | ((uint32_t)args[3] << 24);
		uint8_t rl = args[4];
		if (rl > 0x1D) rl = 0x1D;
		p[12] = 0x90;
		p[14] = args[0]; p[15] = args[1]; p[16] = args[2]; p[17] = args[3];
		p[18] = rl;
		spi_read(a, rl, &p[19]);
		break;
	}
	case 0x11:  // SPI flash write → mirror user cal
		if (alen >= 5) {
			uint32_t a = (uint32_t)args[0] | ((uint32_t)args[1] << 8) |
				     ((uint32_t)args[2] << 16) | ((uint32_t)args[3] << 24);
			jc_spi_write(a, args[4], &args[5],
				     (alen > 5) ? (uint16_t)(alen - 5) : 0);
		}
		p[12] = 0x80;
		break;
	case 0x03:  // set input report mode
		if (alen >= 1 && args[0] == 0x30)
			g_reportMode = 0x30;
		p[12] = 0x80;
		break;
	case 0x04:  // trigger elapsed time
		p[12] = 0x83;
		p[15] = 0xCC; p[17] = 0xEE; p[19] = 0xFF;
		break;
	case 0x21:  // NFC/IR config
		p[12] = 0xA0;
		break;
	default:
		p[12] = 0x80;  // generic ACK
		break;
	}
	jc_enq(0x21, p, JC_REPLEN);
}

// ---- emu interface ---------------------------------------------------------
static void swpro_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b, uint16_t n)
{
	if (type != PP_HID_OUTPUT || n < 1)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid and hands us the body.
	uint8_t id;
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) { id = b[0]; p = b + 1; pn = (uint16_t)(n - 1); }
	else { id = rid; p = b; pn = n; }
	if (id == 0x80) {  // USB handshake
		if (pn < 1) return;
		if (p[0] == 0x01) {
			uint8_t d[9] = { 0x01, 0x00, 0x03, g_jcMac[0], g_jcMac[1],
					 g_jcMac[2], g_jcMac[3], g_jcMac[4], g_jcMac[5] };
			jc_enq(0x81, d, 9);
		} else if (p[0] == 0x02) {
			uint8_t d[1] = { 0x02 };
			jc_enq(0x81, d, 1);
		} else if (p[0] == 0x03) {
			uint8_t d[1] = { 0x03 };
			jc_enq(0x81, d, 1);
		}
		return;
	}
	if (id == 0x01) {  // [timer][rumble x8][subcmd][args...]
		if (pn < 10) return;
		jc_rumble(slot, p, pn);
		jc_subcmd(slot, p[9], p + 10, (pn > 10) ? (uint16_t)(pn - 10) : 0);
		return;
	}
	if (id == 0x10) {  // rumble-only
		jc_rumble(slot, p, pn);
		return;
	}
}

static void swpro_build_30(int slot, uint8_t out[63])
{
	memset(out, 0, 63);
	jc_input_prefix(slot, out);
	// accel X<-+ay, Y<--ax, Z<-+az (÷4 for ±8g cal); gyro roll<-+gy, pitch<--gx,
	// yaw<-+gz. Same signed permutation across both so the console's accel/gyro
	// fusion keeps a consistent handedness (see OpenPuck notes).
	int16_t aX = (int16_t)(g_in[slot].ay / SW_ACCEL_DIV);
	int16_t aY = (int16_t)((-(int16_t)g_in[slot].ax) / SW_ACCEL_DIV);
	int16_t aZ = (int16_t)(g_in[slot].az / SW_ACCEL_DIV);
	int16_t groll = gscale((int16_t)g_in[slot].gy);
	int16_t gpitch = gscale((int16_t)(-(int16_t)g_in[slot].gx));
	int16_t gyaw = gscale((int16_t)g_in[slot].gz);
	for (int k = 0; k < 3; k++) {
		int o = 12 + k * 12;
		out[o + 0] = aX & 0xFF;    out[o + 1] = (aX >> 8) & 0xFF;
		out[o + 2] = aY & 0xFF;    out[o + 3] = (aY >> 8) & 0xFF;
		out[o + 4] = aZ & 0xFF;    out[o + 5] = (aZ >> 8) & 0xFF;
		out[o + 6] = groll & 0xFF; out[o + 7] = (groll >> 8) & 0xFF;
		out[o + 8] = gpitch & 0xFF; out[o + 9] = (gpitch >> 8) & 0xFF;
		out[o + 10] = gyaw & 0xFF; out[o + 11] = (gyaw >> 8) & 0xFF;
	}
}

static uint16_t swpro_build(int slot, uint8_t *out, uint8_t *rid)
{
	jc_build_stick_cal();
	// Drain one queued handshake/subcommand reply per cycle (ordered), ahead of
	// any streamed input, so a bursty host-init isn't starved.
	if (g_jcq_h != g_jcq_t) {
		jc_rep_t *r = &g_jcq[g_jcq_h];
		*rid = r->rid;
		memcpy(out, r->data, JC_REPLEN);
		g_jcq_h = (uint8_t)((g_jcq_h + 1) % JCQ_N);
		return JC_REPLEN;
	}
	if (g_reportMode != 0x30)
		return 0;  // not until the host selects report mode 0x30
	// Rate-limit the 0x30 stream by the configured Switch cadence (the console
	// integrates 3 IMU samples/report at a fixed ~5 ms/sample).
	static uint32_t last_ms;
	uint8_t rate = settings()->sw_pro_rate;
	uint32_t interval = (rate == 2) ? SW_STREAM_MS :
			    (rate == 1) ? SW_PRO_REPORT_MS_120 : SW_PRO_REPORT_MS;
	uint32_t t = to_ms_since_boot(get_absolute_time());
	if (t - last_ms < interval)
		return 0;
	last_ms = t;
	*rid = 0x30;
	swpro_build_30(slot, out);
	return 63;
}

const emu_mode_t emu_switch_pro = {
	.vid = 0x057E, .pid = 0x2009, .bcd = 0x0220,
	.product = "Pro Controller",
	.report_desc = SWPRO_HID_DESC,
	.report_desc_len = sizeof(SWPRO_HID_DESC),
	.build = swpro_build,
	.get_report = NULL,
	.set_report = swpro_set,
	.poll_ms = 1,
};
