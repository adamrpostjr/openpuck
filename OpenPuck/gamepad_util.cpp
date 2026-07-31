// gamepad_util.cpp -- only the CONFIG-COUPLED report helpers live here; the pure
// packers (stick/touch/hat/face/shoulder math) are in the shared source of truth
// src/common/gamepad_util.c, linked by both OpenPuck and PicoPuck. These two read
// OpenPuck's config globals (g_abSwap / g_back / g_qamMap), so they stay local.
#include "gamepad_util.h"
#include "triton.h"
#include "config.h"

// PS face nibble with the A/B + X/Y swap driven by the global Nintendo-layout
// flag (the shared ps_face_nibble takes the swap as an explicit parameter).
uint8_t psFaceNibble(uint32_t b)
{
	return ps_face_nibble(b, g_abSwap);
}

// Apply the QAM remap, the all-four-back-paddles chord guard, and the four
// back-paddle remaps to a raw Triton button field. Uses the shared code->TB_ map.
uint32_t psButtonsFromSteam(uint32_t raw)
{
	uint32_t b = raw;
	if (g_qamMap && (b & TB_QAM)) {
		b &= ~(uint32_t)TB_QAM;
		b |= tritonFromCode(g_qamMap);
	}
	if ((b & CHORD_BACK4) == CHORD_BACK4)
		b &= ~(uint32_t)(TB_A | TB_B | TB_X | TB_Y);
	if (b & TB_L4)
		b |= tritonFromCode(g_back[0]);
	if (b & TB_R4)
		b |= tritonFromCode(g_back[1]);
	if (b & TB_L5)
		b |= tritonFromCode(g_back[2]);
	if (b & TB_R5)
		b |= tritonFromCode(g_back[3]);
	return b;
}
