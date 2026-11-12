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
    int32_t xmod;      /* cross-mod depth, Q1.30 0..1.0 */
    uint32_t xmod_inc; /* osc2's increment times the depth: its largest bend */
    uint32_t recip0;   /* 1 / osc1's increment, scaled by 2^recip0_sh (for sync) */
    uint32_t base[2];  /* increment before the pitch modulation */
    int32_t mod_oct[2]; /* each oscillator's pitch modulation, Q16.16 octaves */
    int32_t pw_mod;    /* pulse width modulation (LFO), Q0.32 of a cycle */
    uint32_t noise;    /* xorshift32 state, never 0 */
    uint8_t wave[2];   /* fm_wave */
    uint8_t sync;      /* 1: osc2 restarts with every cycle of osc1 */
    uint8_t sync_pend; /* a restart's correction is due on the next osc2 sample */
    uint8_t recip0_sh;
    uint8_t special;   /* sync or cross-mod on */
    uint8_t only1;     /* mix 0 and nothing special: osc1 alone, the fast path */
    uint8_t ring;      /* 1: osc2 times osc1, the JX-8P's SYNC 2 and X-MOD */
    int32_t sync_after; /* polyBLEP terms due on osc2's sample after a restart, Q8.24 */
} fm_osc_pair;

/*
 * Saw on both, default tuning and width, mix fully to the first. `seed`
 * starts the noise generator (0 picks a fixed non-zero seed); give each
 * voice its own so noise chords don't sum in step.
 */
void fm_osc_pair_reset(fm_osc_pair *p, uint32_t seed);
/* Selects oscillator `which`'s (0 or 1) waveform; others are ignored. */
void fm_osc_pair_set_wave(fm_osc_pair *p, uint8_t which, uint8_t wave);
/* Pulse duty of oscillator `which`, in percent of a cycle (1..99; values
 * outside are clamped). */
void fm_osc_pair_set_width(fm_osc_pair *p, uint8_t which, int32_t pct);
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
/*
 * Hard sync: with `on`, osc2's phase restarts each time osc1 starts a
 * cycle, at the point within the sample where osc1 wrapped, so the pair
 * sounds at osc1's pitch with osc2's tuning shaping the tone.
 */
void fm_osc_pair_set_sync(fm_osc_pair *p, uint8_t on);
/*
 * Cross-mod depth, Q1.30 (0..FM_OSC_MIX_FULL): osc1's output moves osc2's
 * frequency each sample, by up to +-100 % of it at full depth (frequency
 * modulation, so sidebands at osc2 +- multiples of osc1). 0 is off.
 */
void fm_osc_pair_set_xmod(fm_osc_pair *p, int32_t depth);
/*
 * The JX-8P's ring (SYNC 2 and X-MOD, as PG-8X 2.0.13 plays them, #105):
 * with `on`, osc2's signal becomes x2 (a + b m1) + c m1 before the mix, m1
 * being osc1's sample (peak 1.0) and b near -1: osc2 at nearly full
 * amplitude modulation by an inverted osc1, with a little of osc1 on top.
 * A product, so no pitch moves. docs/patches.md has a, b and c.
 */
void fm_osc_pair_set_ring(fm_osc_pair *p, uint8_t on);
/*
 * Pitch modulation in Q16.16 octaves (0: none), each oscillator its own
 * (`oct1`, `oct2`), on top of the pitch and tuning. Scales the increments
 * by 2^x with no divide, so it can run at the control rate; the pulse
 * width limits stay those of the unmodulated pitch
 * (docs/modulation.md#pitch).
 */
void fm_osc_pair_set_pitch_mod(fm_osc_pair *p, int32_t oct1, int32_t oct2);
/*
 * Pulse width modulation: `offset` (Q0.32 of a cycle, signed) is added
 * to both oscillators' set widths, clamped to 1..99 % and the pitch
 * limits. The gain that keeps a pulse at peak 1.0 then comes from a
 * reciprocal by Newton's method (no divide), so it can run at the
 * control rate. 0 restores the widths exactly.
 */
void fm_osc_pair_set_pw_mod(fm_osc_pair *p, int32_t offset);
/* Next mixed sample, Q8.24. */
int32_t fm_osc_pair_next(fm_osc_pair *p);
/*
 * The next `n` mixed samples into out[0..n-1], Q8.24: exactly what `n`
 * calls of fm_osc_pair_next give, samples and state alike.
 */
void fm_osc_pair_render(fm_osc_pair *p, int32_t *out, uint32_t n);

#endif /* FM_OSC_H */
