// hd_rumble.c — see hd_rumble.h. Curve constants are protocol facts (any correct
// decoder lands on the same numbers); cross-checked against SDL's hidapi_switch
// rumble encoder and community RE notes.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hd_rumble.h"
#include <math.h>

enum { HDR_AMP_MIN = -256, HDR_AMP_OFF = -256 }; // -8.0 log2 units == silent

// Absolute 7-bit amplitude code → 1/32 log2 units (piecewise linear, slopes
// 1/4, 1/16, 1/32; code 0 is silence).
static int16_t hdr_amp7(uint8_t code)
{
	if (code == 0)
		return HDR_AMP_MIN;
	if (code < 16)
		return (int16_t)(8 * (int)code - 248);
	if (code < 32)
		return (int16_t)(2 * (int)code - 158);
	return (int16_t)((int)code - 127);
}

// Compact 5-bit command applied to the running amplitude: 0 silence; 1..11 an
// absolute preset 0..-5.0; 17..22 step up; 26..31 step down; else unchanged.
static int16_t hdr_amp5(uint8_t code, int16_t cur)
{
	if (code == 0)
		return HDR_AMP_OFF;
	if (code <= 11)
		return (int16_t)(-16 * (int)(code - 1));
	int step = 0;
	if (code >= 17 && code <= 19)
		step = 4;
	else if (code >= 20 && code <= 22)
		step = 1;
	else if (code >= 26 && code <= 28)
		step = -1;
	else if (code >= 29 && code <= 31)
		step = -4;
	int v = (int)cur + step;
	return v < HDR_AMP_MIN ? HDR_AMP_MIN : (v > 0 ? 0 : (int16_t)v);
}

// exp2(units/32) scaled to a 16-bit level, built once on first decode.
static uint16_t s_level[257];
static int s_built;
static void build_levels(void)
{
	if (s_built)
		return;
	for (int u = HDR_AMP_MIN; u <= 0; u++) {
		float lin = (float)u / 32.0f;
		float amp = (lin >= -7.9375f) ? exp2f(lin) : 0.0f;
		if (amp > 1.0f)
			amp = 1.0f;
		uint32_t v = (uint32_t)(amp * 65535.0f + 0.5f);
		s_level[u - HDR_AMP_MIN] = (v > 0xFFFF) ? 0xFFFF : (uint16_t)v;
	}
	s_built = 1;
}

static uint8_t hdr_field(uint32_t w, uint8_t shift, uint8_t width)
{
	return (uint8_t)((w >> shift) & ((1u << width) - 1u));
}

void hdr_reset(hdr_band_t *band)
{
	band->lo = HDR_AMP_OFF;
	band->hi = HDR_AMP_OFF;
}

uint16_t hdr_decode(hdr_band_t *band, const uint8_t b4[4])
{
	build_levels();
	int16_t *lo = &band->lo, *hi = &band->hi;
	uint32_t w = (uint32_t)b4[0] | ((uint32_t)b4[1] << 8) |
		     ((uint32_t)b4[2] << 16) | ((uint32_t)b4[3] << 24);
	uint16_t peak = 0;
#define HDR_SAMPLE()                                      \
	do {                                              \
		uint16_t la = s_level[*lo - HDR_AMP_MIN]; \
		uint16_t ha = s_level[*hi - HDR_AMP_MIN]; \
		uint16_t lv = la > ha ? la : ha;          \
		if (lv > peak)                            \
			peak = lv;                        \
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
			int want_hi = (w & 1) != 0;
			int is_freq = ((w >> 2) & 1) != 0;
			if (!is_freq) {
				if (want_hi)
					*hi = hdr_amp7(hdr_field(w, 23, 7));
				else
					*lo = hdr_amp7(hdr_field(w, 23, 7));
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
