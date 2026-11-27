/*
 * output.h - the engine's output stage (#342): the voices' mixed sum, after
 * the mix gain and the chorus, to the int16 that goes to the DAC.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_OUTPUT_H
#define FM_OUTPUT_H

#include <stdint.h>
#include "placement.h"

/*
 * Writes n interleaved stereo frames to `out` from the Q8.24 sums `left`
 * and `right` (right NULL: both sides from left). Each sample gets the
 * +6 dB makeup and the soft clip (output.c), then the output gain of
 * `shift` doublings with a clamp at +-32767 (shift 0: none).
 */
void fm_out_block(int16_t *out, const int32_t *left, const int32_t *right, uint32_t n, uint8_t shift);

#endif /* FM_OUTPUT_H */
