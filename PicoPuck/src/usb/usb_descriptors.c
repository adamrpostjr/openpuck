// usb_descriptors.c — mode-aware TinyUSB descriptors.
//
// Puck modes (Steam/Lizard) present VID/PID 28DE:1304 with four HID slot
// interfaces (the cloned puck descriptor) + a WebUSB vendor interface. Emulated
// modes (Xbox/Switch/PS5/PS3/…) present that mode's controller as ONE HID
// gamepad + the same WebUSB vendor interface (so the panel works in every mode).
// The active mode is read from flash at boot; a mode switch persists + reboots.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string.h>
#include <stdio.h>
#include "tusb.h"
#include "pico/time.h"
#include "usb/usb_descriptors.h"
#include "usb/puck_desc.h"
#include "puck/identity.h"
#include "puck/emu.h"
#include "config/modes.h"
#include "sys/settings.h"

void webusb_set_connected(bool connected);

#if CFG_TUD_CDC
#define PP_BCD PP_BCD_DEVICE_CDC
#else
#define PP_BCD PP_BCD_DEVICE
#endif

// ---- cached per-boot presentation ------------------------------------------
static uint8_t s_mode;
static const emu_mode_t *s_emu; // NULL for puck/xinput modes
static bool s_xinput; // MODE_XBOX: custom vendor class, not HID
static uint8_t s_emu_ifaces =
	1; // # of emulated HID interfaces (one per controller)
static tusb_desc_device_t s_dev;
static uint8_t s_emu_cfg[220]; // PP_NSLOT HID (32 B each) + config + vendor
static uint8_t s_vendor_itf;
static char s_emu_serial[24]; // emulated serial; encodes the mounted count
static void rebuild_emu_serial(void);

uint8_t usb_emu_iface_count(void)
{
	return s_emu_ifaces;
}

#define MS_OS_20_DESC_LEN 0xB2
static uint8_t s_msos[MS_OS_20_DESC_LEN];

// Build the emulated-mode config descriptor: `nif` HID gamepad interfaces (one
// per controller slot, each with an IN + OUT endpoint) followed by the WebUSB
// vendor interface. Presenting several HID interfaces is what lets more than one
// connected controller appear to the host at once (each interface = one pad).
// Returns total length. HID endpoints: IN 0x81+i / OUT 0x01+i; vendor 0x85/0x05.
static uint16_t build_emu_config(uint8_t *b, const emu_mode_t *emu, uint8_t nif)
{
	uint16_t rlen = emu->report_desc_len;
	uint8_t poll = emu->poll_ms ? emu->poll_ms : 1;
	// nif may be 0 (no controllers connected yet → present only the WebUSB vendor
	// interface); the dynamic mounter grows/shrinks it as controllers come/go.
	if (nif > PP_NSLOT)
		nif = PP_NSLOT;
	const uint16_t total = 9 + nif * (9 + 9 + 7 + 7) + (9 + 7 + 7);
	uint8_t *p = b;
	// configuration
	*p++ = 9;
	*p++ = TUSB_DESC_CONFIGURATION;
	*p++ = (uint8_t)(total & 0xFF);
	*p++ = (uint8_t)(total >> 8);
	*p++ = (uint8_t)(nif + 1);
	*p++ = 1;
	*p++ = 0;
	*p++ = 0x80;
	*p++ = 250;
	for (uint8_t i = 0; i < nif; i++) {
		// HID interface i — IN + OUT (rumble / Switch-Pro subcommands ride the
		// interrupt OUT pipe; output reports also work via EP0 SET_REPORT).
		*p++ = 9;
		*p++ = TUSB_DESC_INTERFACE;
		*p++ = i;
		*p++ = 0;
		*p++ = 2;
		*p++ = TUSB_CLASS_HID;
		*p++ = 0;
		*p++ = 0;
		*p++ = 0;
		*p++ = 9;
		*p++ = HID_DESC_TYPE_HID;
		*p++ = 0x11;
		*p++ = 0x01;
		*p++ = 0;
		*p++ = 1;
		*p++ = HID_DESC_TYPE_REPORT;
		*p++ = (uint8_t)(rlen & 0xFF);
		*p++ = (uint8_t)(rlen >> 8);
		*p++ = 7;
		*p++ = TUSB_DESC_ENDPOINT;
		*p++ = (uint8_t)(0x81 + i);
		*p++ = 0x03;
		*p++ = 64;
		*p++ = 0;
		*p++ = poll;
		*p++ = 7;
		*p++ = TUSB_DESC_ENDPOINT;
		*p++ = (uint8_t)(0x01 + i);
		*p++ = 0x03;
		*p++ = 64;
		*p++ = 0;
		*p++ = poll;
	}
	// vendor interface (last) — WebUSB
	*p++ = 9;
	*p++ = TUSB_DESC_INTERFACE;
	*p++ = nif;
	*p++ = 0;
	*p++ = 2;
	*p++ = TUSB_CLASS_VENDOR_SPECIFIC;
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;
	*p++ = 7;
	*p++ = TUSB_DESC_ENDPOINT;
	*p++ = 0x05;
	*p++ = 0x02;
	*p++ = 64;
	*p++ = 0;
	*p++ = 0;
	*p++ = 7;
	*p++ = TUSB_DESC_ENDPOINT;
	*p++ = 0x85;
	*p++ = 0x02;
	*p++ = 64;
	*p++ = 0;
	*p++ = 0;
	return total;
}

