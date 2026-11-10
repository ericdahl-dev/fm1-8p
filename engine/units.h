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

#endif
