/*
 * chorus.h - stereo chorus at the end of the signal path (internal to
 * the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Mono in, stereo out: the summed voices go in as one Q8.24 sample and
 * come out as a left and a right Q8.24 sample. Mode 0 (off) passes the
 * input to both outputs unchanged.
 */
#ifndef FM_CHORUS_H
#define FM_CHORUS_H

#include <stdint.h>

typedef enum {
    FM_CHORUS_OFF = 0,
    FM_CHORUS_MODE_COUNT
} fm_chorus_mode;

typedef struct {
    uint32_t sample_rate;
    uint8_t mode; /* fm_chorus_mode */
} fm_chorus;

/* Sets up `c` at `sample_rate` Hz (non-zero), mode off. */
void fm_chorus_init(fm_chorus *c, uint32_t sample_rate);

/* Selects a mode; an unknown mode turns the chorus off. */
void fm_chorus_set_mode(fm_chorus *c, uint8_t mode);

/* Processes one sample: `in` is Q8.24, `left` and `right` get Q8.24. */
void fm_chorus_process(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right);

#endif /* FM_CHORUS_H */
