#include "mode_ps3.h"
#include "triton.h"
#include "gamepad_util.h"
#include "src/common/report_build.h"
#include "config.h"
#include "haptics.h"
#include "bonds.h"
#include "usb_tx.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <string.h>
#include "src/common/hid_reports.h"

Ps3Controller g_ps3Ctl;

// ---------------------------------------------------------------------------------------------------------
// Genuine Sony Sixaxis / DualShock 3 (054C:0268) HID report descriptor -- 148 bytes, captured verbatim from
// real hardware (Nefarius "SIXAXIS native HID Report Descriptor"). The PS3 console does NOT parse this to
// drive input -- it recognises the pad by VID/PID and the control-transfer handshake below -- but a faithful
// descriptor (declaring input 0x01, output 0x01, and feature reports 0x01/0x02/0xEE/0xEF) keeps both the
// console and PC HID stacks happy. Do not "tidy" it: this is the real firmware's (famously odd) descriptor.
// ---------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------
// Magic GET_REPORT(Feature) responses. Lifted verbatim from GIMX-firmwares EMUPS3 (matlo) -- a LUFA-based DS3
// emulator proven to enumerate on a real PS3. GIMX sends each 64-byte array RAW (Endpoint_Write_Control_-
// Stream_LE), so the genuine wire byte 0 is the array's first byte -- which for a Sixaxis is NOT the report
// id (e.g. GET 0xF5 returns 01 00.., GET 0xEF returns 00 EF..). See emitFeature() for how we reproduce that
// despite TinyUSB force-prepending the report id. The PS3 enable handshake is:
//   GET 0x01 -> GET 0xF2 -> {SET 0xEF / GET 0xEF}x2 -> GET 0xF8 -> SET output 0x01 -> SET 0xF4(enable)
// then input report 0x01 streams. (GIMX does not even handle 0xF4 and streams unconditionally; so do we.)
// ---------------------------------------------------------------------------------------------------------
// 0xF2 device info; bytes [4..9] = the controller's Bluetooth MAC (overlaid from g_ds3Mac at runtime).
// 0xF5 host-pairing info; bytes [2..7] = master (host) BT MAC, overlaid once learned from a SET 0xF5.
// 0xEF / 0xF8 calibration; byte [7] echoes whatever the PS3 last wrote in SET 0xEF byte[6] (the "do you
// remember" check the console performs during the 0xEF dance).

// One HID slot -- a PS3 expects exactly one Sixaxis per USB port.
static Adafruit_USBD_HID g_ds3;
static unsigned long g_ds3LastMs = 0;
// A slot counts as the live controller while it has replied within this window (matches usb_mount's CONN_UP_MS).
#define DS3_CONN_MS 1200u
// First bonded slot with a recent RF reply (the controller we present), or -1 if none is connected. Used for
// both input streaming and rumble routing (this mode is static, so the dynamic g_usbToBond map isn't built).
static int ds3ActiveSlot()
{
	unsigned long now = millis();
	for (int s = 0; s < NSLOT; s++)
		if (g_slot[s].used && g_connReplyMs[s] &&
		    (now - g_connReplyMs[s]) < DS3_CONN_MS)
			return s;
	return -1;
}
static uint8_t g_byte6ef =
	0xb0; // last SET-0xEF byte[6]; echoed back in GET 0xEF/0xF8 byte[7]
static uint8_t g_masterBd[6] = {
	0, 0, 0, 0, 0, 0
}; // host MAC learned via SET 0xF5
static bool g_haveMaster = false;

// Controller BT MAC. OUI 00:1B:DC matches the other PS modes here; last byte 0x80 keeps it distinct. Used
// only for BT pairing identity -- irrelevant over USB, but the PS3 reads it via GET 0xF2.
static uint8_t g_ds3Mac[6] = { 0x00, 0x1B, 0xDC, 0x4F, 0x55, 0x80 };

