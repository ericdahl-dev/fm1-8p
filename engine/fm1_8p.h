/*
 * fm1_8p.h - public interface of the fm1-8p synth engine.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Plain C99, fixed-point only, no hardware or OS code, no dynamic
 * allocation: the caller owns the fm_engine storage (static or on a task
 * stack) and fm_engine_init sets up everything inside it. See
 * docs/fixed-point.md for the number formats.
 *
 * The struct layout is exposed only so callers can allocate it; treat
 * every field as private.
 */
#ifndef FM1_8P_H
#define FM1_8P_H

#include <stdint.h>

#include "env.h"
#include "osc.h"

#define FM_MAX_VOICES 8

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
    FM_PARAM_COUNT
} fm_param_id;

typedef struct {
    uint16_t id;
    const char *name;
    int32_t min, max, def;
} fm_param_info;

/* Range and default for a parameter, or NULL for an unknown id. */
const fm_param_info *fm_param_info_get(uint16_t id);

typedef struct {
    fm_osc osc;
    fm_env amp_env;
    uint32_t stamp; /* engine event count at the last note on or off */
    uint8_t note;
    uint8_t key_down;
} fm_voice;

typedef struct {
    uint32_t sample_rate;
    uint8_t voice_count;
    int32_t params[FM_PARAM_COUNT];
    int32_t mix_gain; /* Q1.30, applied to the sum of the voices */
    uint32_t stamp;   /* counts note ons and offs, to age the voices */
    fm_voice voices[FM_MAX_VOICES];
} fm_engine;

/*
 * Sets up `e` for `voice_count` voices (1..FM_MAX_VOICES) at
 * `sample_rate` Hz (1..192000) with every parameter at its default.
 * This slice plays one voice whatever the count.
 */
fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count);

/*
 * Note numbers and velocities are MIDI 0..127; others are ignored.
 * Velocity 0 is a note off. Otherwise velocity has no effect in this
 * slice: every note plays at the same level (velocity lands in #8).
 */
void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity);
void fm_engine_note_off(fm_engine *e, uint8_t note);

/*
 * MIDI control change (controller and value 0..127). This slice handles
 * 120 (all sound off: silence now) and 123 (all notes off: release);
 * others are ignored. Sustain, mod wheel and bend come in later slices.
 */
void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value);

/*
 * Sets a parameter; values outside its range are clamped. Returns
 * FM_ERR_ARG for an unknown id.
 */
fm_status fm_engine_set_param(fm_engine *e, uint16_t id, int32_t value);
/* Current value of a parameter, or 0 for an unknown id. */
int32_t fm_engine_get_param(const fm_engine *e, uint16_t id);

/* Renders `frames` interleaved stereo frames (L, R) of signed 16-bit. */
void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames);

#endif /* FM1_8P_H */
