// chord.h — controller button chord → USB mode switch (persist + reboot).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_CHORD_H
#define PICOPUCK_CHORD_H

#include <stdbool.h>

// Call ONCE per freshly-decoded BT report (from the input path, on raw g_in).
// Either guard works for any controller: the four back paddles (SC2) OR
// L2+R2+LB+RB. A face button picks the target (A→Steam, B→PS5, X→DS4, Y→Switch)
// and the mode switches immediately (persist + reboot) — no hold needed.
void chord_note(int slot);

#endif // PICOPUCK_CHORD_H
