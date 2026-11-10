/*
 * voice.c - one note: oscillator pair, filter and its envelope, and amp
 * envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "voice.h"

void fm_voice_reset(fm_voice *v, uint32_t seed, int32_t rate_cents)
{
    fm_osc_pair_reset(&v->osc, seed);
    fm_env_reset(&v->amp_env);
    v->stamp = 0;
    v->pitch = 0;
    v->next_pitch = 0;
    v->wait = 0;
    v->gate_left = 0;
    v->note = 0;
    v->old_note = 0;
    v->key_down = 0;
    v->sustained = 0;
    v->pending = 0;
    fm_filter_reset(&v->filter);
    v->rate_cents = rate_cents;
    v->cutoff_cents = FM_FILTER_OPEN_CENTS;
    v->key_track = 0;
    v->env_amount = 0;
    v->key_note = 0;
    v->next_key = 0;
    fm_env_reset(&v->filter_env);
    v->ctrl_phase = 0;
}

int fm_voice_active(const fm_voice *v)
{
    return v->amp_env.stage != FM_ENV_IDLE || v->pending;
}

/* Samples between cutoff updates (control rate: 2756 Hz at 44.1 kHz). */
#define FILTER_CTRL_SAMPLES 16

/* Recomputes the cutoff from the settings, the voice's note and its
 * filter envelope level `fenv`. */
static void update_cutoff(fm_voice *v, int32_t fenv)
{
    fm_filter_set_cutoff(&v->filter,
                         fm_filter_voice_cents(v->cutoff_cents, v->key_track,
                                               v->key_note, fenv,
                                               v->env_amount),
                         v->rate_cents);
}

/* Gates both envelopes on. The filter envelope starts from where the amp
 * envelope does: from its level on a retrigger, from 0 on an idle voice
 * (fresh, or after a steal fade), whatever level it was left at. */
static void gate_on(fm_voice *v)
{
    if (v->amp_env.stage == FM_ENV_IDLE) fm_env_reset(&v->filter_env);
    fm_env_gate_on(&v->amp_env);
    fm_env_gate_on(&v->filter_env);
}

/* Starts the note from the current level (0 when idle). A note whose
 * key came up while it waited for a steal fade queues its release for as
 * long as it waited, so it is heard rather than released at level 0. */
static void start(fm_voice *v, uint32_t sample_rate)
{
    v->pending = 0;
    v->ctrl_phase = 0; /* the new note's cutoff from its first sample */
    v->pitch = v->next_pitch;
    v->key_note = v->next_key;
    fm_osc_pair_set_pitch(&v->osc, v->pitch, sample_rate);
    gate_on(v);
    if (!v->key_down && !v->sustained) v->gate_left = v->wait ? v->wait : 1;
}

/* The voice now plays `note` at `pitch`, held by its key. */
static void hold(fm_voice *v, uint8_t note, int32_t pitch)
{
    v->note = note;
    v->next_key = note;
    v->key_down = 1;
    v->sustained = 0;
    v->gate_left = 0;
    v->next_pitch = pitch;
}

void fm_voice_note_on(fm_voice *v, uint8_t note, int32_t pitch,
                      uint32_t sample_rate, uint32_t fade_samples)
{
    int same = fm_voice_active(v) && v->note == note;
    uint8_t old = v->note;
    hold(v, note, pitch);
    if (v->pending) return; /* starts when the fade ends */
    if (same || v->amp_env.stage == FM_ENV_IDLE) {
        start(v, sample_rate);
    } else {
        v->old_note = old;
        v->pending = 1;
        v->wait = fade_samples;
        fm_env_fade(&v->amp_env, fade_samples);
    }
}

void fm_voice_take_back(fm_voice *v, int32_t pitch, uint32_t sample_rate)
{
    hold(v, v->old_note, pitch);
    start(v, sample_rate);
}

void fm_voice_change_note(fm_voice *v, uint8_t note, int32_t pitch,
                          uint32_t sample_rate, int retrigger)
{
    hold(v, note, pitch);
    if (v->pending) return; /* starts when the fade ends */
    v->pitch = pitch;
    v->key_note = note;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
    /* the new note's cutoff at once; the 16-sample cadence carries on */
    update_cutoff(v, v->filter_env.level);
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE) gate_on(v);
}

void fm_voice_key_up(fm_voice *v, int pedal_down)
{
    if (pedal_down) {
        v->key_down = 0;
        v->sustained = 1;
    } else {
        fm_voice_release(v);
    }
}

void fm_voice_release(fm_voice *v)
{
    v->key_down = 0;
    v->sustained = 0;
    if (!v->pending) {
        fm_env_gate_off(&v->amp_env);
        fm_env_gate_off(&v->filter_env);
    }
}

void fm_voice_silence(fm_voice *v)
{
    fm_env_reset(&v->amp_env);
    fm_env_reset(&v->filter_env);
    fm_filter_clear(&v->filter); /* no ringing left for the next note */
    v->pending = 0;
    v->gate_left = 0;
    v->key_down = 0;
    v->sustained = 0;
}

void fm_voice_set_pitch(fm_voice *v, int32_t pitch, uint32_t sample_rate)
{
    v->next_pitch = pitch;
    if (v->pending) return;
    v->pitch = pitch;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
}

/* A fading old note retunes too: `pitch` is still its pitch. */
void fm_voice_set_tune(fm_voice *v, uint8_t which, int32_t cents,
                       uint32_t sample_rate)
{
    fm_osc_pair_set_tune(&v->osc, which, cents, v->pitch, sample_rate);
}

void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames,
                         uint32_t sample_rate)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t level, fenv, osc;
        if (v->amp_env.stage == FM_ENV_IDLE) {
            if (!v->pending) return;
            start(v, sample_rate); /* the stolen note has faded out */
        }
        if (v->gate_left && --v->gate_left == 0) { /* the queued release */
            fm_env_gate_off(&v->amp_env);
            fm_env_gate_off(&v->filter_env);
        }
        level = fm_env_next(&v->amp_env);
        fenv = fm_env_next(&v->filter_env);
        if (v->ctrl_phase == 0) {
            update_cutoff(v, fenv);
            v->ctrl_phase = FILTER_CTRL_SAMPLES;
        }
        v->ctrl_phase--;
        osc = fm_filter_process(&v->filter, fm_osc_pair_next(&v->osc));
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)osc * level) >> 30);
    }
}
