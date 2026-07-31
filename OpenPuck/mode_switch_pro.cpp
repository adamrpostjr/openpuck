#include "mode_switch_pro.h"
#include "triton.h"
#include "gamepad_util.h"
#include "src/common/report_build.h"
#include "config.h"
#include "haptics.h"
#include "bonds.h"
#include "rf_link.h"
#include "usb_mount.h"
#include "usb_tx.h"
#include <Adafruit_TinyUSB.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "src/common/hid_reports.h"
#include "src/common/hd_rumble.h"
using namespace Adafruit_LittleFS_Namespace;

SwitchProController g_switchPro;

// Switch full-report (0x30) cadence. The console integrates the 3 IMU samples per report assuming ~5 ms/sample
// (3 samples / 15 ms genuine); too high a rate over-integrates the gyro. 120 Hz is drift-free and lower-latency
// (default); 66 Hz is the genuine compat fallback; "full" is the 4 ms PC rate (lowest latency).
#define SW_PRO_REPORT_MS 15u // 66 Hz
#define SW_PRO_REPORT_MS_120 8u // ~120 Hz
// SC2 accel is +/-2g (16384/g); /4 -> the genuine Pro +/-8g (4096/g) the Switch cal expects

uint8_t g_swProRate =
	2; // 0 = 66Hz, 1 = 120Hz, 2 = full (~250Hz / USB_STREAM_MS, default)
uint8_t g_swGyroScale10 = 10; // gyro sensitivity x10 (10 = 1.0x)

// Persist the two Switch Pro motion settings in their own tiny file so they never trigger a global-Cfg reset.
#define SWPRO_CFG_FILE "/swprocfg.bin"
void swProSaveCfg()
{
	uint8_t b[3] = { 0x01, g_swProRate,
			 g_swGyroScale10 }; // [ver][rate 0/1/2][gyroScale x10]
	InternalFS.remove(SWPRO_CFG_FILE);
	File f(InternalFS);
	if (f.open(SWPRO_CFG_FILE, FILE_O_WRITE)) {
		f.write(b, sizeof b);
		f.close();
	}
}
static void swProLoadCfg()
{
	File f(InternalFS);
	uint8_t b[3];
	if (f.open(SWPRO_CFG_FILE, FILE_O_READ)) {
		if (f.read(b, 3) == 3 && b[0] == 0x01) {
			// 0=66Hz, 1=120Hz, 2=full; bad value -> default full
			g_swProRate = (b[1] <= 2) ? b[1] : 2;
			g_swGyroScale10 =
				(b[2] >= 5 && b[2] <= 30) ?
					b[2] :
					10; // sane bounds (0.5x..3.0x)
		}
		f.close();
	}
}

