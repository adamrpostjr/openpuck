// xinput.c — Xbox 360 wired (XInput) personality (see xinput.h).
//
// A custom TinyUSB class driver serves the XInput interfaces (0xFF/0x5D/0x01)
// and their interrupt IN/OUT endpoints. The config descriptor (usb_descriptors.c)
// exposes ONE pad interface per CONNECTED controller via the shared dynamic
// mounter, so two controllers appear as two Xbox pads on the OS (matching
// OpenPuck). Each USB pad interface u (itf u+1; vendor is itf 0) is fed from bond
// slot g_usb_to_bond[u]; host rumble on that interface's OUT endpoint routes back
// to the same bond slot.
//
// PicoPuck's main loop is cooperative (no separate usbd task), so IN transfers
// are issued directly from xinput_task() rather than deferred to a SOF drain.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/xinput.h"
#include "puck/triton.h"
#include "puck/slots.h"
#include "puck/relay.h"
#include "sys/settings.h"
#include "config/modes.h"
#include "usb_mount.h"

#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/time.h"

#define ET_XBOX 0  // per-type config index (matches OpenPuck ET_XBOX)

// XInput button bits.
enum {
	XB_DUP = 0x0001, XB_DDOWN = 0x0002, XB_DLEFT = 0x0004, XB_DRIGHT = 0x0008,
	XB_START = 0x0010, XB_BACK = 0x0020, XB_L3 = 0x0040, XB_R3 = 0x0080,
	XB_LB = 0x0100, XB_RB = 0x0200, XB_GUIDE = 0x0400,
	XB_A = 0x1000, XB_B = 0x2000, XB_X = 0x4000, XB_Y = 0x8000
};

// One state block per USB pad interface (usbSlot u = descriptor interface u,
// itf 1..k). u maps to bond slot g_usb_to_bond[u], exactly like the emu HID path.
static struct {
	uint8_t itf, ep_in, ep_out;
	uint8_t in_buf[32], out_buf[32];
	bool in_use;
} g_xi[PP_NSLOT];

// ---- custom class driver ---------------------------------------------------
static void xi_init(void) { memset(g_xi, 0, sizeof(g_xi)); }
static bool xi_deinit(void) { return true; }
static void xi_reset(uint8_t rhport)
{
	(void)rhport;
	// Cleared on every (re)enumeration; xi_open re-populates the mounted pads.
	for (int u = 0; u < PP_NSLOT; u++) {
		g_xi[u].itf = g_xi[u].ep_in = g_xi[u].ep_out = 0;
		g_xi[u].in_use = false;
	}
}
static uint16_t xi_open(uint8_t rhport, tusb_desc_interface_t const *itf,
			uint16_t max_len)
{
	if (!(itf->bInterfaceClass == 0xFF && itf->bInterfaceSubClass == 0x5D &&
	      itf->bInterfaceProtocol == 0x01))
		return 0;  // not an XInput interface → let another driver claim it
	// Vendor (WebUSB) is itf 0; pad interfaces are itf 1..k → usbSlot u = itf-1.
	uint8_t u = (uint8_t)(itf->bInterfaceNumber - 1);
	if (u >= PP_NSLOT)
		return 0;
	g_xi[u].itf = itf->bInterfaceNumber;
	uint8_t const *p = (uint8_t const *)itf;
	uint8_t const *end = p + max_len;
	uint16_t used = itf->bLength;
	p += itf->bLength;
	uint8_t opened = 0;
	while (p < end && opened < itf->bNumEndpoints) {
		uint8_t blen = p[0], btype = p[1];
		if (btype == TUSB_DESC_ENDPOINT) {
			tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
			usbd_edpt_open(rhport, ep);
			if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN)
				g_xi[u].ep_in = ep->bEndpointAddress;
			else
				g_xi[u].ep_out = ep->bEndpointAddress;
			opened++;
		}
		used += blen;
		p += blen;
	}
	g_xi[u].in_use = true;
	if (g_xi[u].ep_out)  // arm OUT (rumble/LED)
		usbd_edpt_xfer(rhport, g_xi[u].ep_out, g_xi[u].out_buf,
			       sizeof(g_xi[u].out_buf));
	return used;
}
static bool xi_ctrl(uint8_t rhport, uint8_t stage,
		    tusb_control_request_t const *req)
{
	(void)rhport; (void)req;
	return stage != CONTROL_STAGE_SETUP;
}
static bool xi_xfer(uint8_t rhport, uint8_t ep, xfer_result_t res, uint32_t n)
{
	(void)res;
	for (int u = 0; u < PP_NSLOT; u++) {
		if (!g_xi[u].in_use || ep != g_xi[u].ep_out)
			continue;
		// Rumble packet: [00][08][00][big][small][00][00][00]; LED [01][03][led].
		if (n >= 5 && g_xi[u].out_buf[0] == 0x00 &&
		    g_xi[u].out_buf[1] == 0x08) {
			int slot = (u < USB_MOUNT_NSLOT) ? g_usb_to_bond[u] : -1;
			if (slot >= 0 && settings()->type[ET_XBOX].rumble)
				puck_rumble(slot,
					    (uint16_t)g_xi[u].out_buf[3] * 257u,
					    (uint16_t)g_xi[u].out_buf[4] * 257u);
		}
		usbd_edpt_xfer(rhport, g_xi[u].ep_out, g_xi[u].out_buf,
			       sizeof(g_xi[u].out_buf));
		break;
	}
	return true;
}
static const usbd_class_driver_t g_xi_driver = {
#if CFG_TUSB_DEBUG >= 2
	.name = "XINPUT",
#endif
	.init = xi_init,
	.deinit = xi_deinit,
	.reset = xi_reset,
	.open = xi_open,
	.control_xfer_cb = xi_ctrl,
	.xfer_cb = xi_xfer,
	.sof = NULL,
};
// TinyUSB calls this once at init to collect app class drivers. The XInput
// driver's open() only claims 0xFF/0x5D/0x01 interfaces (which exist only in
// MODE_XBOX), so registering it unconditionally is harmless in other modes.
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count)
{
	*count = 1;
	return &g_xi_driver;
}

