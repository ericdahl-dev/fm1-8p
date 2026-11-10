/*
 * osc.h - band-limited oscillator (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_OSC_H
#define FM_OSC_H

#include <stdint.h>

typedef struct {
    uint32_t phase; /* Q0.32, one full cycle = 2^32 */
    uint32_t inc;   /* Q0.32 phase increment per sample, < 2^31 */
} fm_osc;

/* Highest allowed increment: just under Nyquist. */
#define FM_OSC_MAX_INC 0x7FFF0000u

/*
 * Highest pitch the table covers: note 131 (15.8 kHz), Q16.16 semitones.
 * Pitches outside 0..FM_OSC_MAX_PITCH fold back by whole octaves, so an
 * interval stays exact up to the Nyquist cap.
 */
#define FM_OSC_MAX_PITCH (131 << 16)

/*
 * Phase increment (Q0.32 per sample) for a pitch in Q16.16 semitones (MIDI note numbers with
 * a fraction), capped just under Nyquist. The fraction follows
 * 2^(f / 12) to well under 0.01 cent. Integer math; for events, not per
 * sample.
 */
uint32_t fm_osc_pitch_inc(int32_t pitch, uint32_t sample_rate);

void fm_osc_reset(fm_osc *o);
void fm_osc_set_inc(fm_osc *o, uint32_t inc);

/* Next sample of a polyBLEP saw, Q8.24 (nominal range -1.0..+1.0). */
int32_t fm_osc_saw_next(fm_osc *o);

/* Oscillator waveforms. Stable: the patch format stores them. */
typedef enum {
    FM_WAVE_SAW = 0,
    FM_WAVE_PULSE,
    FM_WAVE_SQUARE,
    FM_WAVE_NOISE,
    FM_WAVE_COUNT
} fm_wave;

/*
 * The two oscillators of one voice. Treat the fields as private; set
 * them with the functions below (on events, never per sample).
 */
typedef struct {
    fm_osc osc[2];
    uint32_t width[2]; /* pulse duty as set, Q0.32 of a cycle */
    uint32_t pw[2];    /* duty played: width limited by the pitch */
    int32_t pw_gain[2]; /* Q1.30: brings the pulse's peak to 1.0 */
    int32_t tune[2];   /* offset from the played pitch, Q16.16 semitones */
    int32_t mix;       /* share of the second oscillator, Q1.30 0..1.0 */
    uint32_t noise;    /* xorshift32 state, never 0 */
    uint8_t wave[2];   /* fm_wave */
} fm_osc_pair;

/*
 * Saw on both, default tuning and width, mix fully to the first. `seed`
 * starts the noise generator (0 picks a fixed non-zero seed); give each
 * voice its own so noise chords don't sum in step.
 */
void fm_osc_pair_reset(fm_osc_pair *p, uint32_t seed);
/* Selects oscillator `which`'s (0 or 1) waveform; others are ignored. */
void fm_osc_pair_set_wave(fm_osc_pair *p, uint8_t which, uint8_t wave);
/* Pulse duty of oscillator `which` as a Q0.32 fraction of a cycle. */
void fm_osc_pair_set_width(fm_osc_pair *p, uint8_t which, uint32_t width);
/*
 * Tuning of oscillator `which` in cents from the played pitch (an octave
 * is 1200). `pitch` (Q16.16 semitones) is what the pair plays now, so
 * that oscillator retunes at once; the other is left alone.
 */
void fm_osc_pair_set_tune(fm_osc_pair *p, uint8_t which, int32_t cents,
                          int32_t pitch, uint32_t sample_rate);
/*
 * Crossfade between the oscillators, Q1.30: 0 is the first alone,
 * FM_OSC_MIX_FULL the second alone, values between a linear blend.
 */
#define FM_OSC_MIX_FULL (1 << 30)
void fm_osc_pair_set_mix(fm_osc_pair *p, int32_t mix);
/*
 * Plays `pitch` (Q16.16 semitones: the note plus any bend): each
 * oscillator sounds at it plus its own tuning.
 */
void fm_osc_pair_set_pitch(fm_osc_pair *p, int32_t pitch, uint32_t sample_rate);
/* Next mixed sample, Q8.24. */
int32_t fm_osc_pair_next(fm_osc_pair *p);

#endif /* FM_OSC_H */
