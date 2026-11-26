/*
 * output.h - the engine's output stage (#342): the voices' mixed sum, after
 * the mix gain and the chorus, to the int16 that goes to the DAC.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_OUTPUT_H
#define FM_OUTPUT_H

#include <stdint.h>

/*
 * The source lowpass (#299 option A): PG-8X 2.0.13's fixed top-band roll-off, which every source carries
 * (saw, square, synced DCO-2, noise; the same from C4 to C7). Ours lacked it: a held saw or square read up to
 * 4.6 dB brighter at 14-16 kHz and 7.0 dB at 18-20 kHz, the same at every pitch.
 *
 * It is the two-tap lowpass y[n] = a x[n] + (1 - a) x[n-1], a = 0.66 (one zero, on the real axis at
 * -0.515): fitted to PG-8X minus ours, harmonic by harmonic at each render's measured f0, saw and square at
 * six pitches, 1-20 kHz, it is within 0.34 dB rms at 48 kHz and 0.62 dB at 44.1 kHz (the FM-1's rate), where
 * the best 1-pole (y += k (x - y), corner 8400 Hz) is within 0.76 / 0.96 dB and the bilinear one (14300 Hz)
 * 0.84 / 1.17. Against the analog 1-pole that fits best (10650 Hz) it is within 1.0 dB to 18 kHz at 48 kHz, and
 * 1.33 dB darker at 20 kHz.
 * Its -3 dB point is 12.9 kHz at 48 kHz, 11.8 kHz at 44.1 kHz.
 *
 * Its gain is 1 at DC, exactly, and it never leaves the span of the two samples it weighs, so it adds no
 * peak and no clipping. It is linear and time-invariant, as is everything from the oscillators to the mix
 * below the filter's hold, so on the voices' sum it plays as it would on each voice. One multiply a frame.
 */
#define FM_OUT_SOURCE_LP_A 21627 /* a, Q15: 0.66 */

/* One sample x of the source lowpass; *last holds the input before it (0 after init: silence). The sum
 * is taken in 64 bits: between the rails the step alone passes int32 (0.66 x 2^32), the result never. */
static inline int32_t fm_out_source_lp(int32_t x, int32_t *last)
{
    int32_t y = (int32_t)(*last + ((((int64_t)x - *last) * FM_OUT_SOURCE_LP_A) >> 15));
    *last = x;
    return y;
}

/*
 * Writes n interleaved stereo frames to `out` from the Q8.24 sums `left`
 * and `right` (right NULL: both sides from left). Each sample gets the
 * +6 dB makeup and the soft clip (output.c), then the output gain of
 * `shift` doublings with a clamp at +-32767 (shift 0: none).
 */
void fm_out_block(int16_t *out, const int32_t *left, const int32_t *right, uint32_t n, uint8_t shift);

#endif /* FM_OUTPUT_H */
