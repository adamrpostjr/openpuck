// webusb.c — WebUSB config channel.
//
// PicoPuck speaks OpenPuck's WebUSB protocol so the same panel drives the common
// cards identically: the status blob (marker 0xA5, protocol v17, 180-byte
// payload) and the standard opcode set (0x01 status, 0x02 set-field, 0x03 mode
// switch, 0x0A erase, 0x0C UF2 bootloader, …). Bluetooth pairing — the only
// PicoPuck-specific UI — rides opcodes 0x16-0x19 and device→host marker 0xAD,
// which OpenPuck doesn't use, so nothing collides.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "usb/webusb.h"
#include "config/picopuck_config.h"
#include "config/modes.h"
#include "puck/slots.h"
#include "puck/relay.h"
#include "puck/personality.h"
#include "sys/settings.h"
#include "bt/bt_control.h"

#include <string.h>
#include "tusb.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"

// From other TUs (declared locally to avoid pulling BTstack headers into this
// TinyUSB translation unit).
extern volatile uint16_t g_valve_writes;
uint8_t bt_report_count(int slot);

#define WB_VER 17
#define WB_PAYLEN 180

static bool s_connected;

void webusb_init(void) { s_connected = false; }
void webusb_set_connected(bool c) { s_connected = c; }

static void send_frame(const uint8_t *f, uint16_t len)
{
	if (tud_vendor_write_available() < len)
		return;
	tud_vendor_write(f, len);
	tud_vendor_write_flush();
}

static int active_slot(void)
{
	for (int s = 0; s < PP_NSLOT; s++)
		if (g_slot[s].connected)
			return s;
	return -1;
}

// ---- 0xA5 status blob (OpenPuck v17 layout; payload index = fw index − 2) ----
static void send_blob(void)
{
	uint8_t f[2 + WB_PAYLEN];
	memset(f, 0, sizeof(f));
	f[0] = 0xA5;
	f[1] = WB_PAYLEN;
	uint8_t *p = f + 2;  // payload; p[i] == OpenPuck panel index i
	const pp_cfg_t *c = settings();

	int as = active_slot();
	const pp_type_cfg_t *at = &c->type[0];  // active-type mirror (kept simple)

	p[0] = WB_VER;
	p[1] = c->mode;
	p[2] = c->m_div;
	p[3] = c->m_fric;
	p[4] = at->qam;
	p[5] = at->ab_swap;
	p[6] = at->back[0]; p[7] = at->back[1]; p[8] = at->back[2]; p[9] = at->back[3];
	p[10] = (as < 0) ? 0xFF : (uint8_t)as;
	p[11] = (as >= 0);
	p[20] = 1;  // fwd-new-only
	p[22] = c->persist_mode;
	p[23] = c->chord[0]; p[24] = c->chord[1]; p[25] = c->chord[2];
	p[36] = (as >= 0) ? g_battery[as] : 0;
	p[37] = (as >= 0 && g_link_rssi[as]) ? (uint8_t)(-g_link_rssi[as]) : 0;
	strncpy((char *)(p + 39), PICOPUCK_GIT_COMMIT, 12);
	p[51] = c->rumble_scale;
	p[52] = c->sw_pro_rate;
	p[53] = c->sw_gyro10;
	int bonded = 0;
	for (int s = 0; s < PP_NSLOT; s++)
		if (g_slot[s].connected)
			bonded++;
	p[60] = (uint8_t)bonded;
	for (int s = 0; s < PP_NSLOT; s++) {
		uint8_t *ss = p + 61 + s * 3;
		ss[0] = g_slot[s].connected;
		ss[1] = g_battery[s];
		ss[2] = g_link_rssi[s] ? (uint8_t)(-g_link_rssi[s]) : 0;
	}
	for (int et = 0; et < 4; et++) {
		uint8_t *tt = p + 73 + et * 9;
		const pp_type_cfg_t *t = &c->type[et];
		tt[0] = t->back[0]; tt[1] = t->back[1]; tt[2] = t->back[2]; tt[3] = t->back[3];
		tt[4] = t->qam; tt[5] = t->ab_swap; tt[6] = t->pad_haptics;
		tt[7] = t->led_bright; tt[8] = t->rumble;
	}
	p[126] = 0xFF;  // hang stage = none
	p[179] = c->land_all_87;

	send_frame(f, sizeof(f));
}

