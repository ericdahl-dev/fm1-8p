/*
 * fm1_8p.h - public interface of the fm1-8p synth engine.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Plain C99, fixed-point only, no hardware or OS code, no dynamic
 * allocation: the caller owns the fm_engine storage and fm_engine_init
 * sets up everything inside it. fm_engine is about 5.0 KiB (5084 bytes on
 * 32- and 64-bit desktops and on Cortex-M), about 40 % of it the chorus
 * delay line and about 1 KiB the voices' filters, so static storage is
 * recommended; a task stack must have room for it on
 * top of its other use. See docs/fixed-point.md for the number formats.
 *
 * The struct layout is exposed only so callers can allocate it; treat
 * every field as private.
 *
 * The engine takes no locks. Call every function from one context, or
 * make sure no other call runs while fm_engine_render does (for example,
 * queue MIDI events and apply them between blocks in the audio
 * callback).
 */
#ifndef FM1_8P_H
#define FM1_8P_H

#include <stdint.h>

#include "chorus_state.h"
#include "voice.h"

#define FM_MAX_VOICES 8
/* Frames mixed at a time inside fm_engine_render (any block size works). */
#define FM_MIX_FRAMES 64
/* Keys unison mode remembers for last-note priority (oldest dropped). */
#define FM_HELD_MAX 16

typedef enum {
    FM_OK = 0,
    FM_ERR_ARG = -1 /* bad argument (null pointer, rate, voice count, id) */
} fm_status;

/* Parameter IDs. Stable: the patch format will store them. */
typedef enum {
    FM_PARAM_AMP_ATTACK_MS = 0,
    FM_PARAM_AMP_DECAY_MS,
    FM_PARAM_AMP_SUSTAIN, /* per mille of full level, 0..1000 */
    FM_PARAM_AMP_RELEASE_MS,
    FM_PARAM_BEND_RANGE, /* semitones each way at full bend, 0..24 */
    FM_PARAM_VOICE_MODE, /* FM_VOICE_MODE_POLY or FM_VOICE_MODE_UNISON */
    FM_PARAM_UNISON_DETUNE_CENTS, /* unison spread, lowest to highest voice */
    FM_PARAM_OSC1_WAVE, /* fm_wave: 0 saw, 1 pulse, 2 square, 3 noise */
    FM_PARAM_OSC1_PULSE_WIDTH_PCT, /* pulse duty, percent of a cycle */
    FM_PARAM_OSC1_OCTAVE,          /* -2..+2 octaves */
    FM_PARAM_OSC1_FINE_CENTS,      /* -100..+100 cents */
    FM_PARAM_OSC2_WAVE,            /* the same four for osc2 */
    FM_PARAM_OSC2_PULSE_WIDTH_PCT,
    FM_PARAM_OSC2_OCTAVE,
    FM_PARAM_OSC2_FINE_CENTS,
    FM_PARAM_OSC_MIX,              /* per mille: 0 osc1 only, 1000 osc2 only */
    FM_PARAM_CHORUS_MODE, /* 0 = off, then the fm_chorus_mode modes */
    FM_PARAM_LFO_WAVE,        /* fm_lfo_wave (docs/modulation.md) */
    FM_PARAM_LFO_RATE_CENTIHZ, /* hundredths of a hertz */
    FM_PARAM_LFO_DELAY_MS,    /* zero for this long, then fades in as long */
    FM_PARAM_LFO_PITCH_CENTS, /* vibrato depth at full LFO, each way */
    FM_PARAM_AMP_VELOCITY,    /* percent: 0 ignores velocity, 100 full curve */
    FM_PARAM_LFO_PW_PCT,      /* pulse width depth, percent of a cycle each way */
    /* lowpass cutoff, cents above MIDI note 0 (6900 = 440 Hz);
     * FM_FILTER_OPEN_CENTS (the maximum) is fully open */
    FM_PARAM_FILTER_CUTOFF_CENTS,
    FM_PARAM_FILTER_RESONANCE, /* per mille; 1000 self-oscillates */
    FM_PARAM_FILTER_KEY_TRACK, /* per mille; 1000 follows the note 1:1 */
    FM_PARAM_FILTER_ENV_ATTACK_MS,
    FM_PARAM_FILTER_ENV_DECAY_MS,
    FM_PARAM_FILTER_ENV_SUSTAIN, /* per mille of full level, 0..1000 */
    FM_PARAM_FILTER_ENV_RELEASE_MS,
    /* cents the filter envelope adds to the cutoff at full level;
     * negative sweeps the cutoff down */
    FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
    FM_PARAM_HIGHPASS_STEP, /* 0 (off) .. 3 (most low cut) */
    FM_PARAM_COUNT
} fm_param_id;