// Reproduce a genuine 64-byte Sixaxis feature report on the wire. TinyUSB's HID GET_REPORT path force-writes
// the requested report id as wire byte 0 and hands us the buffer PAST it (`buf` = ctrl[1]); a real Sixaxis
// (and GIMX) instead puts the array's own first byte there. So we overwrite that prepended id via buf[-1]
// (== ctrl[0], a valid writable byte for this control transfer) and copy bytes [1..] into buf. Returns the
// payload length; the stack adds the 1 byte we placed -> total = min(64, wLength), exactly like GIMX's
// Endpoint_Write_Control_Stream_LE(arr, wLength). reqlen is post-prepend (host wLength - 1); the PS3 always
// reads these with wLength >= 8, so the prepend always happened and buf[-1] is in bounds.
static uint16_t emitFeature(uint8_t *buf, uint16_t reqlen,
			    const uint8_t arr[64])
{
	// buf[-1] is only the prepended id (and writable) when the stack actually prepended -- which it does iff
	// the host's wLength was > 1. reqlen here is already post-decrement, so reqlen < 2 means either no prepend
	// (wLength <= 1) or a 2-byte request; bail in both cases so we never write out of bounds. The PS3 reads
	// these reports with wLength >= 8, so real requests are unaffected.
	if (reqlen < 2)
		return 0;
	buf[-1] = arr[0];
	uint16_t n = 63; // 64-byte report minus byte 0 (placed above)
	if (n > reqlen)
		n = reqlen;
	memcpy(buf, arr + 1, n);
	return n;
}

static uint16_t ds3GetReport(uint8_t rid, hid_report_type_t type, uint8_t *buf,
			     uint16_t reqlen)
{
	if (type != HID_REPORT_TYPE_FEATURE || !buf || reqlen == 0)
		return 0;
	uint8_t r[64];
	switch (rid) {
	case 0x01:
		return emitFeature(buf, reqlen, report_01);
	case 0xf2: // overlay our controller MAC at [4..9]
		memcpy(r, report_f2, 64);
		memcpy(r + 4, g_ds3Mac, 6);
		return emitFeature(buf, reqlen, r);
	case 0xf5: // overlay learned host MAC at [2..7] (else the dummy aa.. from the template)
		memcpy(r, report_f5, 64);
		if (g_haveMaster)
			memcpy(r + 2, g_masterBd, 6);
		return emitFeature(buf, reqlen, r);
	case 0xef: // echo the PS3's remembered byte at [7]
		memcpy(r, report_ef, 64);
		r[7] = g_byte6ef;
		return emitFeature(buf, reqlen, r);
	case 0xf8:
		memcpy(r, report_f8, 64);
		r[7] = g_byte6ef;
		return emitFeature(buf, reqlen, r);
	case 0xf7:
		return emitFeature(buf, reqlen, report_f7);
	default:
		return 0;
	}
}

static void ds3SetReport(uint8_t rid, hid_report_type_t type, uint8_t const *b,
			 uint16_t n)
{
	if (type == HID_REPORT_TYPE_FEATURE) {
		// 0xEF: remember byte[6] so the next GET 0xEF/0xF8 echoes it. 0xF5: learn the host's BT MAC.
		// 0xF4: the PS3's "set operational"/power command -- ACK silently (no gating needed; we stream
		// unconditionally like GIMX). b[1]==0x08 would be a power-off request; ignored here.
		if (rid == 0xef && n >= 7)
			g_byte6ef = b[6];
		else if (rid == 0xf5 && n >= 8) {
			memcpy(g_masterBd, b + 2, 6);
			g_haveMaster = true;
		}
		return;
	}
	if (type != HID_REPORT_TYPE_OUTPUT || n < 1)
		return;
	// DS3 output report 0x01 (rumble + LED). It reaches us two ways: on the OUT interrupt endpoint (rid=0,
	// report id 0x01 included as b[0]) or via control SET_REPORT(Output,0x01) (rid=0x01, id not in the data).
	// Normalise to `p` = the bytes AFTER the report id, where the full report is
	//   [0]=id [1]=0x00 [2]=small(right/HFR) dur [3]=small power(on/off) [4]=large(left/LFR) dur [5]=large power.
	// So in p: p[2]=small power, p[4]=large power. Map large->lowFreq, small->highFreq;
	// 257 = 65535/255 scales 8-bit power to the 16-bit haptic range.
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) { // OUT endpoint: report id leads the transfer
		if (b[0] != 0x01)
			return;
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else if (rid ==
		   0x01) { // control SET_REPORT: id may or may not be echoed in the data
		if (b[0] == 0x01) {
			p = b + 1;
			pn = (uint16_t)(n - 1);
		} else {
			p = b;
			pn = n;
		}
	} else {
		return;
	}
	if (pn < 5)
		return;
	int slot = ds3ActiveSlot();
	if (slot < 0)
		return;
	hapticSteamRumble((uint16_t)p[4] * 257u, p[2] ? 0xFFFFu : 0u,
			  (uint8_t)slot);
}