// ---- 0xAD Bluetooth status frame (PicoPuck-specific pairing UI) -------------
// NOTE: this frame carries a 16-BIT little-endian length (marker + 2 length
// bytes + payload), unlike the 1-byte-length OpenPuck frames — with a full scan
// list the payload exceeds 255 bytes, so a 1-byte length would wrap and truncate
// the frame (dropping slots / the scan list intermittently). The panel's
// readFrame() special-cases 0xAD to read the wide length.
#define BT_SLOT_REC 26
#define BT_SCAN_REC 25
#define BT_SCAN_MAX 10
static void send_bt_frame(void)
{
	uint8_t f[3 + 3 + PP_NSLOT * BT_SLOT_REC + 1 + BT_SCAN_MAX * BT_SCAN_REC + 8];
	uint8_t *p = f;
	*p++ = 0xAD;
	uint8_t *plen = p; p += 2;   // 16-bit LE length, backfilled below
	uint8_t *start = p;
	*p++ = 1;             // frame version
	*p++ = PP_BOARD;
	*p++ = bt_scan_active() ? 1 : 0;

	// Controller list (PP_NSLOT recs): live connections first, then persisted
	// bonds that are offline (so pairings show after a reboot / mode switch
	// before they reconnect), padded with empty. state: 0 empty,1 bonded,2 live.
	struct { uint8_t state, kind, batt; int8_t rssi; uint8_t addr[6]; char name[16]; }
		rec[PP_NSLOT];
	memset(rec, 0, sizeof(rec));
	int nr = 0;
	for (int s = 0; s < PP_NSLOT && nr < PP_NSLOT; s++) {
		uint8_t kind = 0; int8_t rssi = 0; uint8_t addr[6] = { 0 };
		char name[16] = { 0 };
		if (g_slot[s].connected && bt_slot_info(s, &kind, &rssi, addr, name)) {
			rec[nr].state = 2; rec[nr].kind = kind;
			rec[nr].batt = g_battery[s]; rec[nr].rssi = rssi;
			memcpy(rec[nr].addr, addr, 6); memcpy(rec[nr].name, name, 16);
			nr++;
		}
	}
	uint8_t baddr[PP_NSLOT][6], btype[PP_NSLOT];
	uint8_t nb = bt_bond_offline_list(baddr, btype, PP_NSLOT);
	for (uint8_t i = 0; i < nb && nr < PP_NSLOT; i++) {
		rec[nr].state = 1;
		// Recover the persisted name/kind; fall back to a generic BLE pad label.
		uint8_t k = 2; char nm[16] = { 0 };
		if (settings_bond_lookup(baddr[i], &k, nm)) {
			rec[nr].kind = k;
			memcpy(rec[nr].name, nm, 16);
		} else {
			rec[nr].kind = 2;
		}
		memcpy(rec[nr].addr, baddr[i], 6);
		nr++;
	}
	for (int r = 0; r < PP_NSLOT; r++) {
		*p++ = rec[r].state; *p++ = rec[r].kind;
		*p++ = rec[r].batt; *p++ = (uint8_t)rec[r].rssi;
		memcpy(p, rec[r].addr, 6); p += 6;
		memcpy(p, rec[r].name, 16); p += 16;
	}

	bt_scan_entry_t ents[BT_SCAN_MAX];
	uint8_t n = bt_scan_list(ents, BT_SCAN_MAX);
	*p++ = n;
	for (uint8_t i = 0; i < n; i++) {
		*p++ = ents[i].kind;
		*p++ = ents[i].addr_type;
		*p++ = (uint8_t)ents[i].rssi;
		memcpy(p, ents[i].addr, 6); p += 6;
		memcpy(p, ents[i].name, 16); p += 16;
	}
	// Diagnostics tail.
	uint16_t adv = 0, hev = 0; uint8_t flags = 0, hci = 0;
	bt_scan_diag(&adv, &flags, &hci, &hev);
	*p++ = (uint8_t)(adv & 0xFF); *p++ = (uint8_t)(adv >> 8);
	*p++ = flags; *p++ = hci;
	*p++ = (uint8_t)(g_valve_writes & 0xFF); *p++ = (uint8_t)(g_valve_writes >> 8);

	uint16_t pl = (uint16_t)(p - start);
	plen[0] = (uint8_t)(pl & 0xFF); plen[1] = (uint8_t)(pl >> 8);
	send_frame(f, (uint16_t)(p - f));
}