// NSLOT Pro-Controller HIDs (one per bond slot) + per-slot handshake state (timer, report-mode gate,
// subcommand-reply FIFO), per-slot reply queue indices, per-slot last-stream millis, per-slot MAC, and
// per-slot user-cal SPI mirror.
static Adafruit_USBD_HID g_swPro[NSLOT];
static unsigned long g_swProLastMs[NSLOT] = { 0 };
// 0 until the host's subcommand 0x03 selects 0x30 -> THEN we stream input
static uint8_t g_swProReportMode[NSLOT] = { 0 };
static uint8_t g_jcTimer[NSLOT] = { 0 };
// Per-slot MAC. The console uses the BT addr it reads from subcommand 0x02 to identify the controller;
// real Pro Controllers each have their own. Last byte varies per slot.
static const uint8_t JC_MAC_BASE[5] = { 0x7C, 0xBB, 0x8A, 0x00, 0x00 };
static uint8_t g_jcMac[NSLOT][6];
static bool g_jcMacInit = false;
static void initJcMacs()
{
	if (g_jcMacInit)
		return;
	for (int s = 0; s < NSLOT; s++) {
		memcpy(g_jcMac[s], JC_MAC_BASE, 5);
		g_jcMac[s][5] = (uint8_t)(0x10 + s);
	}
	g_jcMacInit = true;
}
// Dynamic mount: the handshake/state arrays are keyed by USB slot, but the controller INPUT (and rumble target)
// belongs to the bond slot the USB slot is mapped to. This resolves usbSlot -> bondSlot (identity fallback if
// somehow unmapped, e.g. legacy paths).
static inline uint8_t jcBondOf(uint8_t usbSlot)
{
	int b = (usbSlot < NSLOT) ? g_usbToBond[usbSlot] : -1;
	return (b >= 0) ? (uint8_t)b : usbSlot;
}
// Switch HD-rumble amplitude decoding lives in the shared module
// (common/hd_rumble.c), byte-for-byte identical with PicoPuck. Per-slot,
// per-motor band state persists across frames; hdrReset() silences a slot.
static hdr_band_t g_hdrState[NSLOT][2];
static inline void hdrReset(uint8_t slot)
{
	hdr_reset(&g_hdrState[slot][0]);
	hdr_reset(&g_hdrState[slot][1]);
}
// Per-slot: each Pro Controller has its own rumble stream, so the "last" relay tracking must be per-slot.
static uint16_t g_jcLastLo[NSLOT] = { 0 };
static uint16_t g_jcLastHi[NSLOT] = { 0 };
static void jcRumble(uint8_t slot, const uint8_t *p, uint16_t pn)
{
	if (pn < 9)
		return; // [timer][left rumble x4][right rumble x4]
	uint16_t lo = hdr_decode(&g_hdrState[slot][0], p + 1),
		 hi = hdr_decode(&g_hdrState[slot][1], p + 5);
	// only relay on change: the Switch streams rumble every frame; re-sending
	// unchanged values would flood the RF relay and loop the motor
	if (lo == g_jcLastLo[slot] && hi == g_jcLastHi[slot])
		return;
	g_jcLastLo[slot] = lo;
	g_jcLastHi[slot] = hi;
	hapticSteamRumble(lo, hi,
			  jcBondOf(slot)); // route to the mapped controller
}
static int jcStick12(int16_t v, bool inv)
{ // steam int16 (center 0) -> 12-bit (center 0x800), clamped
	int a = 2048 + (inv ? -((int)v >> 4) : ((int)v >> 4));
	return a < 0 ? 0 : (a > 4095 ? 4095 : a);
}
// pack two 12-bit axes into 3 bytes (both normal; Switch Y polarity is opposite DS4)
static void jcPackStick(uint8_t s[3], int16_t x, int16_t y)
{
	int X = jcStick12(x, false), Y = jcStick12(y, false);
	s[0] = (uint8_t)(X & 0xFF);
	s[1] = (uint8_t)(((Y & 0x0F) << 4) | ((X >> 8) & 0x0F));
	s[2] = (uint8_t)((Y >> 4) & 0xFF);
}

