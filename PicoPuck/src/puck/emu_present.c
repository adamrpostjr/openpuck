// emu_present.c — drive the active emulated controller from g_in (see .h).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu_present.h"
#include "puck/emu.h"
#include "puck/slots.h"
#include "usb/usb_tx.h"
#include "config/modes.h"
#include "sys/settings.h"

#include "pico/time.h"

#define EMU_STREAM_MS 4u  // ~250 Hz, matching OpenPuck's USB_STREAM_MS

static uint32_t s_last_ms;

// First connected slot, or 0 (present a neutral controller when nothing is on).
static int active_slot(void)
{
	for (int s = 0; s < PP_NSLOT; s++)
		if (g_slot[s].connected)
			return s;
	return 0;
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

	uint8_t body[64];
	uint8_t rid = 0;
	uint16_t n = e->build(active_slot(), body, &rid);
	if (n)
		usb_tx_hid(0, rid, body, n);
}

uint16_t emu_get_report(uint8_t report_id, uint8_t type, uint8_t *buf,
			uint16_t reqlen)
{
	const emu_mode_t *e = emu_mode_for(settings_mode());
	if (e && e->get_report)
		return e->get_report(active_slot(), report_id, type, buf, reqlen);
	return 0;
}

void emu_set_report(uint8_t report_id, uint8_t type, const uint8_t *buf,
		    uint16_t len)
{
	const emu_mode_t *e = emu_mode_for(settings_mode());
	if (e && e->set_report)
		e->set_report(active_slot(), report_id, type, buf, len);
}
