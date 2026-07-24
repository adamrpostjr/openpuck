// chord.c — button chord → USB mode switch (see chord.h).
//
// Checked once per freshly-decoded BT report (raw g_in). The guard is a
// deliberate 5-button combination (both triggers + both bumpers, or the four
// back paddles, plus a face button), so it fires immediately on detection — no
// hold. On-change pads (Xbox) therefore switch on the single press report.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/chord.h"
#include "puck/triton.h"
#include "config/modes.h"
#include "config/picopuck_config.h"
#include "sys/settings.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

// Face → target mode (A always Steam). Mirrors OpenPuck's chordBtn idea, but the
// defaults point at PicoPuck's implemented emulated modes.
#define CHORD_B MODE_PS5
#define CHORD_X MODE_HIDGYRO
#define CHORD_Y MODE_SW_HORI

static void mode_switch_reboot(uint8_t mode)
{
	settings_set_mode(mode);
	watchdog_reboot(0, 0, 1);
	while (1) {
	}
}

void chord_note(int slot)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	uint32_t b = g_in[slot].buttons;
	const uint32_t back4 = TB_L4 | TB_R4 | TB_L5 | TB_R5;
	const uint32_t trig = TB_L2 | TB_R2 | TB_LB | TB_RB;
	// Either guard works for any controller: the four back paddles (SC2) OR
	// L2+R2+LB+RB (every pad has these).
	if (((b & back4) != back4) && ((b & trig) != trig))
		return;

	uint8_t want = 0xFF;
	if (b & TB_A) want = MODE_STEAM;
	else if (b & TB_B) want = CHORD_B;
	else if (b & TB_X) want = CHORD_X;
	else if (b & TB_Y) want = CHORD_Y;

	if (want == 0xFF || want == settings_mode())
		return;  // guard held but no (new) target picked → pass through

	mode_switch_reboot(want);  // deliberate combo → switch immediately
}
