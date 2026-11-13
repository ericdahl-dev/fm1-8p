/*
 * Units: the engine's time, rate and ratio conversions, in one place.
 *
 * One rounding rule: every conversion truncates toward zero (integer
 * division). Each function states the range it is exact over; all run on
 * settings and notes, never per sample. Inline, so constant arguments fold.
 */
#ifndef FM1_8P_UNITS_H
#define FM1_8P_UNITS_H

#include <stdint.h>

/* Milliseconds to samples at `rate` Hz: a negative time is 0, and a count
 * past 32 bits saturates (the product, under 2^63, is formed in 64 bits).
 * The longest setting, 32 s at 192 kHz, is 6144000 samples. */
static inline uint32_t fm_ms_to_samples(int32_t ms, uint32_t rate)
{
    uint64_t n;
    if (ms <= 0) return 0;
    n = (uint64_t)(uint32_t)ms * rate / 1000u; /* < 2^31 * 2^32 */
    return n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)n;
}

/* Per mille to Q1.30: 1000 is 1 << 30, clamped to -1000..+1000. A
 * multiply, not a shift: shifting a negative value left is undefined in C. */
static inline int32_t fm_permille_to_q30(int32_t permille)
{
    if (permille > 1000) permille = 1000;
    if (permille < -1000) permille = -1000;
    return (int32_t)((int64_t)permille * (1 << 30) / 1000);
}

/* Microseconds to samples at `rate` Hz, Q16.16. Exact while
 * us * rate < 2^48 (10 ms at 192 kHz is about 2^31). */
static inline uint64_t fm_us_to_samples_q16(uint32_t us, uint32_t rate)
{
    return ((uint64_t)us * rate << 16) / 1000000u;
}

/*
 * 2^(cents / 1200) in Q16.16, saturating at 2^31 - 1: the fraction of an
 * octave from a quartic fit of 2^f on [0, 1) (Q30 coefficients, under
 * 0.011 cent off, as the oscillators' pitch modulation), whole octaves as
 * shifts. Exactly 65536 at 0. One division by 1200 (events only).
 */
static inline uint32_t fm_exp2_cents_q16(int32_t cents)
{
    static const int64_t c[4] = { 744149770, 259040010, 56213420, 14325366 };
    int32_t k = cents >= 0 ? cents / 1200 : -((1199 - cents) / 1200);
    int64_t f = (int64_t)(cents - k * 1200) * 65536 / 1200, acc; /* Q16, 0..1 */
    acc = c[3];
    acc = ((acc * f) >> 16) + c[2];
    acc = ((acc * f) >> 16) + c[1];
    acc = ((acc * f) >> 16) + c[0];
    acc = ((int64_t)1 << 30) + ((acc * f) >> 16); /* 2^f, Q2.30 */
    acc >>= 14;                                  /* Q16 */
    if (k >= 15) return 0x7FFFFFFFu;
    if (k >= 0) acc <<= k;
    else acc = k <= -31 ? 0 : acc >> -k;
    return acc > 0x7FFFFFFF ? 0x7FFFFFFFu : (uint32_t)acc;
}

/* The fastest phase step: just under half a cycle per step. */
#define FM_PHASE_INC_MAX 0x7FFFFFFFu

/*
 * A rate of `value / per_hz` Hz to a Q0.32 phase step (cycles per step) at
 * `rate` Hz, a step being 2^shift samples, capped at FM_PHASE_INC_MAX.
 * Rate 0 gives 0 (no divide by zero: the FM-1's core doesn't trap on one).
 * Exact while value < 2^(32 - shift); the LFO's 3000 centihertz per
 * 2^5-sample tick and the chorus's millihertz per sample are far inside.
 */
static inline uint32_t fm_rate_to_phase_inc(uint32_t value, uint32_t per_hz, uint32_t rate,
                              unsigned shift)
{
    uint64_t d, inc, r;
    unsigned n;
    if (rate == 0 || per_hz == 0 || value == 0) return 0;
    d = (uint64_t)rate * per_hz;
    if (shift < 32 && (uint64_t)value >> (32 - shift) == 0) { /* fits 64 bits */
        inc = ((uint64_t)value << (32 + shift)) / d;
        return inc > FM_PHASE_INC_MAX ? FM_PHASE_INC_MAX : (uint32_t)inc;
    }
    /* Wider: long division, value * 2^(32 + shift) / d a bit at a time,
     * stopping at the cap. value >= 1 and d < 2^64, so by 95 doublings it
     * is past the cap: a larger shift needs no more. */
    if (value >= d) return FM_PHASE_INC_MAX; /* a cycle a step or more */
    if (shift > 63) shift = 63;
    inc = 0;
    r = value; /* r < d throughout */
    for (n = 0; n < 32 + shift; n++) {
        inc <<= 1;
        if (r >= d - r) { r -= d - r; inc |= 1; }
        else r <<= 1;
        if (inc > FM_PHASE_INC_MAX) return FM_PHASE_INC_MAX;
    }
    return (uint32_t)inc;
}

#endif
