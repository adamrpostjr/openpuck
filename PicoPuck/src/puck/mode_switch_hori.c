// mode_switch_hori.c — Switch HORIPAD emulated controller (ported from OpenPuck).
//
// 8-byte report, no report id: [btn_lo][btn_hi][hat][LX][LY][RX][RY][vendor].
// Nintendo face layout (A↔B / X↔Y swapped). No rumble/features.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/emu.h"
#include "sys/settings.h"
#include "hid_reports.h"
#include "report_build.h"

#define ET_SWITCH 1 // per-type config index (matches OpenPuck ET_SWITCH)

// Report layout is in the shared builder (common/report_build.c). This also
// brings PicoPuck's HORIPAD up to OpenPuck parity: back-paddle / QAM remap,
// hat-from-paddle, and configurable A/B+X/Y swap.
static uint16_t hori_build(int slot, uint8_t *out, uint8_t *rid)
{
	*rid = 0;
	const pp_type_cfg_t *t = &settings()->type[ET_SWITCH];
	report_cfg_t cfg = { t->ab_swap,
			     { t->back[0], t->back[1], t->back[2], t->back[3] },
			     t->qam };
	return build_switch_hori(&g_in[slot], &cfg, out);
}

const emu_mode_t emu_switch_hori = {
	.vid = 0x0F0D,
	.pid = 0x0092,
	.bcd = 0x0210,
	.product = "POKKEN CONTROLLER",
	.report_desc = SWITCH_HID_DESC,
	.report_desc_len = sizeof(SWITCH_HID_DESC),
	.build = hori_build,
	.get_report = NULL,
	.set_report = NULL,
	.poll_ms = 4,
};
