/*
 * voice.c - one note: oscillator and amp envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "voice.h"

void fm_voice_reset(fm_voice *v, int32_t rate_cents)
{
    fm_osc_reset(&v->osc);
    fm_env_reset(&v->amp_env);
    v->stamp = 0;
    v->next_inc = 0;
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
    fm_env_reset(&v->filter_env);
    v->ctrl_phase = 0;
}

int fm_voice_active(const fm_voice *v)
{
    return v->amp_env.stage != FM_ENV_IDLE || v->pending;
}

/* Starts the note from the current level (0 when idle). A note whose
 * key came up while it waited for a steal fade queues its release for as
 * long as it waited, so it is heard rather than released at level 0. */
static void start(fm_voice *v)
{
    v->pending = 0;
    v->ctrl_phase = 0; /* the new note's cutoff from its first sample */
    fm_osc_set_inc(&v->osc, v->next_inc);
    fm_env_gate_on(&v->amp_env);
    fm_env_gate_on(&v->filter_env);
    if (!v->key_down && !v->sustained) v->gate_left = v->wait ? v->wait : 1;
}

void fm_voice_note_on(fm_voice *v, uint8_t note, uint32_t inc,
                      uint32_t fade_samples)
{
    int same = fm_voice_active(v) && v->note == note;
    uint8_t old = v->note;
    v->note = note;
    v->key_down = 1;
    v->sustained = 0;
    v->next_inc = inc;
    v->gate_left = 0;
    if (v->pending) return; /* starts when the fade ends */
    if (same || v->amp_env.stage == FM_ENV_IDLE) {
        start(v);
    } else {
        v->old_note = old;
        v->pending = 1;
        v->wait = fade_samples;
        fm_env_fade(&v->amp_env, fade_samples);
    }
}

void fm_voice_take_back(fm_voice *v, uint32_t inc)
{
    v->note = v->old_note;
    v->key_down = 1;
    v->sustained = 0;
    v->gate_left = 0;
    v->next_inc = inc;
    start(v);
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

void fm_voice_set_inc(fm_voice *v, uint32_t inc)
{
    v->next_inc = inc;
    if (!v->pending) fm_osc_set_inc(&v->osc, inc);
}

/* Samples between cutoff updates (control rate: 2756 Hz at 44.1 kHz). */
#define FILTER_CTRL_SAMPLES 16

/* Recomputes the cutoff from the settings, the voice's note and its
 * filter envelope level `fenv`. */
static void update_cutoff(fm_voice *v, int32_t fenv)
{
    fm_filter_set_cutoff(&v->filter,
                         fm_filter_voice_cents(v->cutoff_cents, v->key_track,
                                               v->note, fenv, v->env_amount),
                         v->rate_cents);
}

void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t level, fenv, saw;
        if (v->amp_env.stage == FM_ENV_IDLE) {
            if (!v->pending) return;
            start(v); /* the stolen note has faded out */
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
        saw = fm_filter_process(&v->filter, fm_osc_saw_next(&v->osc));
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)saw * level) >> 30);
    }
}
