/*
 * Units: the engine's time, rate and ratio conversions, in one place.
 *
 * One rounding rule: every conversion truncates toward zero (integer
 * division). Each function states the range it is exact over; all run on
 * settings and notes, never per sample.
 */
#ifndef FM1_8P_UNITS_H
#define FM1_8P_UNITS_H

#include <stdint.h>

/* Milliseconds to samples at `rate` Hz. The product is formed in 64 bits,
 * so any ms and rate fit (10 s at 192 kHz: 1.92e9 before the divide). */
uint32_t fm_ms_to_samples(int32_t ms, uint32_t rate);

/* Per mille (-1000..+1000) to Q1.30: 1000 is 1 << 30. A multiply, not a
 * shift: shifting a negative value left is undefined in C. */
int32_t fm_permille_to_q30(int32_t permille);

/* Microseconds to samples at `rate` Hz, Q16.16. Exact while
 * us * rate < 2^48 (10 ms at 192 kHz is about 2^31). */
uint64_t fm_us_to_samples_q16(uint32_t us, uint32_t rate);

#endif
