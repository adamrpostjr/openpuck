// puck_desc.c — PUCK_HID_DESC now lives in the shared source of truth
// (OpenPuck/src/common/hid_reports.c), linked by both firmwares. This file only
// provides PicoPuck's convenience length + the size static-assert (see .h).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "usb/puck_desc.h"
#include "hid_reports.h"
#include "tusb.h"

const size_t PUCK_HID_DESC_LEN = sizeof(PUCK_HID_DESC);

TU_VERIFY_STATIC(sizeof(PUCK_HID_DESC) == PUCK_HID_DESC_SIZE,
		 "PUCK_HID_DESC_SIZE mismatch");
