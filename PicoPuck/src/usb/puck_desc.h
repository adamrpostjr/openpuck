// puck_desc.h — the cloned Valve puck HID report descriptor.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_PUCK_DESC_H
#define PICOPUCK_PUCK_DESC_H

#include <stdint.h>
#include <stddef.h>
#include "hid_reports.h" // PUCK_HID_DESC lives in the shared source of truth

// PUCK_HID_DESC (the byte-for-byte Valve puck descriptor) is defined in
// OpenPuck/src/common/hid_reports.c and declared in hid_reports.h — shared by
// both firmwares. Compile-time size, needed by the config-descriptor macros;
// verified against the actual array with a static assert in puck_desc.c.
#define PUCK_HID_DESC_SIZE 372

extern const size_t PUCK_HID_DESC_LEN;

#endif // PICOPUCK_PUCK_DESC_H