// Build the XInput config: the WebUSB vendor interface FIRST (itf 0), so the
// built-in vendor class driver claims it, then `k` XInput interfaces (itf 1..k,
// class 0xFF/0x5D/0x01) — ONE Xbox-360-wired pad per CONNECTED controller, so
// multiple controllers each show up on the OS (matches OpenPuck's dynamic XInput
// mounting; the old build presented a single fixed pad → only one controller
// ever appeared). Endpoints: vendor bulk 0x02/0x82; XInput pad j uses endpoint
// number 3+j (interrupt IN 0x83+j / OUT 0x03+j). Returns total length.
static uint16_t build_xinput_config(uint8_t *b, uint8_t k)
{
	if (k > PP_NSLOT)
		k = PP_NSLOT;
	const uint16_t total = 9 + (9 + 7 + 7) + k * (9 + 17 + 7 + 7);
	uint8_t *p = b;
	// configuration (vendor + k pad interfaces)
	*p++ = 9;
	*p++ = TUSB_DESC_CONFIGURATION;
	*p++ = (uint8_t)(total & 0xFF);
	*p++ = (uint8_t)(total >> 8);
	*p++ = (uint8_t)(1 + k);
	*p++ = 1;
	*p++ = 0;
	*p++ = 0x80;
	*p++ = 250;
	// vendor interface (itf 0) — WebUSB, endpoints 0x02/0x82
	*p++ = 9;
	*p++ = TUSB_DESC_INTERFACE;
	*p++ = 0;
	*p++ = 0;
	*p++ = 2;
	*p++ = TUSB_CLASS_VENDOR_SPECIFIC;
	*p++ = 0;
	*p++ = 0;
	*p++ = 4;
	*p++ = 7;
	*p++ = TUSB_DESC_ENDPOINT;
	*p++ = 0x02;
	*p++ = 0x02;
	*p++ = 64;
	*p++ = 0;
	*p++ = 0;
	*p++ = 7;
	*p++ = TUSB_DESC_ENDPOINT;
	*p++ = 0x82;
	*p++ = 0x02;
	*p++ = 64;
	*p++ = 0;
	*p++ = 0;
	// k XInput interfaces (itf 1..k)
	for (uint8_t j = 0; j < k; j++) {
		uint8_t itfnum = (uint8_t)(1 + j);
		uint8_t epin = (uint8_t)(0x83 + j), epout = (uint8_t)(0x03 + j);
		// XInput interface — vendor class 0xFF/0x5D/0x01, 2 interrupt eps
		*p++ = 9;
		*p++ = TUSB_DESC_INTERFACE;
		*p++ = itfnum;
		*p++ = 0;
		*p++ = 2;
		*p++ = 0xFF;
		*p++ = 0x5D;
		*p++ = 0x01;
		*p++ = 0;
		// XInput "unknown" 0x21 descriptor (bytes [6]/[13] = IN/OUT ep addr)
		*p++ = 0x11;
		*p++ = 0x21;
		*p++ = 0x00;
		*p++ = 0x01;
		*p++ = 0x01;
		*p++ = 0x25;
		*p++ = epin;
		*p++ = 0x14;
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x13;
		*p++ = epout;
		*p++ = 0x08;
		*p++ = 0x00;
		*p++ = 0x00;
		// IN endpoint (interrupt, 1 ms)
		*p++ = 7;
		*p++ = TUSB_DESC_ENDPOINT;
		*p++ = epin;
		*p++ = 0x03;
		*p++ = 0x20;
		*p++ = 0;
		*p++ = 1;
		// OUT endpoint (interrupt, 8 ms)
		*p++ = 7;
		*p++ = TUSB_DESC_ENDPOINT;
		*p++ = epout;
		*p++ = 0x03;
		*p++ = 0x20;
		*p++ = 0;
		*p++ = 8;
	}
	return total;
}

