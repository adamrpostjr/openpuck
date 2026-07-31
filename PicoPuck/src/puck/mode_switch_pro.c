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
#include "gamepad_util.h"
#include "puck/relay.h"
#include "puck/slots.h"
#include "sys/settings.h"
#include "config/modes.h"

#include <string.h>
#include "pico/time.h"
#include "hid_reports.h"
#include "hd_rumble.h"
#include "report_build.h"

#define ET_SWITCH 1 // per-type config index (matches OpenPuck ET_SWITCH)
#define SW_PRO_REPORT_MS 15u // 66 Hz
#define SW_PRO_REPORT_MS_120 8u // ~120 Hz
#define SW_STREAM_MS 4u // "full" (~250 Hz), matches emu_present cadence
// CHORD_BACK4 comes from the shared triton_masks.h (via emu.h → triton.h).

// Per-slot handshake state — each presented Pro Controller runs its own init
// handshake and needs a distinct MAC (OUI 7C:BB:8A, last byte 0x10+slot) so the
// console treats them as separate pads.
static void jc_mac(int slot, uint8_t out[6])
{
	static const uint8_t base[5] = { 0x7C, 0xBB, 0x8A, 0x00, 0x00 };
	memcpy(out, base, 5);
	out[5] = (uint8_t)(0x10 + slot);
}
static uint8_t g_jcTimer[PP_NSLOT];
static uint8_t g_reportMode[PP_NSLOT]; // 0 until subcommand 0x03 selects 0x30

// HD-rumble decoding lives in the shared module (common/hd_rumble.c),
// byte-for-byte identical to OpenPuck. Per-slot, per-motor band state:
static hdr_band_t g_bands[PP_NSLOT][2];
static bool g_bands_init;

static uint16_t g_jc_last_lo[PP_NSLOT], g_jc_last_hi[PP_NSLOT];
static void jc_rumble(int slot, const uint8_t *p, uint16_t pn)
{
	if (pn < 9)
		return; // [timer][left x4][right x4]
	if (!g_bands_init) {
		for (int i = 0; i < PP_NSLOT; i++) {
			hdr_reset(&g_bands[i][0]);
			hdr_reset(&g_bands[i][1]);
		}
		g_bands_init = true;
	}
	uint16_t lo = hdr_decode(&g_bands[slot][0], p + 1),
		 hi = hdr_decode(&g_bands[slot][1], p + 5);
	if (lo == g_jc_last_lo[slot] && hi == g_jc_last_hi[slot])
		return; // Switch streams rumble every frame; relay only on change
	g_jc_last_lo[slot] = lo;
	g_jc_last_hi[slot] = hi;
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
	if (pct >= 70)
		cap = 4;
	else if (pct >= 50)
		cap = 3;
	else if (pct >= 30)
		cap = 2;
	else if (pct >= 10)
		cap = 1;
	else
		cap = 0;
	return (uint8_t)(cap
			 << 1); // even nibble {0,2,4,6,8}; leaves bit4 (charging) clear
}

static void jc_input_prefix(int slot, uint8_t *out)
{
	const pp_type_cfg_t *t = &settings()->type[ET_SWITCH];
	report_cfg_t cfg = { t->ab_swap,
			     { t->back[0], t->back[1], t->back[2], t->back[3] },
			     t->qam };
	uint32_t jc =
		switch_pro_buttons(&g_in[slot], &cfg); // shared button field
	out[0] = g_jcTimer[slot]++;
	uint8_t chg = (g_battery_state[slot] == 1) ? 0x10 : 0x00;
	out[1] = (uint8_t)((jc_battery_nibble(slot) << 4) | chg);
	out[2] = (uint8_t)(jc);
	out[3] = (uint8_t)(jc >> 8);
	out[4] = (uint8_t)(jc >> 16);
	jc_pack_stick(out + 5, g_in[slot].lx, g_in[slot].ly);
	jc_pack_stick(out + 8, g_in[slot].rx, g_in[slot].ry);
	out[11] =
		0x09; // rumble_input_report echo (some Switch fw expects nonzero)
}

// ---- factory SPI dumps the host reads for calibration ----------------------
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
	if (g_stick_cal_built)
		return;
	const uint16_t C = 2048, R = 1800;
	uint16_t L[6] = { R, R, C, C, R, R };
	uint16_t Rr[6] = { C, C, R, R, R, R };
	jc_pack12(&g_spi_stick_cal[0], L);
	jc_pack12(&g_spi_stick_cal[9], Rr);
	g_stick_cal_built = true;
}

