// personality.h — the puck HID personality (feature command channel + status).
//
// Implements the TinyUSB HID get/set report callbacks (one instance per slot)
// and a periodic task that emits the 0x79 connection-state edges and the
// synthesized 0x43 battery report Steam reads. This is the port of OpenPuck's
// SteamPuckController.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_PERSONALITY_H
#define PICOPUCK_PERSONALITY_H

#include <stdbool.h>
#include <stdint.h>

void puck_personality_init(void);

// Periodic: connection-state (0x79) edges + battery (0x43). Call each loop.
void puck_personality_task(void);

// True while Steam is actively driving (has written to the command channel
// recently); used to decide whether we configure a controller ourselves.
bool puck_steam_active(void);

// Present a controller's input on `slot`. _synth serialises g_in[slot] into a
// limited report 0x45 (generic pads); _raw forwards an SC2's on-air report
// verbatim (rep[0] = report id). Both mark the slot live.
void puck_present_synth(int slot);
void puck_present_raw(int slot, const uint8_t *rep, uint8_t len);

// Mark a slot connected/disconnected (BT link up/down).
void puck_set_connected(int slot, bool connected);

// Diagnostics: per-slot feature GET/SET counts + last SET command byte, so the
// panel can show whether Steam is polling/pairing each slot's interface.
void puck_slot_io(int slot, uint8_t *get_n, uint8_t *set_n, uint8_t *last_set_cmd);

// Give a slot a synthetic bond record with a UNIQUE serial derived from the
// controller's BT address, so Steam mounts each connected controller as a
// distinct device (0xA3 bond read + 0xAE serial read return this identity).
void puck_set_bond(int slot, const uint8_t addr[6], uint8_t kind);

#endif // PICOPUCK_PERSONALITY_H
