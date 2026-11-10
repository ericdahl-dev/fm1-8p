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
    v->base_inc = 0;
    v->pitch_oct = 0;
    v->lfo_fade = 0;
    v->lfo_wait = 0;
    v->lfo_restart = 0;
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

/*
 * 2^x for x in Q16.16 octaves, times `inc`, capped just under Nyquist.
 * The fraction uses a quartic fit of 2^f on [0, 1) (Q30 coefficients,
 * under 0.011 cent off); whole octaves are shifts. No division.
 */
static uint32_t mod_inc(uint32_t inc, int32_t x)
{
    static const int64_t c[4] = { 744149770, 259040010, 56213420, 14325366 };
    int32_t k;
    int64_t f, acc;
    uint64_t out;
    if (x == 0) return inc;
    k = x >> 16; /* floor: arithmetic shift */
    f = x & 0xFFFF;
    acc = c[3];
    acc = ((acc * f) >> 16) + c[2];
    acc = ((acc * f) >> 16) + c[1];
    acc = ((acc * f) >> 16) + c[0];
    acc = (1 << 30) + ((acc * f) >> 16); /* 2^f, Q2.30, 1.0..2.0 */
    out = ((uint64_t)inc * (uint64_t)acc) >> 30;
    if (k > 0) out = k > 8 ? FM_OSC_MAX_INC : out << k;
    else if (k < 0) out = k < -31 ? 0 : out >> -k;
    return out > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)out;
}

/* The oscillator plays `base_inc` with the LFO offset applied. */
static void apply_pitch(fm_voice *v)
{
    fm_osc_set_inc(&v->osc, mod_inc(v->base_inc, v->pitch_oct));
}

/* A note starts: the LFO's pitch offset drops to 0 and its delay
 * restarts at the next control tick. */
static void restart_lfo(fm_voice *v)
{
    v->pitch_oct = 0;
    v->lfo_restart = 1;
}

/* Starts the note from the current level (0 when idle). A note whose
 * key came up while it waited for a steal fade queues its release for as
 * long as it waited, so it is heard rather than released at level 0. */
static void start(fm_voice *v)
{
    v->pending = 0;
    v->base_inc = v->next_inc;
    restart_lfo(v);
    apply_pitch(v);
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
    v->base_inc = inc;
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE) {
        fm_env_gate_on(&v->amp_env);
        restart_lfo(v);
    }
    apply_pitch(v);
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
    if (v->pending) return;
    v->base_inc = inc;
    apply_pitch(v);
}

void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m)
{
    int32_t pitch;
    if (v->lfo_restart) {
        v->lfo_restart = 0;
        v->lfo_wait = m->delay_ticks;
        v->lfo_fade = m->delay_ticks ? 0 : FM_LFO_FULL;
    } else if (v->lfo_wait) {
        v->lfo_wait--;
    } else if (v->lfo_fade < FM_LFO_FULL) {
        v->lfo_fade = FM_LFO_FULL - v->lfo_fade <= m->fade_step
                          ? FM_LFO_FULL : v->lfo_fade + m->fade_step;
    }
    if (v->lfo_fade != FM_LFO_FULL) /* Q1.30 * Q1.30 */
        lfo = (int32_t)(((int64_t)lfo * v->lfo_fade) >> 30);
    /* Q1.30 * Q16.16 octaves -> Q16.16 octaves */
    pitch = (int32_t)(((int64_t)lfo * m->pitch_oct) >> 30);
    if (pitch != v->pitch_oct) {
        v->pitch_oct = pitch; /* a fading old note keeps its vibrato */
        apply_pitch(v);
    }
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
