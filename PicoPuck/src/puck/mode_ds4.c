// mode_ds4.c — DualShock 4 / HID-gyro emulated controller (ported from OpenPuck
// mode_hidgyro.cpp). Report id 0x01 (63B) + gyro/accel + split trackpad; feature
// reports for motion calibration / MAC / fw; rumble on output report 0x05.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "puck/relay.h"
#include "sys/settings.h"
#include <string.h>
#include "hid_reports.h"
#include "report_build.h"

#define ET_DS4 2 // per-type config index (matches OpenPuck ET_DS4)

// The DS4 report layout lives in the shared builder (common/report_build.c); here
// we only fill the remap config from settings and hold the per-slot seq state.
static report_cfg_t ds4_cfg(void)
{
	const pp_type_cfg_t *t = &settings()->type[ET_DS4];
	report_cfg_t c = { .ab_swap = t->ab_swap,
			   .back = { t->back[0], t->back[1], t->back[2],
				     t->back[3] },
			   .qam = t->qam };
	return c;
}

static uint16_t ds4_build(int slot, uint8_t *out, uint8_t *rid)
{
	static report_seq_t seq[PP_NSLOT];
	*rid = 0x01;
	report_cfg_t cfg = ds4_cfg();
	return build_ds4(&g_in[slot], &cfg, &seq[slot], out);
}

static uint16_t ds4_get(int slot, uint8_t rid, uint8_t type, uint8_t *buf,
			uint16_t reqlen)
{
	if (type != PP_HID_FEATURE || reqlen == 0)
		return 0;
	memset(buf, 0, reqlen);
	uint8_t mac[6] = {
		0x00, 0x1B, 0xDC, 0x4F, 0x55, (uint8_t)(0x50 + slot)
	};
	switch (rid) {
	case 0x02:
		if (reqlen < 36)
			return 0;
		ps_neutral_calib(buf);
		return 36;
	case 0x12:
		if (reqlen < 15)
			return 0;
		memcpy(buf, mac, 6);
		return 15;
	case 0x81:
		if (reqlen < 6)
			return 0;
		memcpy(buf, mac, 6);
		return 6;
	case 0xA3:
		if (reqlen < 48)
			return 0;
		buf[0] = 0x01;
		return 48;
	default:
		return 0;
	}
}

static void ds4_set(int slot, uint8_t rid, uint8_t type, const uint8_t *b,
		    uint16_t n)
{
	if (type != PP_HID_OUTPUT)
		return;
	// OUT endpoint delivers rid=0 with the id in b[0]; control SET_REPORT splits
	// the id into rid. Normalise to p = bytes AFTER the id (effects @ [3]/[4]).
	const uint8_t *p;
	uint16_t pn;
	if (rid == 0) {
		if (n < 1 || b[0] != 0x05)
			return;
		p = b + 1;
		pn = (uint16_t)(n - 1);
	} else if (rid == 0x05) {
		p = b;
		pn = n;
	} else {
		return;
	}
	if (pn < 5)
		return;
	puck_rumble(slot, (uint16_t)p[4] * 257u, (uint16_t)p[3] * 257u);
}

const emu_mode_t emu_ds4 = {
	.vid = 0x054C,
	.pid = 0x05C4,
	.bcd = 0x0120,
	.product = "Wireless Controller",
	.report_desc = GYRO_HID_DESC,
	.report_desc_len = sizeof(GYRO_HID_DESC),
	.build = ds4_build,
	.get_report = ds4_get,
	.set_report = ds4_set,
	.poll_ms = 4,
};
