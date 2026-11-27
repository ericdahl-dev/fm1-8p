/*
 * filter.h - per-voice filter: two-section resonant lowpass and stepped highpass
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
#ifdef FM_CHECK_BUSY
#include <assert.h>
#endif

/* At or above this cutoff the lowpass is fully open: a plain wire. */
#define FM_FILTER_OPEN_CENTS 14400

typedef struct {
    /* the sections' states, Q9.23: section 1's ic1 (bandpass) and ic2
     * (lowpass), then section 2's; while the filter is a wire, s[3] keeps
     * the last sample instead (Q8.24, fm_filter_run) */
    int32_t s[4];
    int32_t c[4];  /* each state's carried remainder (under 2^30) */
    int32_t a1, a2, a3; /* the sections' coefficients, Q1.30 */
    int32_t t;     /* tan(pi f0 / fs), Q5.27: the sections' zeros */
    /* section 1's zeros, z1 of the way to the origin (5/8, to 0 near Nyquist): 3 z1, Q1.30, and t z1,
     * Q5.27 */
    int32_t z3, zt;
    /* 1.0 below the top of the cutoff range, to 0 at open: the output's
     * share against the input there */
    int32_t fade;
    int32_t hp;   /* highpass: its internal lowpass state, Q8.24 */
    int32_t hp_g; /* highpass coefficient, Q1.30 (0 = off, < 0 the boost) */
    int32_t a;    /* input level, Q1.30: PG-8X's passband, k Q0 */
    int16_t res;  /* resonance as set, per mille */
    int16_t cut;  /* lowpass cutoff as set (clamped), cents */
    int16_t rate; /* rate_cents it was set with (0: not yet) */
    /* how much of the resonant top's handover plays, 0..FM_FILTER_UR_ONE (below): where the resonance says,
     * or on its way there on a sounding voice (in the struct's padding: fm_filter is no larger for it) */
    int16_t ur;
} fm_filter;

/* Pitch of the sample rate in cents above MIDI note 0. Computed once
 * per sample rate (at init); the other calls take it as `rate_cents`. */
int32_t fm_filter_rate_cents(uint32_t sample_rate);

/*
 * Cutoff of a voice, in cents (#253). Its sources add in linear cents, as
 * PG-8X 2.0.13 adds VCF ENV and KEY to VCF FREQ in knob steps:
 * `cutoff_linear` (the base, from fm_filter_linear_cents), key tracking
 * (per mille of the note's distance from middle C, note 60, in linear
 * cents), and `mod_cents`, the voice's modulation of the cutoff (the
 * filter envelope and every other source routed there, summed in
 * engine/mod.h; may be negative). The sum is then bent once onto
 * PG-8X's cutoff curve (engine/filter.c, cutoff_curve). Unclamped;
 * fm_filter_set_cutoff clamps.
 */
int32_t fm_filter_voice_cents(int32_t cutoff_linear, int32_t track_permille,
                              uint8_t note, int32_t mod_cents);

/*
 * The linear cents whose curve value is nearest `cents`: the base the cutoff
 * setting gives fm_filter_voice_cents, so that a voice at note 60 with
 * no modulation plays the setting within a cent (exactly, for a cutoff
 * that is itself on the curve). Event rate: an integer square root and a
 * divide.
 */
int32_t fm_filter_linear_cents(int32_t cents);

/* Clears the state and opens the filter (no resonance, no highpass). */
void fm_filter_reset(fm_filter *f);
/* Clears the state (silences any ringing), keeping the settings. */
void fm_filter_clear(fm_filter *f);

/* Lowpass cutoff in cents above MIDI note 0; clamped to 0..open. */
void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents);

/*
 * The resonant top's handover (#168, filter.c). Up to resonance FM_FILTER_TOP_RES_LO the top of the cutoff
 * range is the one below (it fades to the input: an open cutoff is a wire); from FM_FILTER_TOP_RES_HI it is
 * PG-8X's resonant top (no fade: an open cutoff keeps the resonance's level cut and is not a wire); between,
 * linear. f->ur is how much of it plays, in FM_FILTER_UR_ONE.
 *
 * The handover is 22 per mille wide and takes the output from a wire to a filter, and a RES knob step is 10:
 * taken at once that is a click (4.8 % of the output's peak for 90 -> 100 at an open cutoff). So a resonance
 * turned on a sounding voice (fm_filter_turn_resonance) leaves f->ur where it plays, and
 * fm_filter_handover_step, once a control point, moves it FM_FILTER_UR_STEP at most: all of it in 32 control
 * points (512 samples, 11.6 ms at 44.1 kHz), about PG-8X's own 16 ms glide of a setting.
 */
#define FM_FILTER_TOP_RES_LO 90
#define FM_FILTER_TOP_RES_HI 112
#define FM_FILTER_UR_ONE 16368                  /* 744 a per mille of the handover's 22 */
#define FM_FILTER_UR_STEP (FM_FILTER_UR_ONE / 32 + 1)

