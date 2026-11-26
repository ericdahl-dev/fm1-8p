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
 * It is the two-tap lowpass y[n] = a x[n] + (1 - a) x[n-1] (one zero, on the negative real axis), with the
 * weight a fitted at each sample rate against PG-8X rendered at that rate: PG-8X minus ours, harmonic by
 * harmonic at each render's measured f0, saw and square at six pitches, 1-20 kHz.
 *
 *   rate      a        rms left   (no lowpass)
 *   44.1 kHz  0.6950   0.29 dB    3.67 dB       the FM-1's rate
 *   48 kHz    0.6525   0.32 dB    3.69 dB
 *
 * One weight does not serve both: 48 kHz's at 44.1 kHz leaves 0.76 dB rms and reads 1.2 dB dark at 16-20 kHz.
 * Every 1-pole form fits worse (at 44.1 / 48 kHz: y += k (x - y) at 8400 Hz 0.96 / 0.76 dB, the bilinear
 * 1-pole at 14300 Hz 1.17 / 0.84). Other rates take a line between fitted points (fm_out_source_lp_weight):
 * at 32 kHz and below PG-8X minus ours asks for no lowpass (best a 0.985, 1.09 dB rms against 1.11 without),
 * so a is 1 there, a wire; at 88.2 kHz and above the two taps cannot reach it (a 0.5, their most, still
 * leaves 3.5 dB rms: a rate the FM-1 does not run).
 *
 * Its gain is 1 at DC, exactly, and each output lies between the two samples it weighs, so it adds no
 * peak and no clipping. It is linear and time-invariant; the stages before it are linear below the filter's
 * hold but not all time-invariant (the amp envelope, a swept VCF), so on the voices' sum it plays almost,
 * not exactly, as it would on each voice (docs/fixed-point.md lists where it differs).
 * Per frame: one 32-bit subtract, one 32 x 32 -> 64 multiply, a shift and an add.
 */
#define FM_OUT_SOURCE_LP_A_44K1 22774 /* a at 44.1 kHz, Q15: 0.6950 */
#define FM_OUT_SOURCE_LP_A_48K 21381  /* a at 48 kHz, Q15: 0.6525 */

/* The source lowpass's weight a for a sample rate, Q15 (16384..32768): event rate (init), no divide. */
int32_t fm_out_source_lp_weight(uint32_t sample_rate);

/*
 * One sample x of the source lowpass with weight a (Q15, 0..32768); *last holds the input before it (0 after
 * init: silence). x and *last must lie within +-FM_OUT_SOURCE_LP_MAX (2^30, Q8.24 64.0), so that their
 * difference fits int32 and the multiply is one 32 x 32 -> 64. fm_out_mix_block keeps to it for any sum.
 */
#define FM_OUT_SOURCE_LP_MAX (1 << 30)
static inline int32_t fm_out_source_lp(int32_t x, int32_t a, int32_t *last)
{
    int32_t d = x - *last;
    int32_t y = *last + (int32_t)(((int64_t)d * a) >> 15);
    *last = x;
    return y;
}

/*
 * The voices' sum to the mix, in place: each of the n Q8.24 samples times `gain` (Q1.30, the mix gain), then
 * through the source lowpass (weight a, state *last). `gain` must be 0..FM_OUT_MIX_GAIN_MAX (0.5): then any
 * int32 sum scales to within +-2^30, the lowpass's range (the engine's largest gain is 0.36,
 * FM_MIX_GAIN_Q30 at one voice and full level: engine.c checks it at compile time). The one per-sample loop
 * between the voices and the chorus, in a function of its own so it can be placed with the other per-sample
 * code by one attribute (#304).
 */
#define FM_OUT_MIX_GAIN_MAX (1 << 29)
void fm_out_mix_block(int32_t *mix, uint32_t n, int32_t gain, int32_t a, int32_t *last);

/*
 * Writes n interleaved stereo frames to `out` from the Q8.24 sums `left`
 * and `right` (right NULL: both sides from left). Each sample gets the
 * +6 dB makeup and the soft clip (output.c), then the output gain of
 * `shift` doublings with a clamp at +-32767 (shift 0: none).
 */
void fm_out_block(int16_t *out, const int32_t *left, const int32_t *right, uint32_t n, uint8_t shift);

#endif /* FM_OUTPUT_H */
