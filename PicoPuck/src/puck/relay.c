// relay.c — host→controller relay dispatch (see relay.h).
//
// The actual routing lives in the BT host (bt_relay), which knows what is bound
// to each slot: a generic pad's rumble output, or an SC2's Valve report
// characteristic. bt_relay is weak so a BT-less build still links.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/relay.h"

// Diagnostics: how many host→controller relays we've seen, and the last report
// id. Lets the panel confirm Steam is actually sending haptics/rumble.
volatile uint16_t g_relay_count;
volatile uint8_t g_relay_last_id;

__attribute__((weak)) void bt_relay(int slot, uint8_t report_id,
				    const uint8_t *body, uint16_t len)
{
	(void)slot;
	(void)report_id;
	(void)body;
	(void)len;
}

void relay_enqueue(int slot, uint8_t report_id, const uint8_t *body, uint16_t len)
{
	g_relay_count++;
	g_relay_last_id = report_id;
	bt_relay(slot, report_id, body, len);
}

void puck_rumble(int slot, uint16_t lo, uint16_t hi)
{
	// ID_OUT_REPORT_HAPTIC_RUMBLE (0x80) body — matches SDL's triton driver
	// exactly: MsgHapticRumble { u8 type=0, u16 intensity=0, {u16 speed, i8
	// gain}, {u16 speed, i8 gain} }. Magnitude rides left/right.speed (= the
	// low/high-freq rumble); type/intensity/gain stay 0. (We previously set
	// type=0x04 and intensity=max, which the SC2 rejects.)
	uint8_t p[9];
	p[0] = 0;                    // type
	p[1] = 0;                    // intensity lo
	p[2] = 0;                    // intensity hi
	p[3] = (uint8_t)(lo & 0xFF); // left.speed
	p[4] = (uint8_t)(lo >> 8);
	p[5] = 0;                    // left.gain (dB)
	p[6] = (uint8_t)(hi & 0xFF); // right.speed
	p[7] = (uint8_t)(hi >> 8);
	p[8] = 0;                    // right.gain (dB)
	relay_enqueue(slot, 0x80, p, 9);
}