/* Where f->ur goes: the handover at the resonance as set. */
static inline int32_t fm_filter_ur_target(const fm_filter *f)
{
    int32_t r = f->res - FM_FILTER_TOP_RES_LO;
    return r <= 0 ? 0 : r >= FM_FILTER_TOP_RES_HI - FM_FILTER_TOP_RES_LO ? FM_FILTER_UR_ONE
         : r * (FM_FILTER_UR_ONE / (FM_FILTER_TOP_RES_HI - FM_FILTER_TOP_RES_LO));
}

/* Nonzero while the handover is on its way (fm_filter_handover_step has steps to make). */
static inline int fm_filter_handover_moving(const fm_filter *f)
{
    return f->ur != fm_filter_ur_target(f);
}

/* Resonance, per mille: the sections' Q, PG-8X 2.0.13's (0.69 at 0, 6.7
 * at 935, its RES 127), and the passband with it (Q0 / Q); past 935 on to
 * Q 13.4 at 1000, ringing long but never self-oscillating (as PG-8X). Up
 * to resonance 90 the output fades to the input over the top 600 cents of
 * cutoff; from 112 the top is PG-8X's resonant one (11500 cents up, at
 * 44.1 to 48 kHz: filter.c), which does not. Set at once, the handover
 * with it: for a voice that isn't sounding, and for anything that sets a
 * filter up.
 *
 * At resonance 0 the top of the range is fitted to PG-8X's (#151, filter.c):
 * f0 stops at 0.306 of the rate, the sections take more damping, and the fade
 * is the top 2210 cents. Resonances 1 to 388 get a share of it (the long fade
 * only up to about 100); from 389 up nothing of it. Only at the rates it is
 * fitted for, 44.1 to 48 kHz: every other rate keeps the top it had. Per
 * sample that changes one thing: at resonances 0 to about 100 the fade's
 * multiply runs from cutoff 12191 up, where it ran from 13801. */
void fm_filter_set_resonance(fm_filter *f, int32_t permille);

/* The same on a sounding voice: every coefficient at once, as fm_filter_set_resonance, but the handover
 * (f->ur) stays where it plays, for fm_filter_handover_step to move. */
void fm_filter_turn_resonance(fm_filter *f, int32_t permille);

/* Once a control point while fm_filter_handover_moving: the handover one step on, and the coefficients with
 * it. Returns 0 if it was there already, else 1, plus 2 while there is further to go. Control rate. */
int fm_filter_handover_step(fm_filter *f);

/* Highpass position 0..4 (higher is clamped to 4): 0 is off (a wire),
 * then one-pole (6 dB/octave) corners at about 100, 240 and 560 Hz. 4 is
 * not a highpass but the JX-8P's HPF position 0, a bass boost (filter.c). */
#define FM_FILTER_HIGHPASS_STEPS 5
#define FM_FILTER_HIGHPASS_BOOST 4
void fm_filter_set_highpass(fm_filter *f, uint8_t step, int32_t rate_cents);

/* Filters one Q8.24 sample: highpass, then lowpass. Any int32 input (the
 * whole +-128.0) is valid: no overflow, a bounded output, and exactly the
 * output of the same filter computed in 64 bits. */
int32_t fm_filter_process(fm_filter *f, int32_t in);

/* Nonzero while the filter is a wire (fully open, highpass off, and no
 * more resonance than the low top's, 90: with more the open cutoff keeps
 * PG-8X's level cut and the fade is not 0): the one place that decides it. */
static inline int fm_filter_is_wire(const fm_filter *f)
{
    return f->fade == 0 && f->hp_g == 0;
}

/* The sections' input and the output are held to +-FM_FILTER_HOLD (Q8.24
 * 16.0; engine/filter.c): past anything the pair plays (3.868, a ringed
 * spike) through the bass boost (2.28 x) at the input level's 1.0, 8.8. */
#define FM_FILTER_HOLD (16 << 24)

/*
 * fm_filter_process with its wide-open case inline, for the render loop:
 * open (fade 0) with the highpass off, the filter is a wire for any input
 * within its hold (+-16.0), and keeps only the last sample as its state.
 * Exactly the same output and state as the call.
 */
static inline int32_t fm_filter_run(fm_filter *f, int32_t in)
{
    if (fm_filter_is_wire(f) && in <= FM_FILTER_HOLD && in >= -FM_FILTER_HOLD) {
        f->s[3] = in;
        return in;
    }
    return fm_filter_process(f, in);
}

/* `last`, the last of samples that passed the filter as a wire without
 * running it (the voice's fast path): kept as fm_filter_run keeps it, so a
 * filter that closes carries on from it. Only while it is a wire. */
static inline void fm_filter_pass_wire(fm_filter *f, int32_t last)
{
#ifdef FM_CHECK_BUSY /* debug builds: the fast path runs only a wire */
    assert(fm_filter_is_wire(f));
#endif
    f->s[3] = last;
}

#endif /* FM_FILTER_H */
