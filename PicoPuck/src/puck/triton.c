// triton.c — SC2 input model + report 0x45 encoder (see triton.h).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "puck/triton.h"
#include <string.h>

puck_input_t g_in[PP_NSLOT];

// Write a 16-bit LE value at out[2 + off] (the offset convention triton's field
// decoders use: report byte = 2 + field-offset-from-buttons-low-byte).
static inline void put16(uint8_t *out, int off, int v)
{
	out[2 + off] = (uint8_t)(v & 0xFF);
	out[2 + off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void put32(uint8_t *out, int idx, uint32_t v)
{
	out[idx] = (uint8_t)(v & 0xFF);
	out[idx + 1] = (uint8_t)((v >> 8) & 0xFF);
	out[idx + 2] = (uint8_t)((v >> 16) & 0xFF);
	out[idx + 3] = (uint8_t)((v >> 24) & 0xFF);
}

uint8_t puck_synth45(const puck_input_t *in, uint8_t seq, uint32_t usec,
		     uint8_t out[PUCK45_LEN])
{
	memset(out, 0, PUCK45_LEN);
	out[0] = 0x45;
	out[1] = seq;
	out[2] = (uint8_t)(in->buttons & 0xFF);
	out[3] = (uint8_t)((in->buttons >> 8) & 0xFF);
	out[4] = (uint8_t)((in->buttons >> 16) & 0xFF);
	out[5] = (uint8_t)((in->buttons >> 24) & 0xFF);
	// Triggers: the SC2's analog trigger is a u16 that trigU8 maps via >>7, so
	// the inverse is lt << 7 (full 0xFF pull → 0x7F80).
	put16(out, 4, (int)in->lt << 7);
	put16(out, 6, (int)in->rt << 7);
	put16(out, 8, in->lx);
	put16(out, 10, in->ly);
	put16(out, 12, in->rx);
	put16(out, 14, in->ry);
	put16(out, 16, in->lpx);
	put16(out, 18, in->lpy);
	put16(out, 22, in->rpx);
	put16(out, 24, in->rpy);
	// IMU timestamp (report bytes 30..33): Steam integrates gyro as gyro*dt with
	// dt from the delta of this µs counter. Zero gyro × any dt = 0, so it's
	// harmless for generic pads and correct once a gyro-capable pad lands.
	put32(out, 30, usec);
	put16(out, 32, in->ax);
	put16(out, 34, in->ay);
	put16(out, 36, in->az);
	put16(out, 38, in->gx);
	put16(out, 40, in->gy);
	put16(out, 42, in->gz);
	return PUCK45_LEN;
}

// Field decoders: offset is relative to rep[2] (the buttons low byte), matching
// OpenPuck's s16off/u16off/trigU8.
static inline int rd_s16(const uint8_t *r, int off)
{
	int v = r[2 + off] | (r[2 + off + 1] << 8);
	return (v & 0x8000) ? v - 0x10000 : v;
}
static inline int rd_u16(const uint8_t *r, int off)
{
	return r[2 + off] | (r[2 + off + 1] << 8);
}
static inline uint8_t trig_u8(int u16v)
{
	int v = u16v >> 7;
	return (uint8_t)(v > 255 ? 255 : v);
}

void triton_decode45(const uint8_t *rep, uint8_t tlen, puck_input_t *io)
{
	io->buttons = (uint32_t)rep[2] | ((uint32_t)rep[3] << 8) |
		      ((uint32_t)rep[4] << 16) | ((uint32_t)rep[5] << 24);
	io->lt = trig_u8(rd_u16(rep, 4));
	io->rt = trig_u8(rd_u16(rep, 6));
	io->lx = (int16_t)rd_s16(rep, 8);
	io->ly = (int16_t)rd_s16(rep, 10);
	io->rx = (int16_t)rd_s16(rep, 12);
	io->ry = (int16_t)rd_s16(rep, 14);
	io->lpx = (int16_t)rd_s16(rep, 16);
	io->lpy = (int16_t)rd_s16(rep, 18);
	io->rpx = (int16_t)rd_s16(rep, 22);
	io->rpy = (int16_t)rd_s16(rep, 24);
	if (tlen >= 46) {
		io->ax = (int16_t)rd_s16(rep, 32);
		io->ay = (int16_t)rd_s16(rep, 34);
		io->az = (int16_t)rd_s16(rep, 36);
		io->gx = (int16_t)rd_s16(rep, 38);
		io->gy = (int16_t)rd_s16(rep, 40);
		io->gz = (int16_t)rd_s16(rep, 42);
	}
}

uint32_t triton_hat_bits(uint8_t hat)
{
	static const uint32_t T[9] = {
		0,
		TB_DUP,
		TB_DUP | TB_DRT,
		TB_DRT,
		TB_DRT | TB_DDN,
		TB_DDN,
		TB_DDN | TB_DLF,
		TB_DLF,
		TB_DLF | TB_DUP,
	};
	return hat <= 8 ? T[hat] : 0;
}
