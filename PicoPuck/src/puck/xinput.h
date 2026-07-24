// xinput.h — Xbox 360 wired (XInput) personality (MODE_XBOX).
//
// Xbox 360 pads are NOT HID: they expose a vendor interface (class 0xFF /
// subclass 0x5D / protocol 0x01) carrying a 20-byte input report, served by a
// custom TinyUSB class driver. PicoPuck presents ONE XInput interface for the
// active controller, alongside the WebUSB vendor interface (so the panel keeps
// working). The OUT endpoint carries rumble, relayed to the controller.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_XINPUT_H
#define PICOPUCK_XINPUT_H

#include <stdint.h>

// Stream the active controller's 20-byte XInput report (call from the main loop
// in MODE_XBOX). No-op when the interface isn't open / the IN endpoint is busy.
void xinput_task(void);

#endif // PICOPUCK_XINPUT_H