// Per-slot user-cal SPI mirror (0x8000-0x80FF), RAM only. 0xFF = blank → factory
// fallback. Each Pro Controller has its own motion-cal region.
static uint8_t g_user_cal[PP_NSLOT][0x100];
static bool g_user_cal_init[PP_NSLOT];
static void jc_spi_write(int slot, uint32_t addr, uint8_t len,
			 const uint8_t *data, uint16_t avail)
{
	for (uint8_t i = 0; i < len && i < avail; i++) {
		uint32_t a = addr + i;
		if (a >= 0x8000 && a < 0x8100)
			g_user_cal[slot][a - 0x8000] = data[i];
	}
}
static void spi_read(int slot, uint32_t addr, uint8_t len, uint8_t *dst)
{
	if (!g_user_cal_init[slot]) {
		memset(g_user_cal[slot], 0xFF, sizeof(g_user_cal[slot]));
		g_user_cal_init[slot] = true;
	}
	for (uint8_t i = 0; i < len; i++) {
		uint32_t a = addr + i;
		uint8_t v = 0xFF;
		if (a >= 0x6020 && a < 0x6020 + 24)
			v = SPI_IMU_CAL[a - 0x6020];
		else if (a >= 0x603D && a < 0x603D + 18)
			v = g_spi_stick_cal[a - 0x603D];
		else if (a >= 0x6050 && a < 0x6050 + 13)
			v = SPI_COLOR[a - 0x6050];
		else if (a >= 0x6080 && a < 0x6080 + 24)
			v = SPI_PARAMS1[a - 0x6080];
		else if (a >= 0x6098 && a < 0x6098 + 18)
			v = SPI_PARAMS2[a - 0x6098];
		else if (a >= 0x8000 && a < 0x8100)
			v = g_user_cal[slot][a - 0x8000];
		dst[i] = v;
	}
}

// ---- per-slot reply FIFO ----------------------------------------------------
#define JC_REPLEN 63
#define JCQ_N 8
typedef struct {
	uint8_t rid;
	uint8_t data[JC_REPLEN];
} jc_rep_t;
static jc_rep_t g_jcq[PP_NSLOT][JCQ_N];
static uint8_t g_jcq_h[PP_NSLOT], g_jcq_t[PP_NSLOT];
static void jc_enq(int slot, uint8_t rid, const uint8_t *d, uint8_t len)
{
	uint8_t nt = (uint8_t)((g_jcq_t[slot] + 1) % JCQ_N);
	if (nt == g_jcq_h[slot])
		return; // full → drop; host re-requests on timeout
	if (len > JC_REPLEN)
		len = JC_REPLEN;
	g_jcq[slot][g_jcq_t[slot]].rid = rid;
	memset(g_jcq[slot][g_jcq_t[slot]].data, 0, JC_REPLEN);
	memcpy(g_jcq[slot][g_jcq_t[slot]].data, d, len);
	g_jcq_t[slot] = nt;
}

// Build the 0x21 reply (input prefix + ACK + echoed subcommand + reply data).
static void jc_subcmd(int slot, uint8_t sub, const uint8_t *args, uint16_t alen)
{
	uint8_t p[JC_REPLEN];
	uint8_t mac[6];
	jc_mac(slot, mac);
	memset(p, 0, sizeof(p));
	jc_input_prefix(slot, p);
	p[13] = sub;
	switch (sub) {
	case 0x01: { // manual BT pairing: 3-stage key exchange
		uint8_t ty = (alen >= 1) ? args[0] : 3;
		p[12] = 0x81;
		if (ty == 1) {
			p[14] = 0x01;
			memcpy(&p[15], mac, 6);
			p[21] = 0x00;
			p[22] = 0x25;
			p[23] = 0x08;
			static const uint8_t NAME[14] = {
				0x50, 0x72, 0x6F, 0x20, 0x43, 0x6F, 0x6E,
				0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72
			};
			memcpy(&p[24], NAME, 14);
			p[43] = 0x68;
		} else if (ty == 2) {
			memcpy(&p[14], BT_PAIR_2, 31);
		} else {
			p[14] = 0x03;
		}
		break;
	}
	case 0x02: // device info
		p[12] = 0x82;
		p[14] = 0x03;
		p[15] = 0x48;
		p[16] = 0x03; // Pro Controller
		p[17] = 0x02;
		memcpy(&p[18], mac, 6);
		p[24] = 0x01;
		p[25] = 0x01;
		break;
	case 0x10: { // SPI flash read → echo [addr][len] then data
		if (alen < 5) {
			p[12] = 0x80;
			break;
		}
		uint32_t a = (uint32_t)args[0] | ((uint32_t)args[1] << 8) |
			     ((uint32_t)args[2] << 16) |
			     ((uint32_t)args[3] << 24);
		uint8_t rl = args[4];
		if (rl > 0x1D)
			rl = 0x1D;
		p[12] = 0x90;
		p[14] = args[0];
		p[15] = args[1];
		p[16] = args[2];
		p[17] = args[3];
		p[18] = rl;
		spi_read(slot, a, rl, &p[19]);
		break;
	}
	case 0x11: // SPI flash write → mirror user cal
		if (alen >= 5) {
			uint32_t a = (uint32_t)args[0] |
				     ((uint32_t)args[1] << 8) |
				     ((uint32_t)args[2] << 16) |
				     ((uint32_t)args[3] << 24);
			jc_spi_write(slot, a, args[4], &args[5],
				     (alen > 5) ? (uint16_t)(alen - 5) : 0);
		}
		p[12] = 0x80;
		break;
	case 0x03: // set input report mode
		if (alen >= 1 && args[0] == 0x30)
			g_reportMode[slot] = 0x30;
		p[12] = 0x80;
		break;
	case 0x04: // trigger elapsed time
		p[12] = 0x83;
		p[15] = 0xCC;
		p[17] = 0xEE;
		p[19] = 0xFF;
		break;
	case 0x21: // NFC/IR config
		p[12] = 0xA0;
		break;
	default:
		p[12] = 0x80; // generic ACK
		break;
	}
	jc_enq(slot, 0x21, p, JC_REPLEN);
}

