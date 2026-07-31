// mode_ps3.c — Sony Sixaxis / DualShock 3 (054C:0268) emulated controller,
// ported from OpenPuck mode_ps3.cpp. The PS3 recognises the pad by VID/PID plus
// the control-transfer feature handshake below; input then streams as report
// 0x01. Rumble arrives as output report 0x01.
//
// Unlike Adafruit's HID wrapper (which force-prepends the report id to a
// GET_REPORT response), raw TinyUSB sends our buffer verbatim — so the feature
// reports are copied on the wire exactly as GIMX emits them (byte 0 is the
// array's own first byte, NOT necessarily the report id), no buf[-1] trick.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "puck/relay.h"
#include "sys/settings.h"
#include <string.h>
#include "hid_reports.h"
#include "report_build.h"

// Genuine Sixaxis / DualShock 3 HID report descriptor (148 bytes, verbatim from
// real hardware). The console does not parse this to drive input — it matches by
// VID/PID + handshake — but a faithful descriptor keeps host HID stacks happy.
// Do not "tidy" it: this is the real firmware's (famously odd) descriptor.

// Magic GET_REPORT(Feature) responses, verbatim from GIMX-firmwares EMUPS3 — a
// LUFA DS3 emulator proven to enumerate on a real PS3. The enable handshake is:
//   GET 0x01 -> GET 0xF2 -> {SET 0xEF / GET 0xEF}x2 -> GET 0xF8 -> SET out 0x01
//   -> SET 0xF4(enable); then input report 0x01 streams (we stream regardless).

static uint8_t g_byte6ef =
	0xb0; // last SET 0xEF byte[6]; echoed in GET 0xEF/0xF8 byte[7]
static uint8_t g_masterBd[6]; // host MAC learned via SET 0xF5
static bool g_haveMaster;
// Controller BT MAC (OUI 00:1B:DC like the other PS modes; last byte 0x80 keeps
// it distinct). Only relevant to BT pairing identity; read via GET 0xF2.
static const uint8_t g_ds3Mac[6] = { 0x00, 0x1B, 0xDC, 0x4F, 0x55, 0x80 };

// Copy a 64-byte Sixaxis feature report verbatim (raw TinyUSB does not prepend
// the id). Returns min(64, reqlen).
static uint16_t emit_feature(uint8_t *buf, uint16_t reqlen,
			     const uint8_t arr[64])
{
	uint16_t n = reqlen < 64 ? reqlen : 64;
	memcpy(buf, arr, n);
	return n;
}

static uint16_t ds3_get(int slot, uint8_t rid, uint8_t type, uint8_t *buf,
			uint16_t reqlen)
{
	(void)slot;
	if (type != PP_HID_FEATURE || !buf || reqlen == 0)
		return 0;
	uint8_t r[64];
	switch (rid) {
	case 0x01:
		return emit_feature(buf, reqlen, report_01);
	case 0xf2: // overlay our controller MAC at [4..9]
		memcpy(r, report_f2, 64);
		memcpy(r + 4, g_ds3Mac, 6);
		return emit_feature(buf, reqlen, r);
	case 0xf5: // overlay learned host MAC at [2..7]
		memcpy(r, report_f5, 64);
		if (g_haveMaster)
			memcpy(r + 2, g_masterBd, 6);
		return emit_feature(buf, reqlen, r);
	case 0xef: // echo the PS3's remembered byte at [7]
		memcpy(r, report_ef, 64);
		r[7] = g_byte6ef;
		return emit_feature(buf, reqlen, r);
	case 0xf8:
		memcpy(r, report_f8, 64);
		r[7] = g_byte6ef;
		return emit_feature(buf, reqlen, r);
	case 0xf7:
		return emit_feature(buf, reqlen, report_f7);
	default:
		return 0;
	}
}

static void ds3_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b,
		    uint16_t n)
{
	if (type == PP_HID_FEATURE) {
		// 0xEF: remember byte[6] for the next GET echo. 0xF5: learn host MAC.
		// 0xF4 (set-operational) is ACKed silently — we stream unconditionally.
		if (rid == 0xef && n >= 7)
			g_byte6ef = b[6];
		else if (rid == 0xf5 && n >= 8) {
			memcpy(g_masterBd, b + 2, 6);
			g_haveMaster = true;
		}
		return;
	}
	if (type != PP_HID_OUTPUT)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid. Normalise to p = bytes AFTER the id.
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		if (n < 1 || b[0] != 0x01)
			return;
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else if (rid == 0x01) {
		p = b;
		pn = n;
	} else {
		return;
	}
	if (pn < 5)
		return;
	// Output report 0x01 body: p[0]=0x00, p[1]=small dur, p[2]=small power,
	// p[3]=large dur, p[4]=large power. Large→lowFreq, small→highFreq(on/off);
	// 257 = 65535/255 scales 8-bit power to the 16-bit haptic range.
	puck_rumble(slot, (uint16_t)p[4] * 257u, p[2] ? 0xFFFFu : 0u);
}

// Input-report layout is in the shared builder (common/report_build.c). PS3 has
// no dedicated per-type config; it reuses the DS4 DualShock config (back paddles
// / QAM / A-B swap).
#define ET_DS4 2
static uint16_t ds3_build(int slot, uint8_t *out, uint8_t *rid)
{
	*rid = 0x01;
	const pp_type_cfg_t *t = &settings()->type[ET_DS4];
	report_cfg_t cfg = { .ab_swap = t->ab_swap,
			     .back = { t->back[0], t->back[1], t->back[2],
				       t->back[3] },
			     .qam = t->qam };
	return build_ds3(&g_in[slot], &cfg, out);
}

const emu_mode_t emu_ps3 = {
	.vid = 0x054C,
	.pid = 0x0268,
	.bcd = 0x0100,
	.product = "PLAYSTATION(R)3 Controller",
	.report_desc = DS3_HID_DESC,
	.report_desc_len = sizeof(DS3_HID_DESC),
	.build = ds3_build,
	.get_report = ds3_get,
	.set_report = ds3_set,
	.poll_ms = 1,
};