// ---- opcode lengths (incl. opcode byte); 0 = unknown → resync -------------
static uint8_t opcode_len(uint8_t op)
{
	switch (op) {
	case 0x01: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x0C:
	case 0x11: case 0x14: case 0x15: case 0x22: case 0x23: case 0x24:
	case 0x19:
		return 1;
	case 0x02: return 3;
	case 0x03: case 0x05: case 0x0E: case 0x0F: case 0x10: case 0x13:
	case 0x16:
		return 2;
	case 0x0A: return 4;
	case 0x25: return 5;
	case 0x20: return 9;
	case 0x12: return 18;
	case 0x0D: return 27;
	case 0x17: case 0x18: return 8;
	// 0x21 is variable (6 + data) — handled in the parser.
	default: return 0;
	}
}

static void dispatch(const uint8_t *c)
{
	switch (c[0]) {
	case 0x01: send_blob(); break;
	case 0x02: settings_set_field(c[1], c[2]); send_blob(); break;
	case 0x03:
		if (c[1] <= MODE_MAX) { settings_set_mode(c[1]); watchdog_reboot(0, 0, 1); }
		break;
	case 0x08:
		// Power off the connected SC2 (ID_TURN_OFF); ignored by generic pads.
		for (int s = 0; s < PP_NSLOT; s++)
			if (g_slot[s].connected)
				relay_enqueue(s, 0x9F, (const uint8_t *)"off!", 4);
		break;
	case 0x0A:
		if (c[1] == 0x45 && c[2] == 0x52 && c[3] == 0x53) {  // "ERS"
			bt_forget_all();
			settings_factory_reset();
			watchdog_reboot(0, 0, 1);
		}
		break;
	case 0x0B:
	case 0x0C:  // reboot to UF2 bootloader (PicoPuck has no serial DFU)
		watchdog_disable();
		reset_usb_boot(0, 0);
		break;
	// --- Bluetooth pairing (PicoPuck-specific) ---
	case 0x16: if (c[1]) bt_scan_start(c[1]); else bt_scan_stop(); break;
	case 0x17: bt_pair(&c[1], c[7]); break;
	case 0x18: bt_forget(&c[1], c[7]); break;
	case 0x19: send_bt_frame(); break;
	default: break;  // stubs (0x07/0x09/0x0D/0x0E/0x0F/0x10/0x11-0x15/0x20-0x25)
	}
}

void webusb_task(void)
{
	static uint8_t rx[64];
	static uint8_t rxlen;

	while (tud_vendor_available()) {
		uint32_t got = tud_vendor_read(rx + rxlen,
					       (uint32_t)(sizeof(rx) - rxlen));
		if (!got)
			break;
		rxlen += (uint8_t)got;
	}

	uint8_t off = 0;
	while (off < rxlen) {
		uint8_t op = rx[off];
		uint8_t need;
		if (op == 0x21) {  // fw chunk: 6 header + data[5]
			if ((uint8_t)(rxlen - off) < 6)
				break;
			need = (uint8_t)(6 + rx[off + 5]);
		} else {
			need = opcode_len(op);
		}
		if (need == 0) {
			off++;  // unknown opcode → resync
			continue;
		}
		if ((uint8_t)(rxlen - off) < need)
			break;
		dispatch(&rx[off]);
		off += need;
	}
	if (off > 0 && off < rxlen)
		memmove(rx, rx + off, (size_t)(rxlen - off));
	rxlen -= off;
}
