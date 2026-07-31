// usb_modes.h — USB presentation mode numbers, shared by OpenPuck and PicoPuck.
// These ARE the WebUSB protocol's mode ids (the panel sends mode-switch opcodes
// and reads the active mode as these values), so they MUST stay identical between
// the two firmwares — a mismatch would mis-switch modes. RF/BT transport is the
// same across all; only USB enumeration + report mapping differ per mode.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_USB_MODES_H
#define OPK_COMMON_USB_MODES_H

#define MODE_STEAM 0 // Valve puck (auto-lizard when Steam closed)
#define MODE_XBOX 1 // Xbox 360 XInput
#define MODE_SW_HORI 2 // Switch HORIPAD
#define MODE_LIZARD 3 // Puck HID, always keyboard+mouse
#define MODE_SW_PRO 4 // Nintendo Switch Pro Controller + gyro
#define MODE_PS5 5 // Sony DualSense + gyro + split trackpad
#define MODE_HIDGYRO 6 // DS4-layout generic HID gamepad + gyro
#define MODE_PS5_GAME 7 // DualSense, clean single-HID
#define MODE_DS4_GAME 8 // DS4, clean single-HID
#define MODE_PS3 9 // DualShock 3 / Sixaxis
#define MODE_MAX 9

#endif // OPK_COMMON_USB_MODES_H
