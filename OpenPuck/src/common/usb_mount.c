// usb_mount.c — see usb_mount.h. Ported verbatim from OpenPuck's original policy;
// the platform-specific connected signal + re-enumerate are behind hooks so both
// firmwares get IDENTICAL mount/unmount behaviour.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "usb_mount.h"

#define NSLOT USB_MOUNT_NSLOT

uint8_t g_usb_mount_count = 0;
int8_t g_usb_to_bond[NSLOT];
int8_t g_bond_to_usb[NSLOT];

static bool s_dyn_on = false;
static uint8_t s_max_slots = NSLOT;

// The connected set must hold steady this long before we re-enumerate — absorbs
// RF blips and the staggered way several controllers connect at boot (one
// re-enumeration once the set settles, not one per controller).
#define MOUNT_DEBOUNCE_MS 2000u
// Lazy removal: a disconnected slot's interface is only dropped (and the device
// re-enumerated down) once NO controller has sent input for this long — i.e.
// during genuine downtime. Re-enumeration is risky mid-use, so we never shrink
// while anything is being played; dead slots linger as idle interfaces and are
// cleaned up when the rig goes quiet. Additions are NOT gated by this.
#define IDLE_CLEANUP_MS 60000u

static uint8_t mounted_mask(void)
{
	uint8_t m = 0;
	for (uint8_t u = 0; u < g_usb_mount_count; u++)
		if (g_usb_to_bond[u] >= 0)
			m |= (uint8_t)(1u << g_usb_to_bond[u]);
	return m;
}

// Order-preserving, budget-capped mount list for `mask`. Controllers already
// mounted keep their USB-slot order (a departure compacts survivors down but
// never reorders them); newly-connected controllers are appended (ascending bond
// order among simultaneous joiners). Does NOT mutate the live map.
static uint8_t compute_order(uint8_t mask, int8_t out[NSLOT])
{
	uint8_t n = 0;
	for (uint8_t u = 0; u < g_usb_mount_count && n < s_max_slots; u++) {
		int8_t b = g_usb_to_bond[u];
		if (b >= 0 && (mask & (1u << b)))
			out[n++] = b;
	}
	for (int s = 0; s < NSLOT && n < s_max_slots; s++) {
		if (!(mask & (1u << s)))
			continue;
		bool have = false;
		for (uint8_t i = 0; i < n; i++)
			if (out[i] == (int8_t)s) {
				have = true;
				break;
			}
		if (!have)
			out[n++] = (int8_t)s;
	}
	return n;
}
static uint8_t mask_of_list(const int8_t *list, uint8_t n)
{
	uint8_t m = 0;
	for (uint8_t i = 0; i < n; i++)
		if (list[i] >= 0)
			m |= (uint8_t)(1u << list[i]);
	return m;
}
static void commit_order(const int8_t *list, uint8_t n)
{
	for (int i = 0; i < NSLOT; i++) {
		g_usb_to_bond[i] = -1;
		g_bond_to_usb[i] = -1;
	}
	for (uint8_t u = 0; u < n; u++) {
		g_usb_to_bond[u] = list[u];
		g_bond_to_usb[list[u]] = (int8_t)u;
	}
	g_usb_mount_count = n;
}

void usb_mount_rebuild_map(void)
{
	int8_t tmp[NSLOT];
	uint8_t n = compute_order(usb_mount_connected_mask(), tmp);
	commit_order(tmp, n);
}

void usb_mount_enable(bool on, uint8_t max_slots)
{
	s_dyn_on = on;
	s_max_slots = max_slots ? (max_slots <= NSLOT ? max_slots : NSLOT) : 1;
	for (int i = 0; i < NSLOT; i++)
		g_usb_to_bond[i] = g_bond_to_usb[i] = -1;
	g_usb_mount_count = 0;
}

void usb_mount_task(void)
{
	if (!s_dyn_on)
		return;
	static uint8_t last_mask = 0xFF; // force first compare
	static uint32_t stable_since = 0;
	uint32_t now = usb_mount_now_ms();

	// ADD IMMEDIATELY, REMOVE LAZILY. We only ever SHRINK the interface set
	// during genuine downtime:
	//   - active (input within IDLE_CLEANUP_MS): target = connected UNION
	//     already-mounted → the set only grows; a dropped controller keeps its
	//     now-idle interface and a reconnect reuses it with NO re-enumeration.
	//   - idle (no input for a full minute): target = exactly the connected set →
	//     any lingering dead slot is dropped and we re-enumerate down, safely.
	uint32_t last_any = usb_mount_last_input_ms();
	bool idle = (last_any == 0) || (now - last_any) >= IDLE_CLEANUP_MS;
	uint8_t want =
		idle ? usb_mount_connected_mask() :
		       (uint8_t)(usb_mount_connected_mask() | mounted_mask());
	if (want != last_mask) { // set is moving → restart the stability timer
		last_mask = want;
		stable_since = now;
		return;
	}
	if ((now - stable_since) < MOUNT_DEBOUNCE_MS)
		return;
	int8_t tmp[NSLOT];
	uint8_t n = compute_order(want, tmp);
	if (mask_of_list(tmp, n) == mounted_mask())
		return; // same set already mounted (order preserved across joins/leaves)
	commit_order(tmp, n);
	usb_reenumerate(g_usb_mount_count);
}
