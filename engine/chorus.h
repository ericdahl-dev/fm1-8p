/*
 * chorus.h - stereo chorus at the end of the signal path (internal to
 * the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Mono in, stereo out: the summed voices go in as one Q8.24 sample and
 * come out as a left and a right Q8.24 sample. Mode 0 (off) passes the
 * input to both outputs unchanged.
 *
 * When on, the input goes into one short delay line, read by two taps
 * whose delay a triangle LFO sweeps in opposite directions (left gets
 * center + sweep, right center - sweep). Each output is half the dry
 * input plus half its tap, so an output never exceeds the larger of the
 * input peak and the delay line's +-2.0 clamp.
 *
 * Memory: fixed, no allocation. The delay line is FM_CHORUS_DELAY_LEN
 * int16_t samples = 2048 bytes; the whole struct is about 2.1 KiB
 * (sizeof(fm_chorus) is 2072 on the desktop), and it lives inside
 * fm_engine. The line holds 23 ms at 44.1 kHz; the deepest mode needs
 * 7.5 ms, so above about 136 kHz a mode's delays are scaled down to fit.
 */
#ifndef FM_CHORUS_H
#define FM_CHORUS_H

#include <stdint.h>

/* Delay line length in samples; a power of two so the index wraps by mask. */
#define FM_CHORUS_DELAY_LEN 1024

/*
 * Modes (LFO rate, center delay, sweep either side of the center):
 *   SLOW  0.5 Hz, 5.0 ms, +-1.5 ms - gentle, slow movement
 *   DEEP  0.8 Hz, 5.0 ms, +-2.5 ms - wider and faster
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

/* Sets up `c` at `sample_rate` Hz, mode off. */
void fm_chorus_init(fm_chorus *c, uint32_t sample_rate);

/*
 * Selects a mode; an unknown mode, or a zero sample rate, turns the
 * chorus off. Turning it on from off starts from a silent delay line.
 * Switching between two "on" modes jumps the tap delays at once (no
 * crossfade), so it can click while sound is playing.
 */
void fm_chorus_set_mode(fm_chorus *c, uint8_t mode);

/* Empties the delay line (all sound off); the mode and LFO are kept. */
void fm_chorus_silence(fm_chorus *c);

/* Processes one sample: `in` is Q8.24, `left` and `right` get Q8.24. */
void fm_chorus_process(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right);

#endif /* FM_CHORUS_H */
