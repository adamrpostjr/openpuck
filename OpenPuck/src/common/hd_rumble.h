// hd_rumble.h — Nintendo Switch HD-rumble amplitude decoder, shared byte-for-byte
// by OpenPuck and PicoPuck (both drive the SC2's single ERM from the console's
// dual-band HD-rumble stream, so both need the same scalar-amplitude decode).
//
// When emulating a Pro Controller the console streams a 4-byte word per motor
// each frame; the top two bits select how many amplitude/frequency updates are
// packed. A naive fixed-byte decode mis-reads the packed modes as spurious
// amplitude (phantom menu buzzing), so the full mode handling here is required.
// Amplitude is carried in 1/32 log2 units over [-8, 0] → integer [-256, 0],
// mapped to a 16-bit level via a boot-built exp2 table (no FP in the hot path).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_HD_RUMBLE_H
#define OPK_COMMON_HD_RUMBLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One motor's running low/high band amplitudes (1/32 log2 units). Packed 5-bit
// commands are relative, so this state must persist across frames — keep one
// hdr_band_t per (slot, motor).
typedef struct {
	int16_t lo;
	int16_t hi;
} hdr_band_t;

// Reset a motor's bands to silent. Call per motor at init / (re)mount.
void hdr_reset(hdr_band_t *band);

// Decode one motor's 4 rumble bytes, advancing *band, and return the PEAK 16-bit
// motor level over the frame's updates. Every idle/neutral frame resolves to 0.
// The exp2 level table is built on the first call.
uint16_t hdr_decode(hdr_band_t *band, const uint8_t b4[4]);

#ifdef __cplusplus
}
#endif

#endif // OPK_COMMON_HD_RUMBLE_H