// MS OS 2.0 template (WINUSB compatible id + DeviceInterfaceGUIDs). The
// function-subset "first interface" byte is patched per mode at init.
static const uint8_t k_msos_template[MS_OS_20_DESC_LEN] = {
	U16_TO_U8S_LE(0x000A),
	U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
	U32_TO_U8S_LE(0x06030000),
	U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
	U16_TO_U8S_LE(0x0008),
	U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
	0,
	0,
	U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
	U16_TO_U8S_LE(0x0008),
	U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
	0 /* first interface, patched */,
	0,
	U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
	U16_TO_U8S_LE(0x0014),
	U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
	'W',
	'I',
	'N',
	'U',
	'S',
	'B',
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
	U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
	U16_TO_U8S_LE(0x0007),
	U16_TO_U8S_LE(0x002A),
	'D',
	0x00,
	'e',
	0x00,
	'v',
	0x00,
	'i',
	0x00,
	'c',
	0x00,
	'e',
	0x00,
	'I',
	0x00,
	'n',
	0x00,
	't',
	0x00,
	'e',
	0x00,
	'r',
	0x00,
	'f',
	0x00,
	'a',
	0x00,
	'c',
	0x00,
	'e',
	0x00,
	'G',
	0x00,
	'U',
	0x00,
	'I',
	0x00,
	'D',
	0x00,
	's',
	0x00,
	0x00,
	0x00,
	U16_TO_U8S_LE(0x0050),
	'{',
	0x00,
	'9',
	0x00,
	'7',
	0x00,
	'5',
	0x00,
	'F',
	0x00,
	'4',
	0x00,
	'4',
	0x00,
	'D',
	0x00,
	'9',
	0x00,
	'-',
	0x00,
	'0',
	0x00,
	'D',
	0x00,
	'0',
	0x00,
	'8',
	0x00,
	'-',
	0x00,
	'4',
	0x00,
	'3',
	0x00,
	'F',
	0x00,
	'D',
	0x00,
	'-',
	0x00,
	'8',
	0x00,
	'B',
	0x00,
	'3',
	0x00,
	'E',
	0x00,
	'-',
	0x00,
	'1',
	0x00,
	'2',
	0x00,
	'7',
	0x00,
	'C',
	0x00,
	'A',
	0x00,
	'8',
	0x00,
	'A',
	0x00,
	'F',
	0x00,
	'F',
	0x00,
	'F',
	0x00,
	'9',
	0x00,
	'D',
	0x00,
	'}',
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
};

