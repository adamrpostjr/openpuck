// bt_hogp.h — manual HID-over-GATT client for standard BLE gamepads (Xbox, …).
//
// BTstack's hids_client auto-discovers and subscribes, but in practice it does
// not deliver notifications from Xbox controllers (observed: bt0). joypad-os hit
// the same wall and hand-rolled GATT discovery; our Valve GATT client (bt_valve)
// proves that manual discovery works reliably here. This is the same pattern for
// the standard HID service: discover 0x1812, enable the input Report
// characteristic's notifications, and route each notification to the slot's
// input driver.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_BT_HOGP_H
#define PICOPUCK_BT_HOGP_H

#include <stdint.h>
#include "btstack.h"

void hogp_start(int slot, hci_con_handle_t handle);
void hogp_disconnected(hci_con_handle_t handle);

// Write an output report (e.g. rumble) to the pad's HID output characteristic.
void hogp_send_output(int slot, const uint8_t *data, uint16_t len);

// Periodic Battery Service poll for connected HOGP pads. Call each loop.
void hogp_periodic(void);

#endif // PICOPUCK_BT_HOGP_H