// ---- report building -------------------------------------------------------
// Back-paddle / QAM button code → XInput bit (0=none 1=A..15=Dright).
static uint16_t code_to_xb(uint8_t c)
{
	switch (c) {
	case 1: return XB_A;
	case 2: return XB_B;
	case 3: return XB_X;
	case 4: return XB_Y;
	case 5: return XB_LB;
	case 6: return XB_RB;
	case 7: return XB_L3;
	case 8: return XB_R3;
	case 9: return XB_BACK;
	case 10: return XB_START;
	case 11: return XB_GUIDE;
	case 12: return XB_DUP;
	case 13: return XB_DDOWN;
	case 14: return XB_DLEFT;
	case 15: return XB_DRIGHT;
	default: return 0;
	}
}
static void xi_build(int slot, uint8_t *r)
{
	const pp_type_cfg_t *t = &settings()->type[ET_XBOX];
	uint32_t b = g_in[slot].buttons;
	uint16_t btn = 0;
	if (b & TB_DUP) btn |= XB_DUP;
	if (b & TB_DDN) btn |= XB_DDOWN;
	if (b & TB_DLF) btn |= XB_DLEFT;
	if (b & TB_DRT) btn |= XB_DRIGHT;
	if (b & TB_VIEW) btn |= XB_START;
	if (b & TB_MENU) btn |= XB_BACK;
	if (b & TB_STEAM) btn |= XB_GUIDE;
	if (b & TB_LB) btn |= XB_LB;
	if (b & TB_RB) btn |= XB_RB;
	if (b & TB_L3) btn |= XB_L3;
	if (b & TB_R3) btn |= XB_R3;
	uint16_t fA = t->ab_swap ? XB_B : XB_A, fB = t->ab_swap ? XB_A : XB_B;
	uint16_t fX = t->ab_swap ? XB_Y : XB_X, fY = t->ab_swap ? XB_X : XB_Y;
	if (b & TB_A) btn |= fA;
	if (b & TB_B) btn |= fB;
	if (b & TB_X) btn |= fX;
	if (b & TB_Y) btn |= fY;
	if (b & TB_L4) btn |= code_to_xb(t->back[0]);
	if (b & TB_R4) btn |= code_to_xb(t->back[1]);
	if (b & TB_L5) btn |= code_to_xb(t->back[2]);
	if (b & TB_R5) btn |= code_to_xb(t->back[3]);
	if (t->qam && (b & TB_QAM)) btn |= code_to_xb(t->qam);

	uint8_t lt = g_in[slot].lt, rt = g_in[slot].rt;
	if (b & TB_L2) lt = 0xFF;
	if (b & TB_R2) rt = 0xFF;
	// Trigger remaps: a back paddle / QAM mapped to code 19 (LT) / 20 (RT) pulls
	// that analog trigger to full.
	const uint8_t bc[4] = { t->back[0], t->back[1], t->back[2], t->back[3] };
	const uint32_t bm[4] = { TB_L4, TB_R4, TB_L5, TB_R5 };
	for (int i = 0; i < 4; i++) {
		if (!(b & bm[i])) continue;
		if (bc[i] == 19) lt = 0xFF;
		else if (bc[i] == 20) rt = 0xFF;
	}
	if (t->qam && (b & TB_QAM)) {
		if (t->qam == 19) lt = 0xFF;
		else if (t->qam == 20) rt = 0xFF;
	}

	int16_t lx = g_in[slot].lx, ly = g_in[slot].ly;
	int16_t rx = g_in[slot].rx, ry = g_in[slot].ry;
	r[0] = 0x00;
	r[1] = 0x14;
	r[2] = (uint8_t)(btn & 0xFF);
	r[3] = (uint8_t)(btn >> 8);
	r[4] = lt;
	r[5] = rt;
	r[6] = lx & 0xFF;  r[7] = (lx >> 8) & 0xFF;
	r[8] = ly & 0xFF;  r[9] = (ly >> 8) & 0xFF;
	r[10] = rx & 0xFF; r[11] = (rx >> 8) & 0xFF;
	r[12] = ry & 0xFF; r[13] = (ry >> 8) & 0xFF;
	memset(r + 14, 0, 6);
}

void xinput_task(void)
{
	if (!tud_mounted())
		return;
	static uint32_t last_ms;
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (now - last_ms < 4)  // ~250 Hz cap (per interface)
		return;
	last_ms = now;
	// Stream every mounted pad from its bond slot (usbSlot u → g_usb_to_bond[u]).
	for (int u = 0; u < PP_NSLOT; u++) {
		if (!g_xi[u].in_use || g_xi[u].ep_in == 0)
			continue;
		if (usbd_edpt_busy(0, g_xi[u].ep_in))
			continue;  // in flight — newest state next cycle
		int slot = (u < USB_MOUNT_NSLOT) ? g_usb_to_bond[u] : -1;
		if (slot < 0)
			continue;
		xi_build(slot, g_xi[u].in_buf);
		if (usbd_edpt_claim(0, g_xi[u].ep_in)) {
			if (!usbd_edpt_xfer(0, g_xi[u].ep_in, g_xi[u].in_buf, 20))
				usbd_edpt_release(0, g_xi[u].ep_in);
		}
	}
}
