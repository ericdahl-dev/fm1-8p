/*
 * filter.h - per-voice filter: 4-pole lowpass and stepped highpass
 * (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Pitches are in cents above MIDI note 0 (8.18 Hz), so 6900 is 440 Hz
 * and one octave is 1200. Cutoff, key tracking and the filter envelope
 * are summed in that domain, then turned into a coefficient by
 * fm_filter_set_cutoff at control rate (not per sample). Samples are
 * Q8.24, as everywhere in the engine.
 */
#ifndef FM_FILTER_H
#define FM_FILTER_H

#include <stdint.h>

/* At or above this cutoff the lowpass is fully open: a plain wire. */
#define FM_FILTER_OPEN_CENTS 14400

typedef struct {
    int32_t s[4]; /* lowpass stage outputs, Q8.24 */
    int32_t g;    /* lowpass stage coefficient, Q1.30 (1.0 = open) */
} fm_filter;

/* Pitch of the sample rate in cents above MIDI note 0. Computed once
 * per sample rate (at init); the other calls take it as `rate_cents`. */
int32_t fm_filter_rate_cents(uint32_t sample_rate);

/* Clears the state and opens the filter (no resonance, no highpass). */
void fm_filter_reset(fm_filter *f);

/* Lowpass cutoff in cents above MIDI note 0; clamped to 0..open. */
void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents);

/* Resonance, per mille (0 = none, 1000 = self-oscillation). */
void fm_filter_set_resonance(fm_filter *f, int32_t permille);

/* Filters one Q8.24 sample. */
int32_t fm_filter_process(fm_filter *f, int32_t in);

#endif /* FM_FILTER_H */
