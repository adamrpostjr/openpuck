// chord.c — button chord → USB mode switch (see chord.h).
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

#define CHORD_HOLD 12  // consecutive matching reports before switching

static uint8_t s_cnt[PP_NSLOT];
static uint8_t s_want[PP_NSLOT];

static void mode_switch_reboot(uint8_t mode)
{
	settings_set_mode(mode);
	watchdog_reboot(0, 0, 1);
	while (1) {
	}
}

void chord_check(int slot, bool is_sc2)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	uint32_t b = g_in[slot].buttons;
	uint32_t guard = is_sc2 ? (TB_R4 | TB_L4 | TB_R5 | TB_L5)
				: (TB_L2 | TB_R2 | TB_LB | TB_RB);

	if ((b & guard) != guard) {
		s_cnt[slot] = 0;
		return;
	}

	// Guard held → a face button selects the target.
	uint8_t want = 0xFF;
	if (b & TB_A)
		want = MODE_STEAM;
	else if (b & TB_B)
		want = CHORD_B;
	else if (b & TB_X)
		want = CHORD_X;
	else if (b & TB_Y)
		want = CHORD_Y;

	// Don't leak the chord to the host.
	g_in[slot].buttons &= ~(guard | TB_A | TB_B | TB_X | TB_Y);

	if (want == 0xFF || want == settings_mode()) {
		s_cnt[slot] = 0;
		return;
	}
	if (want != s_want[slot]) {
		s_want[slot] = want;
		s_cnt[slot] = 1;
		return;
	}
	if (++s_cnt[slot] >= CHORD_HOLD)
		mode_switch_reboot(want);
}