// The Switch battery/connection byte (bat_con) is NOT a 0..8 value in the high nibble. Per hid-nintendo it is:
//   bits[7:5] = battery capacity (0=empty .. 4=full), bit4 = charging, bit0 = host_powered (USB-powered).
// A genuine pad therefore only ever emits the EVEN high-nibble levels 8/6/4/2/0 (full/medium/low/critical/empty)
// -- the odd bit (bit4) is reserved for the charging flag. The old code packed a 0..8 value straight into the
// high nibble, so any ODD level (1,3,5,7) set bit4 and the console showed the pad as "charging". Here we round
// the percentage onto the genuine 5-level scale and return the ready-to-place EVEN nibble {0,2,4,6,8}.
static uint8_t jcBatteryNibble(uint8_t bond)
{
	if (bond >= NSLOT)
		return 0;
	uint8_t pct = g_battery[bond];
	uint8_t cap; // genuine 0..4 capacity
	if (pct >= 70)
		cap = 4; // full
	else if (pct >= 50)
		cap = 3; // medium
	else if (pct >= 30)
		cap = 2; // low
	else if (pct >= 10)
		cap = 1; // critical
	else
		cap = 0; // empty
	return (uint8_t)(cap << 1); // -> 0,2,4,6,8 (even; leaves bit4 clear)
}
// Standard input-report prefix [0..11] (timer, battery/conn, 3 button bytes, both packed sticks, vibrator),
// shared by the streamed 0x30 report and the 0x21 subcommand-reply reports the host reads during init.
static void jcInputPrefix(uint8_t slot, uint8_t *out)
{
	uint8_t bond = jcBondOf(
		slot); // input data comes from the mapped controller; timer/state stay per USB slot
	// Button field is the shared builder; timer/battery/sticks stay OpenPuck-local.
	report_cfg_t cfg = { g_abSwap != 0,
			     { g_back[0], g_back[1], g_back[2], g_back[3] },
			     g_qamMap };
	uint32_t jc = switch_pro_buttons(&g_in[bond], &cfg);
	out[0] = g_jcTimer[slot]++;

	// bat_con byte: [7:5]=capacity, bit4=charging, bit0=host_powered (see jcBatteryNibble). The controllers are
	// wireless (battery powered) from the console's view, so host_powered=0 -- setting it pins the pad the console
	// treats as the wired/primary device in the charging state (the first-enumerated pad showing "charging, 100%").
	// The charging flag reflects the controller's REAL EChargeState (2=charging); wireless pads report discharging
	// (1) so it stays clear, but a pad genuinely on a charger will show the bolt correctly.
	uint8_t chg = (bond < NSLOT && g_batteryState[bond] == 2) ? 0x10 : 0x00;
	out[1] = (uint8_t)((jcBatteryNibble(bond) << 4) | chg);
	out[2] = (uint8_t)(jc);
	out[3] = (uint8_t)(jc >> 8);
	out[4] = (uint8_t)(jc >> 16);
	jcPackStick(out + 5, g_in[bond].lx, g_in[bond].ly);
	jcPackStick(out + 8, g_in[bond].rx, g_in[bond].ry);
	// rumble_input_report echo: genuine pad emits 0x09..0x0C; some Switch firmware expects this nonzero.
	out[11] = 0x09;
}
static void switchProBuild(uint8_t slot, uint8_t out[63])
{
	uint8_t bond =
		jcBondOf(slot); // IMU data comes from the mapped controller
	memset(out, 0, 63);
	jcInputPrefix(slot, out);
	// IMU (accel/gyro permutation + user gyro scale) is the shared builder.
	switch_pro_imu(&g_in[bond], g_swGyroScale10, out + 12);
}
// --- Canonical factory SPI dumps the host reads for calibration. Neutral IMU + centered sticks so a fresh
// "device" calibrates sane; user-cal regions (0x80xx) read 0xFF so the host falls back to these factory blocks.
// built at boot: left[9]+right[9] factory stick calibration (packed 12-bit). Shared across slots -- it's a
// factory stub the host reads to seed its cal, and we present the same neutral stub for all controllers.
static uint8_t g_spiStickCal[18];
static void jcPack12(uint8_t *o9, const uint16_t v[6])
{ // pack 6 12-bit values into 9 bytes (Switch stick-cal format)
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
static void jcBuildStickCal()
{
	const uint16_t C = 2048, R = 1800; // center, +/- range per axis
	uint16_t L[6] = {
		R, R, C, C, R, R
	}; // left  order: max(x,y), center(x,y), min(x,y)
	uint16_t Rr[6] = {
		C, C, R, R, R, R
	}; // right order: center(x,y), min(x,y), max(x,y)
	jcPack12(&g_spiStickCal[0], L);
	jcPack12(&g_spiStickCal[9], Rr);
}
// Manual-pairing (subcommand 0x01) reply payloads. A real Switch runs this 3-stage BT key exchange over USB so it
// can register the pad (Steam/hid-nintendo do not). The Switch only validates the shape, not the key contents. Each
// is the 31-byte reply body following the 0x21 input prefix + ack(0x81) + echoed-subcommand(0x01).
static const uint8_t BT_PAIR_3[31] = {
	0x03
}; // type 3: save pairing (all zero body)
// User-calibration SPI mirror (0x8000-0x80FF). A real Pro Controller stores the gyro/accel (and stick) calibration
// the Switch writes during "Calibrate Motion Controls" here, then reads it back and applies it -- that's how a
// resting IMU/neutral offset gets cancelled. Per-slot: each Pro Controller has its own SPI mirror, so each
// gets its own persisted file (NSLOT files; the same 0x100 bytes as before, partitioned by slot index).
#define SWCAL_FILE_BASE "/swimucal"
static uint8_t g_userCal[NSLOT][0x100];
static bool g_userCalLoaded[NSLOT] = { false, false, false, false };
static void swCalFileName(uint8_t slot, char *out)
{
	// "/swimucal0.bin" .. "/swimucal3.bin"
	const char *base = SWCAL_FILE_BASE;
	int i = 0;
	while (base[i]) {
		out[i] = base[i];
		i++;
	}
	out[i++] = (char)('0' + slot);
	out[i++] = '.';
	out[i++] = 'b';
	out[i++] = 'i';
	out[i++] = 'n';
	out[i] = '\0';
}
static void loadUserCal()
{
	for (int s = 0; s < NSLOT; s++) {
		memset(g_userCal[s], 0xFF, sizeof g_userCal[s]);
		char fn[16];
		swCalFileName((uint8_t)s, fn);
		File f(InternalFS);
		if (f.open(fn, FILE_O_READ)) {
			f.read(g_userCal[s], sizeof g_userCal[s]);
			f.close();
		}
		g_userCalLoaded[s] = true;
	}
}
static void saveUserCal(uint8_t slot)
{
	char fn[16];
	swCalFileName(slot, fn);
	InternalFS.remove(fn);
	File f(InternalFS);
	if (f.open(fn, FILE_O_WRITE)) {
		f.write(g_userCal[slot], sizeof g_userCal[slot]);
		f.close();
	}
}
// jcSpiWrite runs in the USB ISR (via jcSet). NEVER do flash I/O here -- a blocking LittleFS erase/write in the
// interrupt wedges USB + the RF poll and corrupts state (device drops into a bad state needing a replug). Only
// update the RAM mirror and flag it dirty; task() (main loop) does the actual save, debounced so a calibration
// write-burst coalesces into one flash write. Per-slot: each Pro Controller's ISR write targets its own mirror.
static volatile bool g_userCalDirty[NSLOT] = { false, false, false, false };
static volatile unsigned long g_userCalDirtyMs[NSLOT] = { 0 };
static void jcSpiWrite(uint8_t slot, uint32_t addr, uint8_t len,
		       const uint8_t *data, uint16_t avail)
{
	bool changed = false;
	for (uint8_t i = 0; i < len && i < avail; i++) {
		uint32_t a = addr + i;
		if (a >= 0x8000 && a < 0x8100) {
			if (g_userCal[slot][a - 0x8000] != data[i]) {
				g_userCal[slot][a - 0x8000] = data[i];
				changed = true;
			}
		}
	}
	if (changed) {
		g_userCalDirty[slot] = true;
		g_userCalDirtyMs[slot] = millis();
	} // defer the flash write to task()
}
static void spiRead(uint8_t slot, uint32_t addr, uint8_t len, uint8_t *dst)
{
	for (uint8_t i = 0; i < len; i++) {
		uint32_t a = addr + i;
		uint8_t v = 0xFF;
		if (a >= 0x6020 && a < 0x6020 + 24)
			v = SPI_IMU_CAL[a - 0x6020];
		else if (a >= 0x603D && a < 0x603D + 18)
			v = g_spiStickCal[a - 0x603D];
		else if (a >= 0x6050 && a < 0x6050 + 13)
			v = SPI_COLOR[a - 0x6050];
		else if (a >= 0x6080 && a < 0x6080 + 24)
			v = SPI_PARAMS1[a - 0x6080];
		else if (a >= 0x6098 && a < 0x6098 + 18)
			v = SPI_PARAMS2[a - 0x6098];
		else if (a >= 0x8000 && a < 0x8100)
			// persisted user cal (0xFF blank -> factory fallback)
			v = g_userCal[slot][a - 0x8000];
		dst[i] = v;
	}
}
// Reply FIFO: the host's handshake/subcommand reports arrive in the USB ISR (jcSet); we enqueue the canonical
// 0x81/0x21 reply and drain it from task() where sendReport is safe. SPSC ring, volatile indices. Per-slot: each
// Pro Controller runs its own handshake state machine, so each has its own reply queue. Every reply is sent as
// a full 63-byte report (64 with the id): macOS IOHIDManager silently DROPS any input report shorter than the
// descriptor-declared length (0x3F), so short 0x81/0x21 replies never reached the host.
#define JC_REPLEN 63
struct JcRep {
	uint8_t rid;
	uint8_t data[JC_REPLEN];
};
#define JCQ_N 8
static volatile uint8_t g_jcQh[NSLOT] = { 0, 0, 0, 0 };
static volatile uint8_t g_jcQt[NSLOT] = { 0, 0, 0, 0 };
static JcRep g_jcQ[NSLOT][JCQ_N];
static void jcEnq(uint8_t slot, uint8_t rid, const uint8_t *d, uint8_t len)
{
	uint8_t t = g_jcQt[slot];
	uint8_t nt = (uint8_t)((t + 1) % JCQ_N);
	if (nt == g_jcQh[slot])
		return; // full -> drop; the host re-requests on timeout
	if (len > JC_REPLEN)
		len = JC_REPLEN;
	g_jcQ[slot][t].rid = rid;
	memset(g_jcQ[slot][t].data, 0, JC_REPLEN);
	memcpy(g_jcQ[slot][t].data, d, len); // zero-pad to full report length
	g_jcQt[slot] = nt;
}
// Build the 0x21 reply (standard input prefix + ACK + echoed subcommand id + reply data) for a report-0x01 subcommand.
static void jcSubcmd(uint8_t slot, uint8_t sub, const uint8_t *args,
		     uint16_t alen)
{
	uint8_t p[JC_REPLEN];
	memset(p, 0, sizeof p);
	jcInputPrefix(slot, p);
	p[13] = sub;
	switch (sub) {
	// manual BT pairing: 3-stage key exchange a real Switch runs over USB
	case 0x01: {
		uint8_t t = (alen >= 1) ? args[0] : 3;
		p[12] = 0x81; // pairing ACK
		if (t == 1) {
			// echo type + this slot's MAC + "Pro Controller" name
			p[14] = 0x01;
			memcpy(&p[15], g_jcMac[slot], 6);
			p[21] = 0x00;
			p[22] = 0x25;
			p[23] = 0x08;
			// "Pro Controller" = 50 72 6F 20 43 6F 6E 74 72 6F 6C 6C 65 72
			static const uint8_t NAME[14] = {
				0x50, 0x72, 0x6F, 0x20, 0x43, 0x6F, 0x6E,
				0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72
			};
			memcpy(&p[24], NAME, 14);
			p[38] = 0x00;
			p[39] = 0x00;
			p[40] = 0x00;
			p[41] = 0x00;
			p[42] = 0x00;
			p[43] = 0x68;
		} else if (t == 2) {
			memcpy(&p[14], BT_PAIR_2, 31);
		} else {
			memcpy(&p[14], BT_PAIR_3, 31);
		}
		break;
	}
	case 0x02: // request device info
		p[12] = 0x82;
		p[14] = 0x03;

		// firmware version 3.72 (genuine Pro Controller value)
		p[15] = 0x48;
		p[16] = 0x03; // controller type: Pro Controller
		p[17] = 0x02; // fixed
		memcpy(&p[18], g_jcMac[slot], 6);
		p[24] = 0x01; // colors stored in SPI (0x6050)
		p[25] = 0x01; // fixed
		break;
	case 0x10: { // SPI flash read -> echo [addr][len] then the data
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
		spiRead(slot, a, rl, &p[19]);
		break;
	}
	// SPI flash write -> persist user calibration so Switch motion-cal sticks
	case 0x11:
		if (alen >= 5) {
			uint32_t a = (uint32_t)args[0] |
				     ((uint32_t)args[1] << 8) |
				     ((uint32_t)args[2] << 16) |
				     ((uint32_t)args[3] << 24);
			jcSpiWrite(slot, a, args[4], &args[5],
				   (alen > 5) ? (uint16_t)(alen - 5) : 0);
		}
		p[12] = 0x80; // write ACK
		break;

	// set input report mode (0x30 = standard full) -> begin streaming
	case 0x03:
		if (alen >= 1 && args[0] == 0x30)
			g_swProReportMode[slot] = 0x30;
		p[12] = 0x80;
		break;
	// trigger buttons elapsed time -> canned reply (genuine pad returns data)
	case 0x04:
		p[12] = 0x83;
		p[14] = 0x00;
		p[15] = 0xCC;
		p[16] = 0x00;
		p[17] = 0xEE;
		p[18] = 0x00;
		p[19] = 0xFF;
		break;
	case 0x21:
		p[12] = 0xA0;
		break; // set NFC/IR config
	default:
		p[12] = 0x80;

		// 0x06/0x08/0x30/0x38/0x40/0x41/0x48/... generic positive ACK
		break;
	}
	jcEnq(slot, 0x21, p, JC_REPLEN);
}
// USB-ISR callback for the Pro Controller's OUT endpoint. Per-slot dispatch via per-instance callback
// (jcSet##N closes over the slot index). Report 0x80 = USB handshake; 0x01 = subcommand.
static void jcSetCommon(uint8_t slot, uint8_t rid, hid_report_type_t type,
			uint8_t const *b, uint16_t n)
{
	if (type != HID_REPORT_TYPE_OUTPUT || n < 1)
		return;
	uint8_t id;
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		id = b[0];
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} // EP-OUT: id is the first payload byte
	else {
		id = rid;
		p = b;
		pn = n;
	} // control SET_REPORT: id already split out
	if (id == 0x80) {
		if (pn < 1)
			return;
		if (p[0] == 0x01) {
			uint8_t d[9] = { 0x01,
					 0x00,
					 0x03,
					 g_jcMac[slot][0],
					 g_jcMac[slot][1],
					 g_jcMac[slot][2],
					 g_jcMac[slot][3],
					 g_jcMac[slot][4],
					 g_jcMac[slot][5] };
			jcEnq(slot, 0x81, d, 9);
		} // device type + MAC
		else if (p[0] == 0x02) {
			uint8_t d[1] = { 0x02 };
			jcEnq(slot, 0x81, d, 1);
		} // handshake
		else if (p[0] == 0x03) {
			uint8_t d[1] = { 0x03 };
			jcEnq(slot, 0x81, d, 1);
		} // set baudrate
		// 0x04 force-USB / 0x05 enable-timeout / 0x06 reset: no reply expected
		return;
	}
	if (id == 0x01) { // [timer][rumble x8][subcmd][args...]
		if (pn < 10)
			return;
		jcRumble(slot, p, pn);
		jcSubcmd(slot, p[9], p + 10,
			 (pn > 10) ? (uint16_t)(pn - 10) : 0);
		return;
	}
	if (id == 0x10) {
		jcRumble(slot, p, pn);
		return;
	}
	// report 0x82: ignored
}
#define JCCB(N)                                                                \
	static void jcSet##N(uint8_t r, hid_report_type_t t, uint8_t const *b, \
			     uint16_t n)                                       \
	{                                                                      \
		jcSetCommon(N, r, t, b, n);                                    \
	}
