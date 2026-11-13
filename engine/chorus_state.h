/*
 * chorus_state.h - the stereo chorus's state and modes (internal to the
 * engine; fm1_8p.h includes it only so callers can size fm_engine). The
 * functions are in chorus.h, which only the engine includes.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Mono in, stereo out: the summed voices go in as one Q8.24 sample and
 * come out as a left and a right Q8.24 sample. Mode 0 (off) passes the
 * input to both outputs unchanged.
 *
 * When on, the input goes into one short delay line, read by two taps
 * whose delay a triangle LFO sweeps in opposite directions (left gets
 * center + sweep, right center - sweep). Each output is the dry input
 * plus its tap (as PG-8X's chorus, #104), so an output never exceeds the
 * input peak plus the delay line's +-2.0 clamp; the dry part is limited
 * to about +-126.0 so the sum never overflows int32.
 *
 * Memory: fixed, no allocation. The delay line is FM_CHORUS_DELAY_LEN
 * int16_t samples = 2048 bytes; the whole struct is about 2.1 KiB
 * (sizeof(fm_chorus) is 2072 on the desktop), and it lives inside
 * fm_engine. The line holds 23 ms at 44.1 kHz; the deepest mode needs
 * 9.0 ms, so above about 113 kHz a mode's delays are scaled down to fit.
 */
#ifndef FM_CHORUS_STATE_H
#define FM_CHORUS_STATE_H

#include <stdint.h>

/* Delay line length in samples; a power of two so the index wraps by mask. */
#define FM_CHORUS_DELAY_LEN 1024

/*
 * Modes (LFO rate, center delay, sweep either side of the center):
 *   SLOW  0.68 Hz, 2.0 ms, +-1.6 ms - chorus I, PG-8X 2.0.13's chorus
 *   DEEP  0.68 Hz, 2.1 ms, +-2.0 ms - chorus II, the same, swept deeper
 * (the JX-8P loader sends both to PG-8X's one chorus, so both match it)
 *   FAST  6.0 Hz, 3.0 ms, +-0.3 ms - quick, shallow shimmer
 */
typedef enum {
    FM_CHORUS_OFF = 0,
    FM_CHORUS_SLOW,
    FM_CHORUS_DEEP,
    FM_CHORUS_FAST,
    FM_CHORUS_MODE_COUNT
} fm_chorus_mode;

typedef struct {
    uint32_t sample_rate;
    uint32_t lfo_phase; /* Q0.32, one LFO cycle = 2^32 */
    uint32_t lfo_inc;   /* Q0.32 per sample */
    int32_t center;     /* delay in samples, Q16.16 */
    int32_t depth;      /* sweep either side of center, samples, Q16.16 */
    uint16_t pos;       /* index of the newest sample in line */
    uint8_t mode;       /* fm_chorus_mode */
    int16_t line[FM_CHORUS_DELAY_LEN]; /* Q8.24 >> 10: 1.0 = 16384 */
} fm_chorus;

#endif /* FM_CHORUS_STATE_H */
