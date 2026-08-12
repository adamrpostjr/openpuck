// pplog.h — tiny in-RAM diagnostic log ring, surfaced over WebUSB.
//
// PicoPuck's stdio goes to UART only (the USB port is the puck's HID identity),
// so when no serial adapter is attached these lines would be lost. pplog() keeps
// the most-recent bytes in RAM and send_bt_frame() appends a snapshot to the
// 0xAD pairing frame the panel already polls — so the log shows up in the panel.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PP_SYS_PPLOG_H
#define PP_SYS_PPLOG_H

#include <stdint.h>

// Append a NUL-terminated string (include your own '\n'). Oldest bytes are
// dropped when the ring fills. Safe to call from any context; best-effort.
void pplog(const char *s);

// Copy up to `max` most-recent bytes (chronological order) into out[]; returns
// the number copied.
uint16_t pplog_snapshot(uint8_t *out, uint16_t max);

// A SEPARATE, non-rolling buffer for the one-shot GATT characteristic dump, so a
// burst of haptic writes can't evict it before it's read. Reset at the start of
// each discovery, then appended per characteristic.
void pplog_chars_reset(void);
void pplog_chars(const char *s);
uint16_t pplog_chars_snapshot(uint8_t *out, uint16_t max);

#endif // PP_SYS_PPLOG_H
