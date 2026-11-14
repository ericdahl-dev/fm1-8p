/*
 * fm1_8p.h - public interface of the fm1-8p synth engine.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Plain C99, fixed-point only, no hardware or OS code, no dynamic
 * allocation: the caller owns the fm_engine storage and fm_engine_init
 * sets up everything inside it. fm_engine is about 6.1 KiB (at most
 * FM_ENGINE_BUDGET_32 bytes on 32-bit targets such as the FM-1 and Cortex-M,
 * FM_ENGINE_BUDGET_64 on 64-bit desktops, where the envelopes' settings
 * pointers take 8 bytes: see below the param list), about 40 % of it
 * the chorus delay line and about 1 KiB the voices' filters, so static
 * storage is recommended; a task stack must have room for it on top of
 * its other use. See docs/fixed-point.md for the number formats.
 *
 * The struct layout is exposed only so callers can allocate it; treat
 * every field as private. Do not copy an fm_engine (assignment, memcpy):
 * its voices point at settings inside it (#19), so a copy's voices would
 * still read the original's. Initialize each one with fm_engine_init.
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
/* settings that glide with setting_glide (#146) */
#define FM_GLIDE_SETTINGS 8
/* Keys unison mode remembers for last-note priority (oldest dropped). */
#define FM_HELD_MAX 16

typedef enum {
    FM_OK = 0,
    FM_ERR_ARG = -1 /* bad argument (null pointer, rate, voice count, id) */
} fm_status;

/* Parameter IDs (fm_param_id), generated from tools/params.py. */
#include "fm1_8p_params.h"

/*
 * fm_engine's size budget: a base plus 4 bytes a param (fm_engine.params),
 * so a new param needs no edit here. Anything else that grows (voice state,
 * the chorus) must raise the base. The FM-1 build checks the 32-bit budget
 * (engine/engine.c), the host tests the 64-bit one (tests/test_chorus.c).
 */
#define FM_ENGINE_BUDGET_BASE_32 6048u
#define FM_ENGINE_BUDGET_BASE_64 6404u
#define FM_ENGINE_BUDGET_32 (FM_ENGINE_BUDGET_BASE_32 + 4u * (unsigned)FM_PARAM_COUNT)
#define FM_ENGINE_BUDGET_64 (FM_ENGINE_BUDGET_BASE_64 + 4u * (unsigned)FM_PARAM_COUNT)

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
    fm_env_settings amp_set;    /* every voice's amp envelope reads these */
    fm_env_settings filter_set; /* and its filter envelope these (#19) */
    fm_env_settings gate_set;   /* the amp envelope's with amp_gate (#63) */
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
    uint8_t glide_moving; /* setting_glide (#146): a setting is on its way */
    int32_t mix[FM_MIX_FRAMES]; /* render scratch, Q8.24; kept off the stack */
    /* the chorus's right side, Q8.24; before the chorus writes it, the
     * voices' scratch (each voice's oscillators for the chunk): the chorus
     * only writes it (chorus_block_writes_right_without_reading_it) */
    int32_t mix_right[FM_MIX_FRAMES];
    /* setting_glide (#146): where each gliding setting is now, Q8 in
     * its own units (its parameter holds it rounded), where it goes, and
     * the one-pole's step (1 - a, Q16) */
    int32_t glide_cur[FM_GLIDE_SETTINGS];
    int16_t glide_to[FM_GLIDE_SETTINGS];
    int32_t glide_k;
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
 * 1 / voice_count, then -2.85 dB (PG-8X 2.0.13's level, #102); past the
 * knee at -2.85 dBFS a soft clip bends the output short of full scale.
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
