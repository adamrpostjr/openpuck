// bt_valve.h — Steam Controller 2 native-BLE client (Valve proprietary GATT).
//
// The SC2 in Bluetooth mode does NOT use HID-over-GATT. It exposes Valve's
// custom service 100F6C32-…: an input-notify characteristic carrying report
// 0x45 (or the 0x47 variant) and a writable "report" characteristic for feature
// reports. This client discovers the service, subscribes to input (forwarded
// verbatim to the puck slot for transparent Steam operation), keeps the pad in
// gamepad mode (lizard-off keepalive), and writes Steam's relayed commands
// straight through. Reference: SDL SD_hidapi_steam_triton.c and joypad-os.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_BT_VALVE_H
#define PICOPUCK_BT_VALVE_H

#include <stdint.h>
#include <stdbool.h>
#include "btstack.h"

// Diagnostic (read by the WebUSB panel): GATT feature writes issued to SC2s.
extern volatile uint16_t g_valve_writes;

// Begin Valve GATT discovery for a freshly-secured SC2 on `slot`.
void valve_start(int slot, hci_con_handle_t handle);

// Tear down the client for a disconnected handle.
void valve_disconnected(hci_con_handle_t handle);

// Periodic: resend the lizard-off keepalive (~2 s) and enable IMU once. Call each loop.
void valve_periodic(void);

// Forward the HID report [report_id][body] verbatim to the SC2's report
// characteristic (queued; drained one write at a time).
void valve_feature_write(int slot, uint8_t report_id, const uint8_t *body, uint16_t len);

#endif // PICOPUCK_BT_VALVE_H
