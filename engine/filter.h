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
    int32_t s[4];  /* lowpass stage outputs, Q8.24 */
    /* the first stage's input one sample ago, Q8.24 (each later stage's
     * is the stage before it one sample ago: s[i - 1] before the update) */
    int32_t x1;
    int32_t g;     /* lowpass stage coefficient, Q1.30 (1.0 = open) */
    int32_t k;     /* resonance feedback in use, Q4.28 (0 .. 4.2) */
    /* 1.0 below the top of the cutoff range, to 0 at open; a stage weighs
     * its previous input by fade / 4 (the zero, 1/4 below the top) */
    int32_t fade;
    int32_t hp;   /* highpass: its internal lowpass state, Q8.24 */
    int32_t hp_g; /* highpass coefficient, Q1.30 (0 = off) */
    /* Resonance's bass shelf: the output is s[3] + h * (lp - s[3]), lp a
     * one-pole lowpass of s[3] at the cutoff (h 0: off, the plain ladder).
     * A weighted average of the two, so it stays within their range. */
    int32_t lp;   /* the shelf's lowpass state, Q8.24 */
    int16_t a;    /* input level, Q2.13: the resonance's passband level */
    int16_t h;    /* weight of lp, Q2.13 (0 .. 0.57) */
    /* the resonance as set, per mille, and what it asks for below the fade
     * (looked up once, at fm_filter_set_resonance): the retune in cents,
     * h and a in Q2.13 */
    int16_t res, retune, shelf, level;
    int16_t cut;  /* lowpass cutoff as set (clamped), cents */
    int16_t rate; /* rate_cents it was set with (0: not yet) */
} fm_filter;

/* Pitch of the sample rate in cents above MIDI note 0. Computed once
 * per sample rate (at init); the other calls take it as `rate_cents`. */
int32_t fm_filter_rate_cents(uint32_t sample_rate);

/*
 * Cutoff of a voice, in cents: the base cutoff, plus key tracking (per
 * mille of the note's distance from middle C, note 60), plus the filter
 * envelope (`env_level` Q1.30, 0..1.0, times `env_amount_cents`, which
 * may be negative). Unclamped; fm_filter_set_cutoff clamps.
 */
int32_t fm_filter_voice_cents(int32_t cutoff_cents, int32_t track_permille,
                              uint8_t note, int32_t env_level,
                              int32_t env_amount_cents);

/* Clears the state and opens the filter (no resonance, no highpass). */
void fm_filter_reset(fm_filter *f);
/* Clears the state (silences any ringing), keeping the settings. */
void fm_filter_clear(fm_filter *f);

/* Lowpass cutoff in cents above MIDI note 0; clamped to 0..open. */
void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents);

/* Resonance, per mille (0 = none, 1000 = self-oscillation 190 cents under
 * the cutoff). As on PG-8X 2.0.13 it also lowers the peak under the
 * cutoff, sets the passband level and keeps the treble-to-bass balance
 * (docs/fixed-point.md, Resonance). All of it fades out over the top 600
 * cents of cutoff. */
void fm_filter_set_resonance(fm_filter *f, int32_t permille);

/* Highpass position 0..3 (higher is clamped to 3): 0 is off (a wire),
 * then one-pole (6 dB/octave) corners at about 100, 240 and 560 Hz. */
#define FM_FILTER_HIGHPASS_STEPS 4
void fm_filter_set_highpass(fm_filter *f, uint8_t step, int32_t rate_cents);

/* Filters one Q8.24 sample: highpass, then lowpass. Any int32 input (the
 * whole +-128.0) is valid: no overflow, a bounded output, and exactly the
 * output of the same filter computed in 64 bits. */
int32_t fm_filter_process(fm_filter *f, int32_t in);

/* Nonzero while the filter is a wire (fully open, highpass off): the one
 * place that decides it. */
static inline int fm_filter_is_wire(const fm_filter *f)
{
    return f->fade == 0 && f->hp_g == 0;
}

/*
 * fm_filter_process with its wide-open case inline, for the render loop:
 * open (fade 0) with the highpass off, the filter is a wire for any input
 * the soft clip passes untouched (within +-2.0), and keeps only the last
 * sample as its state. Exactly the same output and state as the call.
 */
static inline int32_t fm_filter_run(fm_filter *f, int32_t in)
{
    if (fm_filter_is_wire(f) && in <= (2 << 24) && in >= -(2 << 24)) {
        f->s[3] = in;
        return in;
    }
    return fm_filter_process(f, in);
}

#endif /* FM_FILTER_H */
