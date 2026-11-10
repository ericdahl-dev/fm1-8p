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

/* Phase increment (Q0.32 per sample) for a MIDI note at a sample rate. */
uint32_t fm_osc_note_inc(uint8_t note, uint32_t sample_rate);

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
    uint32_t width[2]; /* pulse duty, Q0.32 of a cycle */
    uint8_t wave[2];   /* fm_wave */
} fm_osc_pair;

void fm_osc_pair_reset(fm_osc_pair *p);
/* Selects oscillator `which`'s (0 or 1) waveform; others are ignored. */
void fm_osc_pair_set_wave(fm_osc_pair *p, uint8_t which, uint8_t wave);
/* Pulse duty of oscillator `which` as a Q0.32 fraction of a cycle. */
void fm_osc_pair_set_width(fm_osc_pair *p, uint8_t which, uint32_t width);
/* Plays a MIDI note at a sample rate: sets both oscillators' pitch. */
void fm_osc_pair_set_note(fm_osc_pair *p, uint8_t note, uint32_t sample_rate);
/* Next mixed sample, Q8.24. */
int32_t fm_osc_pair_next(fm_osc_pair *p);

#endif /* FM_OSC_H */