// ---- emu interface ---------------------------------------------------------
static void swpro_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b,
		      uint16_t n)
{
	if (type != PP_HID_OUTPUT || n < 1)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid and hands us the body.
	uint8_t id;
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		id = b[0];
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else {
		id = rid;
		p = b;
		pn = n;
	}
	if (id == 0x80) { // USB handshake
		if (pn < 1)
			return;
		if (p[0] == 0x01) {
			uint8_t mac[6];
			jc_mac(slot, mac);
			uint8_t d[9] = { 0x01,	 0x00,	 0x03,	 mac[0], mac[1],
					 mac[2], mac[3], mac[4], mac[5] };
			jc_enq(slot, 0x81, d, 9);
		} else if (p[0] == 0x02) {
			uint8_t d[1] = { 0x02 };
			jc_enq(slot, 0x81, d, 1);
		} else if (p[0] == 0x03) {
			uint8_t d[1] = { 0x03 };
			jc_enq(slot, 0x81, d, 1);
		}
		return;
	}
	if (id == 0x01) { // [timer][rumble x8][subcmd][args...]
		if (pn < 10)
			return;
		jc_rumble(slot, p, pn);
		jc_subcmd(slot, p[9], p + 10,
			  (pn > 10) ? (uint16_t)(pn - 10) : 0);
		return;
	}
	if (id == 0x10) { // rumble-only
		jc_rumble(slot, p, pn);
		return;
	}
}

static void swpro_build_30(int slot, uint8_t out[63])
{
	memset(out, 0, 63);
	jc_input_prefix(slot, out);
	switch_pro_imu(&g_in[slot], settings()->sw_gyro10, out + 12);
}

static uint16_t swpro_build(int slot, uint8_t *out, uint8_t *rid)
{
	jc_build_stick_cal();
	// Drain one queued handshake/subcommand reply per cycle (ordered), ahead of
	// any streamed input, so a bursty host-init isn't starved. Per-slot FIFO.
	if (g_jcq_h[slot] != g_jcq_t[slot]) {
		jc_rep_t *r = &g_jcq[slot][g_jcq_h[slot]];
		*rid = r->rid;
		memcpy(out, r->data, JC_REPLEN);
		g_jcq_h[slot] = (uint8_t)((g_jcq_h[slot] + 1) % JCQ_N);
		return JC_REPLEN;
	}
	if (g_reportMode[slot] != 0x30)
		return 0; // not until the host selects report mode 0x30
	// Rate-limit the 0x30 stream by the configured Switch cadence (the console
	// integrates 3 IMU samples/report at a fixed ~5 ms/sample). Per-slot timer.
	static uint32_t last_ms[PP_NSLOT];
	uint8_t rate = settings()->sw_pro_rate;
	uint32_t interval = (rate == 2) ? SW_STREAM_MS :
			    (rate == 1) ? SW_PRO_REPORT_MS_120 :
					  SW_PRO_REPORT_MS;
	uint32_t t = to_ms_since_boot(get_absolute_time());
	if (t - last_ms[slot] < interval)
		return 0;
	last_ms[slot] = t;
	*rid = 0x30;
	swpro_build_30(slot, out);
	return 63;
}

const emu_mode_t emu_switch_pro = {
	.vid = 0x057E,
	.pid = 0x2009,
	.bcd = 0x0220,
	.product = "Pro Controller",
	.report_desc = SWPRO_HID_DESC,
	.report_desc_len = sizeof(SWPRO_HID_DESC),
	.build = swpro_build,
	.get_report = NULL,
	.set_report = swpro_set,
	.poll_ms = 1,
};
