// usb_mount.h -- OpenPuck adapter over the SHARED dynamic-mount policy
// (src/common/usb_mount.{h,c}, linked by both OpenPuck and PicoPuck so the
// mount/unmount behaviour is identical). The map + debounce + add-now/remove-lazy
// logic lives in the shared file; this header keeps OpenPuck's camelCase call
// sites unchanged, and OpenPuck provides the platform hooks (connected mask /
// last-input / now in usb_mount.cpp; usbReenumerate in OpenPuck.ino).
#pragma once
#include <stdint.h>
#include "bonds.h" // NSLOT
#include "src/common/usb_mount.h"

// camelCase compatibility aliases -> shared snake_case symbols.
#define g_usbMountCount g_usb_mount_count
#define g_usbToBond g_usb_to_bond
#define g_bondToUsb g_bond_to_usb
#define usbMountEnable usb_mount_enable
#define usbMountRebuildMap usb_mount_rebuild_map
#define usbMountTask usb_mount_task
// OpenPuck.ino defines the re-enumerate hook under its historical name.
#define usbReenumerate usb_reenumerate

// Persist `mode` and reboot into it, cleanly detaching USB first so the host
// tears down the outgoing personality (releasing any held input). Does not
// return. OpenPuck-specific (defined in OpenPuck.ino); PicoPuck has its own.
void modeSwitchReboot(uint8_t mode);
