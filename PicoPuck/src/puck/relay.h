// relay.h — host→controller relay seam.
//
// The personality hands Steam's actuator/config writes (haptics 0x80-0x86,
// settings 0x87, power-off 0x9F, and feature-id-1 passthrough) to relay_enqueue.
// Where they go depends on what is bound to the slot:
//   - Phase 2: a generic BLE/Classic pad → mapped to the pad's rumble output.
//   - Phase 4: a Steam Controller 2 → GATT-written verbatim to the Valve report
//     characteristic (transparent forwarding).
// In Phase 1 there is nothing bound, so this is a no-op.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_RELAY_H
#define PICOPUCK_RELAY_H

#include <stdint.h>

#include <stdint.h>

// Diagnostics (read by the WebUSB panel): total relays and the last report id.
extern volatile uint16_t g_relay_count;
extern volatile uint8_t g_relay_last_id;

// Build the SC2 0x80 rumble report from 16-bit motor magnitudes and relay it to
// the controller bound to `slot` (OpenPuck's hapticSteamRumble). Emulated modes
// call this from their host output-report handlers.
void puck_rumble(int slot, uint16_t lo, uint16_t hi);

// Forward the HID report [report_id][body...] to the controller bound to `slot`
// (SC2: written verbatim to the Valve report characteristic; generic pad: 0x80
// rumble mapped to the pad's output). No length byte is re-inserted — pass the
// exact bytes that should land on the controller after the report-id.
void relay_enqueue(int slot, uint8_t report_id, const uint8_t *body, uint16_t len);

#endif // PICOPUCK_RELAY_H