// SC2 IMU int16 (center 0) -> DS3 10-bit unsigned (center 511), little-endian on the wire (low byte first).
// >>6 maps the full int16 swing onto roughly +/-512 around center; the PS3 applies its own calibration so
// exact scale is not critical. Writes 2 bytes at out[0..1].

// Build the 48-byte input-report PAYLOAD (the stack prepends report id 0x01 -> 49-byte Sixaxis report).
// Offsets are the genuine report's rd[] minus one (rd[0] is the prepended id).
static void ds3Build(uint8_t slot, uint8_t out[48])
{
	// Report layout is in the shared builder (src/common/report_build.c).
	report_cfg_t cfg = { g_abSwap != 0,
			     { g_back[0], g_back[1], g_back[2], g_back[3] },
			     g_qamMap };
	build_ds3(&g_in[slot], &cfg, out);
}

// Neutral input report: centered sticks + IMU, no buttons. Streamed when no controller is linked so the PS3
// keeps the Sixaxis "alive" (and a games/XMB controller slot) instead of seeing a silent endpoint.
static void ds3Neutral(uint8_t out[48])
{
	// A zeroed input through the shared builder IS the neutral report (centered
	// sticks + IMU, no buttons, battery full).
	puck_input_t neutral = { 0 };
	report_cfg_t cfg = { false, { 0, 0, 0, 0 }, 0 };
	build_ds3(&neutral, &cfg, out);
}

void Ps3Controller::usbIdentity()
{
	// Genuine Sixaxis / DualShock 3 identity. The PS3 recognises the pad by exactly this VID/PID.
	USBDevice.setID(0x054C, 0x0268);
	USBDevice.setDeviceVersion(0x0100);
	USBDevice.setManufacturerDescriptor("Sony");
	USBDevice.setProductDescriptor("PLAYSTATION(R)3 Controller");
}

// Static mount: present the single DS3 interface permanently (the setup() static path calls begin() then
// attaches; it does NOT call usbIdentity() or build a slot pool, so we set the identity and register the HID
// here). g_ds3.begin() both adds the interface to the descriptor and locks its TinyUSB instance.
void Ps3Controller::begin()
{
	usbIdentity();
	g_ds3.enableOutEndpoint(
		true); // OUT endpoint for rumble/LED output reports
	g_ds3.setReportCallback(ds3GetReport, ds3SetReport);
	g_ds3.setReportDescriptor(DS3_HID_DESC, sizeof DS3_HID_DESC);
	g_ds3.setPollInterval(1); // 1 ms interrupt IN, like the genuine pad
	g_ds3.begin();
}

void Ps3Controller::task()
{
	if (!g_ds3.ready())
		return;
	if (millis() - g_ds3LastMs < USB_STREAM_MS)
		return;
	g_ds3LastMs = millis();
	uint8_t p[48];
	int slot = ds3ActiveSlot();
	if (slot >= 0)
		ds3Build((uint8_t)slot, p);
	else
		ds3Neutral(p);
	usbTxHid(&g_ds3, 0x01, p, sizeof p);
}
