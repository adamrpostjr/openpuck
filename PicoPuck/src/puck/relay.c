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