void usb_descriptors_init(void)
{
	s_mode = settings_mode();
	s_xinput = mode_is_xinput(s_mode);
	s_emu = (mode_is_puck(s_mode) || s_xinput) ? NULL :
						     emu_mode_for(s_mode);
	if (!mode_is_puck(s_mode) && !s_xinput && !s_emu)
		s_mode =
			MODE_STEAM; // unknown/unimplemented emu mode → puck fallback

	memset(&s_dev, 0, sizeof(s_dev));
	s_dev.bLength = sizeof(tusb_desc_device_t);
	s_dev.bDescriptorType = TUSB_DESC_DEVICE;
	s_dev.bcdUSB = 0x0210;
	s_dev.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
	s_dev.iManufacturer = 0x01;
	s_dev.iProduct = 0x02;
	s_dev.iSerialNumber = 0x03;
	s_dev.bNumConfigurations = 0x01;
	if (s_xinput) {
		// Xbox 360 wired: Windows xusb / Linux xpad / SDL bind 045E:028E.
		s_dev.idVendor = 0x045E;
		s_dev.idProduct = 0x028E;
		s_dev.bcdDevice = 0x0120;
		// Start with NO pad interfaces (nothing connected at boot); the shared
		// dynamic mounter grows the count to match the CONNECTED controllers and
		// shrinks it lazily — same as the emulated-HID modes, so 2 controllers
		// present 2 XInput pads.
		s_emu_ifaces = 0;
		build_xinput_config(s_emu_cfg, s_emu_ifaces);
		s_vendor_itf = 0; // WebUSB vendor is itf 0 in the XInput config
		rebuild_emu_serial();
	} else if (s_emu) {
		s_dev.idVendor = s_emu->vid;
		s_dev.idProduct = s_emu->pid;
		s_dev.bcdDevice = s_emu->bcd;
		// Start with NO gamepad interfaces (nothing is connected at boot); the
		// dynamic mounter (usb_mount / usb_reenumerate) grows the count to match
		// the CONNECTED controllers and shrinks it lazily — same behaviour as
		// OpenPuck (present only connected pads, not bonded ones).
		s_emu_ifaces = 0;
		build_emu_config(s_emu_cfg, s_emu, s_emu_ifaces);
		s_vendor_itf =
			s_emu_ifaces; // vendor is the interface after the HIDs
		rebuild_emu_serial();
	} else {
		s_dev.idVendor = PP_USB_VID;
		s_dev.idProduct = PP_USB_PID;
		s_dev.bcdDevice = PP_BCD;
		s_vendor_itf = ITF_NUM_VENDOR;
	}

	memcpy(s_msos, k_msos_template, MS_OS_20_DESC_LEN);
	s_msos[22] = s_vendor_itf; // function-subset "first interface"
}

const uint8_t *tud_descriptor_device_cb(void)
{
	return (const uint8_t *)&s_dev;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
	(void)instance;
	return s_emu ? s_emu->report_desc : PUCK_HID_DESC;
}

// The emulated-mode serial encodes the mounted-interface count, so the host
// invalidates its cached config descriptor whenever we re-enumerate up/down
// (mirrors OpenPuck's usbReenumerate serial bump).
static void rebuild_emu_serial(void)
{
	snprintf(s_emu_serial, sizeof(s_emu_serial), "%.16s.%u", g_usb_serial,
		 (unsigned)s_emu_ifaces);
}

// Rebuild the emulated config for `k` HID gamepad interfaces (0..PP_NSLOT) and
// refresh the vendor-interface index + MS-OS patch + serial. Called by
// usb_reenumerate() while detached.
void usb_descriptors_set_emu_ifaces(uint8_t k)
{
	if (k > PP_NSLOT)
		k = PP_NSLOT;
	if (s_xinput) {
		// XInput: vendor stays itf 0, k pad interfaces follow. The serial encodes
		// k so the host drops its cached config on every up/down re-enumeration.
		s_emu_ifaces = k;
		build_xinput_config(s_emu_cfg, k);
		rebuild_emu_serial();
		return;
	}
	if (!s_emu)
		return;
	s_emu_ifaces = k;
	build_emu_config(s_emu_cfg, s_emu, k);
	s_vendor_itf = k; // vendor is the interface after the k HIDs
	s_msos[22] = s_vendor_itf;
	rebuild_emu_serial();
}

// Platform hook for the shared dynamic mounter: present `k` gamepad interfaces
// with NO reboot. Rebuild the descriptor, drop off the bus briefly so the host
// sees the change, then re-attach and re-enumerate. Cooperative single-core loop
// context, so this can't race tud_task (unlike OpenPuck's RTOS caveat).
void usb_reenumerate(uint8_t k)
{
	usb_descriptors_set_emu_ifaces(k);
	tud_disconnect();
	busy_wait_ms(50);
	tud_connect();
}

