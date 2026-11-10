/*
 * voice.c - one note: oscillator and amp envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "voice.h"

void fm_voice_reset(fm_voice *v)
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
    fm_osc_set_inc(&v->osc, v->next_inc);
    fm_env_gate_on(&v->amp_env);
    if (!v->key_down && !v->sustained) v->gate_left = v->wait ? v->wait : 1;
}

/* The voice now plays `note` at `inc`, held by its key. */
static void hold(fm_voice *v, uint8_t note, uint32_t inc)
{
    v->note = note;
    v->key_down = 1;
    v->sustained = 0;
    v->gate_left = 0;
    v->next_inc = inc;
}

void fm_voice_note_on(fm_voice *v, uint8_t note, uint32_t inc,
                      uint32_t fade_samples)
{
    int same = fm_voice_active(v) && v->note == note;
    uint8_t old = v->note;
    hold(v, note, inc);
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
    hold(v, v->old_note, inc);
    start(v);
}

void fm_voice_change_note(fm_voice *v, uint8_t note, uint32_t inc,
                          int retrigger)
{
    hold(v, note, inc);
    if (v->pending) return; /* starts when the fade ends */
    fm_osc_set_inc(&v->osc, inc);
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE)
        fm_env_gate_on(&v->amp_env);
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
    if (!v->pending) fm_env_gate_off(&v->amp_env);
}

void fm_voice_silence(fm_voice *v)
{
    fm_env_reset(&v->amp_env);
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

void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t level, saw;
        if (v->amp_env.stage == FM_ENV_IDLE) {
            if (!v->pending) return;
            start(v); /* the stolen note has faded out */
        }
        if (v->gate_left && --v->gate_left == 0)
            fm_env_gate_off(&v->amp_env); /* the queued release */
        level = fm_env_next(&v->amp_env);
        saw = fm_osc_saw_next(&v->osc);
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)saw * level) >> 30);
    }
}
