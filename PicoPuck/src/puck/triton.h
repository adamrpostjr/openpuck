// triton.h — the Steam Controller 2's native input model, shared by every path.
//
// Bluetooth drivers decode a controller into one PuckInput per slot; the puck
// personality serialises it back into an SC2 report 0x45 (puck_synth45) for the
// host, or — for a real SC2 — forwards the on-air 0x45 verbatim. Ported from
// OpenPuck/triton.h (button masks and field layout are the SC2's).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_TRITON_H
#define PICOPUCK_TRITON_H

#include <stdint.h>
#include "config/picopuck_config.h"
#include "triton_masks.h" // shared TB_* button masks + SW_TRIG_ON + CHORD_BACK4
#include "triton_input.h" // shared canonical input struct (puck_input_t)

extern puck_input_t g_in[PP_NSLOT];

// Length of a full SC2 report 0x45 (report id + 45 body bytes).
#define PUCK45_LEN 46

// Serialise g_in-style input into a report 0x45 buffer (out[0]=0x45). Trackpads
// and IMU are written from the struct (zero for generic pads → neutral). The µs
// timestamp Steam uses for gyro dt is stamped from `usec`. Returns PUCK45_LEN.
uint8_t puck_synth45(const puck_input_t *in, uint8_t seq, uint32_t usec,
		     uint8_t out[PUCK45_LEN]);

// 8-way HID hat (0 idle, 1..8 clockwise from N) → TB_ dpad bits.
uint32_t triton_hat_bits(uint8_t hat);

// Decode an SC2 report 0x45 / 0x42 (rep[0]=id, rep[1]=seq) into `io`. `tlen` is
// the full report length incl id; IMU is decoded only when tlen >= 46.
void triton_decode45(const uint8_t *rep, uint8_t tlen, puck_input_t *io);

#endif // PICOPUCK_TRITON_H
