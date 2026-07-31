// emu_present.c — drive the active emulated controller(s) from g_in (see .h).
//
// Dynamic mounting (shared common/usb_mount.c): the host is presented one HID
// gamepad per CONNECTED controller. usbSlot u (interface u) is fed from bond slot
// g_usb_to_bond[u]; the report callbacks route the same way. This file also
// supplies the mounter's platform hooks (connected mask / last-input / now) — the
// re-enumerate hook lives in usb_descriptors.c.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu_present.h"
#include "puck/emu.h"
#include "puck/slots.h"
#include "usb/usb_tx.h"
#include "usb/usb_descriptors.h"
#include "config/modes.h"
#include "config/picopuck_config.h"
#include "sys/settings.h"
#include "usb_mount.h"

#include "pico/time.h"

_Static_assert(PP_NSLOT == USB_MOUNT_NSLOT,
	       "usb_mount slot count must match PP_NSLOT");

#define EMU_STREAM_MS 4u // ~250 Hz, matching OpenPuck's USB_STREAM_MS

static uint32_t s_last_ms;

// usbSlot u → bond slot (the u-th connected controller), or -1 if not mounted.
static int mounted_slot(uint8_t u)
{
	return (u < USB_MOUNT_NSLOT) ? g_usb_to_bond[u] : -1;
}

void emu_present_task(void)
{
	const emu_mode_t *e = emu_mode_for(settings_mode());
	if (!e)
		return;
	uint32_t t = to_ms_since_boot(get_absolute_time());
	if (t - s_last_ms < EMU_STREAM_MS)
		return;
	s_last_ms = t;

	for (uint8_t u = 0; u < g_usb_mount_count; u++) {
		int slot = mounted_slot(u);
		if (slot < 0)
			continue;
		uint8_t body[64];
		uint8_t rid = 0;
		uint16_t n = e->build(slot, body, &rid);
		if (n)
			usb_tx_hid(u, rid, body, n);
	}
}

uint16_t emu_get_report(uint8_t instance, uint8_t report_id, uint8_t type,
			uint8_t *buf, uint16_t reqlen)
{
	const emu_mode_t *e = emu_mode_for(settings_mode());
	int slot = mounted_slot(instance);
	if (e && e->get_report && slot >= 0)
		return e->get_report(slot, report_id, type, buf, reqlen);
	return 0;
}

void emu_set_report(uint8_t instance, uint8_t report_id, uint8_t type,
		    const uint8_t *buf, uint16_t len)
{
	const emu_mode_t *e = emu_mode_for(settings_mode());
	int slot = mounted_slot(instance);
	if (e && e->set_report && slot >= 0)
		e->set_report(slot, report_id, type, buf, len);
}

// ---- shared dynamic-mount platform hooks -----------------------------------
uint8_t usb_mount_connected_mask(void)
{
	uint8_t m = 0;
	for (int s = 0; s < PP_NSLOT; s++)
		if (g_slot[s].connected)
			m |= (uint8_t)(1u << s);
	return m;
}

uint32_t usb_mount_last_input_ms(void)
{
	uint32_t last = 0;
	for (int s = 0; s < PP_NSLOT; s++)
		if (g_slot[s].conn_reply_ms > last)
			last = g_slot[s].conn_reply_ms;
	return last;
}

uint32_t usb_mount_now_ms(void)
{
	return to_ms_since_boot(get_absolute_time());
}
