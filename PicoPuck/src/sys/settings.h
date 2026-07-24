// settings.h — persisted device settings (mirrors OpenPuck's Cfg: mode + the
// generic tunables the WebUSB panel reads/writes, so the common panel cards work
// identically). Stored in a flash sector below the BTstack bond bank.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_SETTINGS_H
#define PICOPUCK_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

// Per-emulated-type config (Xbox/Switch/DS4/DS5), mirroring OpenPuck's TypeCfg.
typedef struct {
	uint8_t back[4];   // back-paddle L4/R4/L5/R5 → button code
	uint8_t qam;       // QAM (…) → button code
	uint8_t ab_swap;   // A/B + X/Y swap
	uint8_t pad_haptics;
	uint8_t led_bright;
	uint8_t rumble;    // per-type rumble enable
} pp_type_cfg_t;

// Persisted per-bond metadata, so a paired controller that is currently offline
// (e.g. right after a reboot / mode switch) still shows its real name and kind in
// the panel instead of a generic placeholder. Keyed by the bond identity address.
// The Bluetooth stack persists the bond itself (keys) in its own flash bank; this
// only carries the human-facing label the stack doesn't keep.
typedef struct {
	uint8_t used;
	uint8_t addr[6];
	uint8_t addr_type;
	uint8_t kind;       // 0 unknown, 1 SC2, 2 BLE pad, 3 Classic pad
	char name[16];
} pp_bond_meta_t;

typedef struct {
	uint8_t mode;             // active USB mode (MODE_*)
	uint8_t m_div, m_fric;    // mouse sensitivity / friction
	uint8_t rumble_scale;     // rumble strength % (100 = 1×)
	uint8_t persist_mode;     // remember last mode across reboots
	uint8_t chord[3];         // B/X/Y chord target modes
	uint8_t sw_pro_rate;      // Switch Pro report rate 0/1/2
	uint8_t sw_gyro10;        // Switch gyro scale ×10
	uint8_t land_all_87;
	pp_type_cfg_t type[4];    // ET_XBOX, ET_SWITCH, ET_DS4, ET_DS5
	pp_bond_meta_t bond[4];   // labels for the (≤4) persisted bonds
} pp_cfg_t;

void settings_load(void);
const pp_cfg_t *settings(void);  // read-only view

uint8_t settings_mode(void);
void settings_set_mode(uint8_t mode);      // persist mode (caller reboots)
void settings_set_field(uint8_t field, uint8_t val);  // WebUSB 0x02 set-field
void settings_factory_reset(void);

// Bond label persistence (see pp_bond_meta_t). remember() upserts by address;
// lookup() fills kind/name for a known address (returns false if unknown);
// forget() drops one address. Each commits to flash.
void settings_bond_remember(const uint8_t addr[6], uint8_t addr_type,
			    uint8_t kind, const char name[16]);
bool settings_bond_lookup(const uint8_t addr[6], uint8_t *kind, char name[16]);
void settings_bond_forget(const uint8_t addr[6]);

#endif // PICOPUCK_SETTINGS_H