// ---- puck configuration descriptor -----------------------------------------
#define PUCK_CONFIG_TOTAL_LEN                                               \
	(TUD_CONFIG_DESC_LEN + 4 * TUD_HID_DESC_LEN + TUD_VENDOR_DESC_LEN + \
	 CFG_TUD_CDC * TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration_puck[] = {
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, PUCK_CONFIG_TOTAL_LEN, 0x00,
			      250),
	TUD_HID_DESCRIPTOR(ITF_NUM_HID0, 0, HID_ITF_PROTOCOL_NONE,
			   PUCK_HID_DESC_SIZE, EPNUM_HID0,
			   CFG_TUD_HID_EP_BUFSIZE, 1),
	TUD_HID_DESCRIPTOR(ITF_NUM_HID1, 0, HID_ITF_PROTOCOL_NONE,
			   PUCK_HID_DESC_SIZE, EPNUM_HID1,
			   CFG_TUD_HID_EP_BUFSIZE, 1),
	TUD_HID_DESCRIPTOR(ITF_NUM_HID2, 0, HID_ITF_PROTOCOL_NONE,
			   PUCK_HID_DESC_SIZE, EPNUM_HID2,
			   CFG_TUD_HID_EP_BUFSIZE, 1),
	TUD_HID_DESCRIPTOR(ITF_NUM_HID3, 0, HID_ITF_PROTOCOL_NONE,
			   PUCK_HID_DESC_SIZE, EPNUM_HID3,
			   CFG_TUD_HID_EP_BUFSIZE, 1),
	TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, EPNUM_VENDOR_OUT,
			      EPNUM_VENDOR_IN, 64),
#if CFG_TUD_CDC
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT,
			   EPNUM_CDC_IN, 64),
#endif
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index;
	return (s_emu || s_xinput) ? s_emu_cfg : desc_configuration_puck;
}

// ---- BOS + MS OS 2.0 --------------------------------------------------------
#define BOS_TOTAL_LEN                                 \
	(TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + \
	 TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
	TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
	TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 0),
	TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN,
				    VENDOR_REQUEST_MICROSOFT),
};

const uint8_t *tud_descriptor_bos_cb(void)
{
	return desc_bos;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
				tusb_control_request_t const *request)
{
	if (stage != CONTROL_STAGE_SETUP)
		return true;

	switch (request->bmRequestType_bit.type) {
	case TUSB_REQ_TYPE_VENDOR:
		if (request->bRequest == VENDOR_REQUEST_MICROSOFT &&
		    request->wIndex == 7) {
			uint16_t total_len;
			memcpy(&total_len, s_msos + 8, 2);
			return tud_control_xfer(rhport, request,
						(void *)(uintptr_t)s_msos,
						total_len);
		}
		return false;
	case TUSB_REQ_TYPE_CLASS:
		if (request->bRequest == 0x22) {
			webusb_set_connected(request->wValue != 0);
			return tud_control_status(rhport, request);
		}
		break;
	default:
		break;
	}
	return false;
}

// ---- string descriptors ----------------------------------------------------
enum {
	STRID_LANGID = 0,
	STRID_MANUFACTURER,
	STRID_PRODUCT,
	STRID_SERIAL,
	STRID_VENDOR
};
static uint16_t desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;
	size_t chr_count;

	if (index == STRID_LANGID) {
		desc_str[1] = 0x0409;
		chr_count = 1;
	} else {
		const char *str;
		switch (index) {
		case STRID_MANUFACTURER:
			str = s_emu	 ? "PicoPuck" :
			      s_xinput	 ? "Microsoft" :
					   "Valve Software";
			break;
		case STRID_PRODUCT:
			str = s_emu    ? s_emu->product :
			      s_xinput ? "Controller" :
					 "Steam Controller Puck";
			break;
		case STRID_SERIAL:
			// XInput uses the count-encoding serial too (re-enum cache bust).
			str = (s_emu || s_xinput) ? s_emu_serial : g_usb_serial;
			break;
		case STRID_VENDOR:
			str = "PicoPuck WebUSB";
			break;
		default:
			return NULL;
		}
		if (!str)
			return NULL;
		chr_count = strlen(str);
		if (chr_count > 31)
			chr_count = 31;
		for (size_t i = 0; i < chr_count; i++)
			desc_str[1 + i] = str[i];
	}

	desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
	return desc_str;
}
