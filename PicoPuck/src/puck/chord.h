// chord.h — controller button chord → USB mode switch (persist + reboot).
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PICOPUCK_CHORD_H
#define PICOPUCK_CHORD_H

#include <stdbool.h>

// Check the mode-switch chord against g_in[slot] (call after each input decode).
// Guard: an SC2 uses the four back paddles (like OpenPuck); a generic pad uses
// L2+R2+LB+RB (which every pad has). While the guard is held a face button picks
// the target (A→Steam, B/X/Y→configured modes) and the guard+face bits are
// masked out of g_in so they don't leak to the host. A stable selection reboots
// into the new mode.
void chord_check(int slot, bool is_sc2);

#endif // PICOPUCK_CHORD_H
