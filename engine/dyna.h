/*
 * DYNAMICS: how much of a velocity-sensitive depth a note plays.
 *
 * PG-8X 2.0.13 scales the DCO pitch env, MIX env and VCF env depths, and the
 * VCA gain, by one shared curve per DYNAMICS position (#8). This module is
 * that curve; nothing in the voice uses it yet.
 */
#ifndef FM1_8P_DYNA_H
#define FM1_8P_DYNA_H

#include <stdint.h>

/* The share of full depth, Q15 (32768 = full), at DYNAMICS `position`
 * (0 = off, 1..3) and note-on `velocity` (1..127). Out of range clamps:
 * a position below 0 is off and past 3 is 3; velocity is held to 1..127.
 * A table lookup, for note-on, not per sample. */
uint32_t fm_dyna_share_q15(int position, int velocity);

#endif
