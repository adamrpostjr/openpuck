// triton_masks.h — the Steam Controller 2 canonical button field (TB_* masks)
// and the analog-trigger digital threshold. This is the canonicalization
// contract every controller decodes into and every output personality reads,
// so it MUST be identical across OpenPuck and PicoPuck — hence one shared file
// both firmwares include. (The g_in struct + decoders differ per firmware and
// stay in each project's triton.h.)
//
// The 32-bit field is report 0x45 bytes [2..5]. Bits 0..29 are real SC2 bits;
// bits 30/31 are unused by the controller and reused as virtual PS remap targets
// (TB_TOUCH/TB_MUTE) settable only via a back-paddle/QAM remap.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef OPK_COMMON_TRITON_MASKS_H
#define OPK_COMMON_TRITON_MASKS_H

#define TB_A 0x1u
#define TB_B 0x2u
#define TB_X 0x4u
#define TB_Y 0x8u
#define TB_QAM 0x10u
#define TB_R3 0x20u
#define TB_VIEW 0x40u
#define TB_R4 0x80u
#define TB_R5 0x100u
#define TB_RB 0x200u
#define TB_DDN 0x400u
#define TB_DRT 0x800u
#define TB_DLF 0x1000u
#define TB_DUP 0x2000u
#define TB_MENU 0x4000u
#define TB_L3 0x8000u
#define TB_STEAM 0x10000u
#define TB_L4 0x20000u
#define TB_L5 0x40000u
#define TB_LB 0x80000u
#define TB_RPADT 0x200000u
#define TB_RPADC 0x400000u
#define TB_LPADT 0x2000000u
#define TB_LPADC 0x4000000u
#define TB_R2 0x800000u
#define TB_L2 0x8000000u
#define TB_TOUCH 0x40000000u // PS touchpad click (remap-only virtual target)
#define TB_MUTE 0x80000000u // PS5 mute (remap-only virtual target)

// All four back paddles held → mode-switch chord guard.
#define CHORD_BACK4 (TB_R4 | TB_L4 | TB_R5 | TB_L5)

// Analog-trigger fraction (of 0xFF) at which digital ZL/ZR / L2/R2 trips.
#define SW_TRIG_ON 40

#endif // OPK_COMMON_TRITON_MASKS_H
