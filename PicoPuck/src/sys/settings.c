// settings.c — persisted device settings (see settings.h).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sys/settings.h"
#include "config/modes.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// MUST sit BELOW the BTstack bond bank on BOTH boards or a settings write erases
// the bonds (→ no pairing persistence, no reconnect). The SDK's pico_flash_bank
// occupies the top 2 sectors on RP2040 (Pico W) but SIZE-3*SECTOR .. SIZE-SECTOR
// on RP2350 (Pico 2 W: its A2 bootrom reserves the final sector). SIZE-4*SECTOR
// is clear of both. (History: this used to be -3*SECTOR, which collided with the
// Pico 2 W bond bank and wiped pairings on every mode switch / connect.)
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - 4 * FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC \
	0x50503105u // "PP" v5 (chord defaults → LIZARD/XBOX/SW_PRO)

typedef struct {
	uint32_t magic;
	pp_cfg_t cfg;
} stored_t;

static pp_cfg_t s_cfg;

static void defaults(pp_cfg_t *c)
{
	memset(c, 0, sizeof(*c));
	// All defaults MUST match OpenPuck's (config.cpp g_* / g_type[]) so the shared
	// WebUSB panel shows/sets identical values on both firmwares.
	c->mode = MODE_STEAM;
	c->m_div = 64; // mouse sensitivity (OpenPuck g_mDiv)
	c->m_fric = 94; // mouse friction   (OpenPuck g_mFric)
	c->rumble_scale =
		200; // OpenPuck g_rumbleScale (panel slider is 0..255)
	c->persist_mode = 0;
	// back4 + B/X/Y chord targets (A is always Steam). Matches OpenPuck's
	// g_chordBtn default so the same combo switches to the same mode on both.
	c->chord[0] = MODE_LIZARD; // B
	c->chord[1] = MODE_XBOX; // X
	c->chord[2] = MODE_SW_PRO; // Y
	c->sw_pro_rate = 2;
	c->sw_gyro10 = 10;
	// Per-type (ET_XBOX/ET_SWITCH/ET_DS4/ET_DS5) — mirrors OpenPuck g_type[]:
	//   back {5,6,7,8}, rumble on; Switch is Nintendo layout (ab_swap) with QAM→
	//   Capture(18) and NO trackpad haptics; the rest default trackpad haptics on.
	for (int et = 0; et < 4; et++) {
		c->type[et].back[0] = 5;
		c->type[et].back[1] = 6;
		c->type[et].back[2] = 7;
		c->type[et].back[3] = 8;
		c->type[et].rumble = 1;
		c->type[et].ab_swap = (et == 1) ? 1 : 0;
		c->type[et].qam = (et == 1) ? 18 : 0;
		c->type[et].pad_haptics = (et == 1) ? 0 : 1;
	}
}

void settings_load(void)
{
	const stored_t *f = (const stored_t *)(XIP_BASE + SETTINGS_OFFSET);
	if (f->magic == SETTINGS_MAGIC && f->cfg.mode <= MODE_MAX)
		s_cfg = f->cfg;
	else
		defaults(&s_cfg);
}

const pp_cfg_t *settings(void)
{
	return &s_cfg;
}

uint8_t settings_mode(void)
{
	return s_cfg.mode;
}

static void commit(void)
{
	stored_t st;
	st.magic = SETTINGS_MAGIC;
	st.cfg = s_cfg;
	uint8_t page[FLASH_PAGE_SIZE];
	memset(page, 0xFF, sizeof(page));
	memcpy(page, &st,
	       sizeof(st) < sizeof(page) ? sizeof(st) : sizeof(page));

	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
	restore_interrupts(ints);
}

void settings_set_mode(uint8_t mode)
{
	if (mode > MODE_MAX)
		return;
	s_cfg.mode = mode;
	commit();
}

void settings_set_field(uint8_t field, uint8_t val)
{
	// Per-type fields: 40 + et*9 + k (et 0..3, k 0..8) — mirrors OpenPuck.
	if (field >= 40 && field < 40 + 4 * 9) {
		int et = (field - 40) / 9, k = (field - 40) % 9;
		pp_type_cfg_t *t = &s_cfg.type[et];
		switch (k) {
		case 0:
		case 1:
		case 2:
		case 3:
			t->back[k] = val;
			break;
		case 4:
			t->qam = val;
			break;
		case 5:
			t->ab_swap = val ? 1 : 0;
			break;
		case 6:
			t->pad_haptics = val ? 1 : 0;
			break;
		case 7:
			t->led_bright = val > 100 ? 100 : val;
			break;
		case 8:
			t->rumble = val ? 1 : 0;
			break;
		}
		commit();
		return;
	}
	switch (field) {
	case 1:
		s_cfg.m_div = val < 4 ? 4 : val;
		break;
	case 2:
		s_cfg.m_fric = val > 99 ? 99 : val;
		break;
	case 16:
		s_cfg.persist_mode = val ? 1 : 0;
		break;
	case 17:
		if (val <= MODE_MAX)
			s_cfg.chord[0] = val;
		break;
	case 18:
		if (val <= MODE_MAX)
			s_cfg.chord[1] = val;
		break;
	case 19:
		if (val <= MODE_MAX)
			s_cfg.chord[2] = val;
		break;
	case 22:
		s_cfg.rumble_scale = val;
		break;
	case 23:
		s_cfg.sw_pro_rate = val <= 2 ? val : 2;
		break;
	case 24:
		s_cfg.sw_gyro10 = (val >= 5 && val <= 30) ? val : 10;
		break;
	case 29:
		s_cfg.land_all_87 = val ? 1 : 0;
		break;
	default:
		return; // unknown/ignored field — don't rewrite flash
	}
	commit();
}

void settings_factory_reset(void)
{
	defaults(&s_cfg);
	commit();
}

// ---- bond label table ------------------------------------------------------
static int bond_find(const uint8_t addr[6])
{
	for (int i = 0; i < 4; i++)
		if (s_cfg.bond[i].used &&
		    memcmp(s_cfg.bond[i].addr, addr, 6) == 0)
			return i;
	return -1;
}

void settings_bond_remember(const uint8_t addr[6], uint8_t addr_type,
			    uint8_t kind, const char name[16])
{
	int i = bond_find(addr);
	if (i < 0)
		for (int k = 0; k < 4; k++)
			if (!s_cfg.bond[k].used) {
				i = k;
				break;
			}
	if (i < 0)
		i = 0; // table full (≤4 bonds): overwrite the first entry
	pp_bond_meta_t *b = &s_cfg.bond[i];
	// Skip the flash write if nothing changed (avoids a wear cycle on every
	// reconnect, since route_connection calls this each time a pad links).
	if (b->used && b->addr_type == addr_type && b->kind == kind &&
	    memcmp(b->addr, addr, 6) == 0 && memcmp(b->name, name, 16) == 0)
		return;
	b->used = 1;
	memcpy(b->addr, addr, 6);
	b->addr_type = addr_type;
	b->kind = kind;
	memcpy(b->name, name, 16);
	commit();
}

bool settings_bond_lookup(const uint8_t addr[6], uint8_t *kind, char name[16])
{
	int i = bond_find(addr);
	if (i < 0)
		return false;
	if (kind)
		*kind = s_cfg.bond[i].kind;
	if (name)
		memcpy(name, s_cfg.bond[i].name, 16);
	return true;
}

void settings_bond_forget(const uint8_t addr[6])
{
	int i = bond_find(addr);
	if (i < 0)
		return;
	memset(&s_cfg.bond[i], 0, sizeof(s_cfg.bond[i]));
	commit();
}