/* Values of FM_PARAM_VOICE_MODE (see docs/voices.md#unison). */
#define FM_VOICE_MODE_POLY 0
#define FM_VOICE_MODE_UNISON 1

typedef struct {
    uint16_t id;
    const char *name;
    int32_t min, max, def;
} fm_param_info;

/* Range and default for a parameter, or NULL for an unknown id. */
const fm_param_info *fm_param_info_get(uint16_t id);

typedef struct {
    uint32_t sample_rate;
    uint8_t voice_count;
    int32_t params[FM_PARAM_COUNT];
    int32_t mix_gain; /* Q1.30, applied to the sum of the voices */
    uint32_t stamp;   /* counts note ons and offs, to age the voices */
    uint32_t fade_samples; /* fast fade of a stolen voice */
    uint8_t pedal_down;    /* sustain pedal (CC 64 >= 64) */
    int32_t bend_value;    /* last pitch bend, -8192..8191 from center */
    int32_t bend;          /* Q16.16 semitones added to every note */
    fm_voice voices[FM_MAX_VOICES];
    int32_t detune[FM_MAX_VOICES]; /* Q16.16 semitones each voice is off by */
    uint8_t held[FM_HELD_MAX]; /* unison: keys down, oldest first */
    uint8_t held_count;
    fm_lfo lfo;          /* one LFO for every voice (docs/modulation.md) */
    fm_voice_mod mod;    /* what the LFO does to each voice */
    uint32_t tick_left;  /* samples until the next control tick */
    uint32_t ticks;      /* control ticks since init */
    uint8_t mod_on;      /* the last tick applied a nonzero depth */
    int32_t mix[FM_MIX_FRAMES]; /* render scratch, Q8.24; kept off the stack */
    int32_t mix_right[FM_MIX_FRAMES]; /* the chorus's right side, Q8.24 */
    /* output stage, after the voices are mixed: about 2.1 KiB, almost
     * all of it the delay line (see chorus_state.h) */
    fm_chorus chorus;
} fm_engine;

/*
 * Sets up `e` for `voice_count` voices (1..FM_MAX_VOICES) at
 * `sample_rate` Hz (8000..192000) with every parameter at its default.
 * Each note takes a voice; with more notes than voices the engine steals
 * one (the rule is in docs/voices.md). In unison mode (FM_PARAM_VOICE_MODE)
 * every voice plays the newest note instead, detuned. Each voice is mixed at
 * 1 / voice_count, then -6 dB, so a full chord never clips without filter
 * resonance; high resonance can reach full scale, where the output
 * saturates.
 */
fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count);

/*
 * Note numbers and velocities are MIDI 0..127; others are ignored.
 * Velocity 0 is a note off. Otherwise it scales the note's amp
 * envelope by the amp_velocity curve (docs/modulation.md#velocity).
 */
void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity);
void fm_engine_note_off(fm_engine *e, uint8_t note);

/*
 * MIDI control change (controller and value 0..127). Handled: 64
 * (sustain pedal, down at 64 or more), 120 (all sound off: silence now)
 * and 123 (all notes off: release every note, even those the pedal
 * holds). Others are ignored; the mod wheel comes in a later slice.
 */
void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value);

/*
 * MIDI pitch bend, 0..16383 (the 14-bit value; 8192 is center). Full
 * down is -bend_range_semitones, full up 8191/8192 of +range. Sounding
 * notes follow at once. Out-of-range values are ignored.
 */
void fm_engine_pitch_bend(fm_engine *e, uint16_t value);

/*
 * Sets a parameter; values outside its range are clamped. Returns
 * FM_ERR_ARG for an unknown id.
 */
fm_status fm_engine_set_param(fm_engine *e, uint16_t id, int32_t value);
/* Current value of a parameter, or 0 for an unknown id. */
int32_t fm_engine_get_param(const fm_engine *e, uint16_t id);

/*
 * Number of voices sounding (attack through release, or fading after a
 * steal). A voice goes idle once its release reaches silence.
 */
uint8_t fm_engine_active_voices(const fm_engine *e);

/* Renders `frames` interleaved stereo frames (L, R) of signed 16-bit. */
void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames);

#endif /* FM1_8P_H */
