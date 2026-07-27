// triton.h -- the controller's native input as it comes off the RF link, and the decoders for it.
//
// "Triton" is the Steam Controller 2 controller. Its HID input report 0x45 is what the puck relays; rf_link.cpp
// decodes one into the shared g_in struct each fresh frame, and every USB personality reads g_in to build its
// own host report. report 0x45 layout:
//   [0]=0x45 [1]=seq [2..5]=buttons u32; analog offsets below are from the buttons low byte (rep[2]).
#pragma once
#include <stdint.h>
// Triton button masks (TB_*), SW_TRIG_ON and CHORD_BACK4 are the canonicalization
// contract shared byte-for-byte with PicoPuck — one source of truth.
#include "src/common/triton_masks.h"

// ---- report 0x45 field decoders (offsets relative to rep[2], the buttons low byte) ----
static inline int s16off(const uint8_t *r, int off)
{
	int v = r[2 + off] | (r[2 + off + 1] << 8);
	return (v & 0x8000) ? v - 0x10000 : v;
}
static inline int u16off(const uint8_t *r, int off)
{
	return r[2 + off] | (r[2 + off + 1] << 8);
}
// Controller trigger u16 tops out near half-scale (~0x8000) at a full pull, so a straight >>8 reads only ~0x80
// (host sees a half-pressed trigger). Scale x2 (>>7) and saturate so a full pull maps to 0xFF.
static inline uint8_t trigU8(int u16v)
{
	int v = u16v >> 7;
	return (uint8_t)(v > 255 ? 255 : v);
}
static inline uint32_t btnsOf(const uint8_t *r)
{
	return (uint32_t)r[2] | ((uint32_t)r[3] << 8) | ((uint32_t)r[4] << 16) |
	       ((uint32_t)r[5] << 24);
}
// report 0x45 IMU offsets (PROTOCOL.md §8): accel @0x22, gyro @0x28 from report start.
void imuFrom45(const uint8_t *r, int16_t *ax, int16_t *ay, int16_t *az,
	       int16_t *gx, int16_t *gy, int16_t *gz);

// ---- shared decoded input (filled by rf_link.cpp once per fresh report 0x45, read by every mode) ----
// One PuckInput per bond slot: each controller in a multi-slot puck has its own decoded input. The stream
// modes (Switch, PS5, DS4) read g_in[s] for each active slot in their task() loop; the puck/lizard mode reads
// g_in[slot] inside onReport45/onAuxReport to forward to hid[slot].
struct PuckInput {
	// raw Triton buttons (TB_*); per-mode builders apply their own chord masking
	uint32_t buttons;
	int16_t lx, ly, rx, ry; // sticks (int16, center 0)
	uint8_t lt, rt; // triggers scaled 0..255 (trigU8)
	int16_t lpx, lpy, rpx, rpy; // left / right trackpad coords (int16)
	int16_t ax, ay, az; // accelerometer
	int16_t gx, gy, gz; // gyroscope
};
#include "bonds.h" // NSLOT
extern PuckInput g_in[NSLOT];