// clang-format off
JCCB(0)
JCCB(1)
JCCB(2)
JCCB(3)
// clang-format on
typedef void (*jc_setcb_t)(uint8_t, hid_report_type_t, uint8_t const *,
			   uint16_t);
static jc_setcb_t const JC_SETCB[NSLOT] = { jcSet0, jcSet1, jcSet2, jcSet3 };

// Dynamic-mount mode: begin() is unused (setup() calls beginPool()+usbReenumerate instead).
void SwitchProController::begin()
{
}
// Wake mouse (1 HID) is present in Switch mode, leaving CFG_TUD_HID-1 for the Pro Controller pool.
uint8_t SwitchProController::maxSlots() const
{
	uint8_t cap = (uint8_t)(CFG_TUD_HID - 1);
	return cap < NSLOT ? cap : (uint8_t)NSLOT;
}
void SwitchProController::usbIdentity()
{
	USBDevice.setID(0x057E, 0x2009);
	USBDevice.setDeviceVersion(0x0220);
	USBDevice.setManufacturerDescriptor("Nintendo Co., Ltd.");
	USBDevice.setProductDescriptor("Pro Controller");
}
void SwitchProController::beginPool()
{
	jcBuildStickCal();
	for (uint8_t s = 0; s < NSLOT; s++)
		hdrReset(s);
	loadUserCal();
	swProLoadCfg();
	initJcMacs();
	uint8_t pool = maxSlots();
	for (uint8_t s = 0; s < pool; s++) {
		g_swPro[s].enableOutEndpoint(true);
		g_swPro[s].setReportCallback(NULL, JC_SETCB[s]);
		g_swPro[s].setReportDescriptor(SWPRO_HID_DESC,
					       sizeof SWPRO_HID_DESC);
		g_swPro[s].setPollInterval(1);
		g_swPro[s].begin();
	}
}
void SwitchProController::mountSlots(uint8_t k)
{
	for (uint8_t u = 0; u < k; u++) {
		// Each re-enumeration restarts the host handshake -- clear this USB slot's gate + reply FIFO so we
		// don't stream 0x30 before the (new) host has re-selected report mode.
		g_swProReportMode[u] = 0;
		g_jcQh[u] = g_jcQt[u] = 0;
		// reset the rumble decoder + relay dedup so a stale amplitude from a
		// prior session can't carry across the reconnect
		hdrReset(u);
		g_jcLastLo[u] = g_jcLastHi[u] = 0;
		USBDevice.addInterface(g_swPro[u]);
	}
}
void SwitchProController::task()
{
	for (uint8_t s = 0; s < g_usbMountCount; s++) {
		if (!g_swPro[s].ready())
			continue;
		// Deferred user-cal flash write (queued by the USB ISR; debounced so a calibration write-burst
		// is one save). Per-slot: each Pro Controller saves its own mirror.
		if (g_userCalDirty[s] &&
		    (unsigned long)(millis() - g_userCalDirtyMs[s]) > 250u) {
			g_userCalDirty[s] = false;
			saveUserCal((uint8_t)s);
		}
		// drain handshake/subcommand replies first (ordered) -- one report per slot per call so a
		// bursty host-init doesn't starve the streamed 0x30
		if (g_jcQh[s] != g_jcQt[s]) {
			JcRep *r = &g_jcQ[s][g_jcQh[s]];
			usbTxHid(&g_swPro[s], r->rid, r->data, JC_REPLEN);
			g_jcQh[s] = (uint8_t)((g_jcQh[s] + 1) % JCQ_N);
			// one report per slot per call; the rest next loop
			continue;
		}
		// not until the host has finished init + selected 0x30
		if (g_swProReportMode[s] != 0x30)
			continue;
		// The Switch integrates the report's 3 IMU samples by SAMPLE COUNT at a fixed ~5 ms/sample (it ignores
		// the timer byte and assumes a 3-samples-per-15ms genuine cadence). Streaming faster (e.g. 250 Hz x 3
		// = 750 samples/s) over-credits gyro rotation ~3.75x, so residual bias accumulates into the slow
		// orientation lean that builds over minutes and resets on replug. 15 ms matches the genuine push,
		// making integration 1:1.
		uint32_t interval = (g_swProRate == 2) ? USB_STREAM_MS :
				    (g_swProRate == 1) ? SW_PRO_REPORT_MS_120 :
							 SW_PRO_REPORT_MS;
		if (millis() - g_swProLastMs[s] < interval)
			continue;
		g_swProLastMs[s] = millis();
		uint8_t p[63];
		switchProBuild((uint8_t)s, p);
		usbTxHid(&g_swPro[s], 0x30, p, sizeof p);
	}
}
