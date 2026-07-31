// usb_mount.h — dynamic USB mounting of ACTIVELY-CONNECTED controllers in the
// emulated modes, shared by OpenPuck and PicoPuck so the mount/unmount BEHAVIOUR
// is identical: present one HID gamepad per CONNECTED controller (not per bonded
// slot), re-enumerating with NO reboot as that set changes.
//
// The k connected controllers are shown as a DENSE PREFIX usbSlot 0..k-1; usbSlot
// u is fed from bond slot g_usb_to_bond[u]. USB can't hold an interface "gap", so
// the prefix is compacted → player identity follows CONNECTION ORDER. Re-enum is
// debounced (RF blips / staggered boot joins) and shrinks only during idle
// (add-now, remove-lazy) so we never tear down USB mid-play.
//
// Transport/USB-framework specifics live behind four platform hooks each firmware
// implements (connected mask, last-input time, now, and the actual re-enumerate).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_USB_MOUNT_H
#define OPK_COMMON_USB_MOUNT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef USB_MOUNT_NSLOT
#define USB_MOUNT_NSLOT 4
#endif

// Active mount map. usbSlot (0..g_usb_mount_count-1) → bond slot, and the inverse
// for output-report routing.
extern uint8_t g_usb_mount_count;
extern int8_t g_usb_to_bond[USB_MOUNT_NSLOT]; // usbSlot → bondSlot (-1 unused)
extern int8_t g_bond_to_usb[USB_MOUNT_NSLOT]; // bondSlot → usbSlot (-1 unmounted)

// Enable the dynamic watcher for this boot (emulated modes call this; puck/lizard
// leave it off). max_slots caps the mounted count to the mode's interface budget.
void usb_mount_enable(bool on, uint8_t max_slots);

// Rebuild the map from the CURRENT connected set (bond-slot order). Called by the
// boot enumerate and on every accepted change.
void usb_mount_rebuild_map(void);

// Per-loop watcher: on a debounced change of the connected set, commit the new
// map and call usb_reenumerate(). No-op unless enabled.
void usb_mount_task(void);

// ---- platform hooks (provided by each firmware) ----------------------------
// Bitmask (bit s) of slots that are currently mount-worthy (connected).
uint8_t usb_mount_connected_mask(void);
// millis() of the most-recent input across all slots (0 = never). Drives the
// idle-cleanup (remove-lazy) branch.
uint32_t usb_mount_last_input_ms(void);
// Monotonic milliseconds.
uint32_t usb_mount_now_ms(void);
// Tear down + rebuild the USB config presenting `k` gamepad interfaces (fixed
// WebUSB / wake interfaces replayed too), then re-attach. NO MCU reboot. The map
// is already committed for k connected controllers when this is called.
void usb_reenumerate(uint8_t k);

#ifdef __cplusplus
}
#endif

#endif // OPK_COMMON_USB_MOUNT_H
