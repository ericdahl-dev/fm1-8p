/*
 * voice.c - one note: oscillator pair and amp envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "voice.h"

void fm_voice_reset(fm_voice *v, uint32_t seed)
{
    fm_osc_pair_reset(&v->osc, seed);
    fm_env_reset(&v->amp_env);
    v->stamp = 0;
    v->pitch = 0;
    v->next_pitch = 0;
    v->wait = 0;
    v->gate_left = 0;
    v->pitch_oct = 0;
    v->pw_off = 0;
    v->lfo_fade = 0;
    v->lfo_wait = 0;
    v->lfo_restart = 0;
    v->next_peak = FM_ENV_FULL;
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

/* The oscillators apply the LFO's current offsets. */
static void apply_pitch(fm_voice *v)
{
    fm_osc_pair_set_pitch_mod(&v->osc, v->pitch_oct);
    fm_osc_pair_set_pw_mod(&v->osc, v->pw_off);
}

/*
 * A note starts from silence: the LFO's offsets drop to 0 and its delay
 * restarts at the next control tick. A note restruck while it still
 * sounds (the same note again, a take back, a unison retrigger) skips
 * this, so the vibrato carries on from where it is instead of jumping.
 */
static void restart_lfo(fm_voice *v)
{
    if (v->amp_env.stage != FM_ENV_IDLE) return;
    v->pitch_oct = 0;
    v->pw_off = 0;
    v->lfo_restart = 1;
}

/* Starts the note from the current level (0 when idle). A note whose
 * key came up while it waited for a steal fade queues its release for as
 * long as it waited, so it is heard rather than released at level 0. */
static void start(fm_voice *v, uint32_t sample_rate)
{
    v->pending = 0;
    restart_lfo(v);
    apply_pitch(v);
    v->pitch = v->next_pitch;
    fm_osc_pair_set_pitch(&v->osc, v->pitch, sample_rate);
    v->amp_env.peak = v->next_peak;
    fm_env_gate_on(&v->amp_env);
    if (!v->key_down && !v->sustained) v->gate_left = v->wait ? v->wait : 1;
}

/* The voice now plays `note` at `pitch`, held by its key. */
static void hold(fm_voice *v, uint8_t note, int32_t pitch)
{
    v->note = note;
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
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE) {
        restart_lfo(v); /* before the gate: it checks for silence */
        apply_pitch(v);
        v->amp_env.peak = v->next_peak;
        fm_env_gate_on(&v->amp_env);
    }
    v->pitch = pitch;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
}

void fm_voice_set_velocity(fm_voice *v, int32_t peak)
{
    v->next_peak = peak;
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

void fm_voice_set_pitch(fm_voice *v, int32_t pitch, uint32_t sample_rate)
{
    v->next_pitch = pitch;
    if (v->pending) return;
    v->pitch = pitch;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
}

void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m)
{
    int32_t pitch, pw;
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
    /* Q1.30 * Q0.32 -> Q0.32 */
    pw = (int32_t)(((int64_t)lfo * m->pw) >> 30);
    if (pitch != v->pitch_oct || pw != v->pw_off) {
        v->pitch_oct = pitch; /* a fading old note keeps its vibrato */
        v->pw_off = pw;
        apply_pitch(v);
    }
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
        int32_t level, osc;
        if (v->amp_env.stage == FM_ENV_IDLE) {
            if (!v->pending) return;
            start(v, sample_rate); /* the stolen note has faded out */
        }
        if (v->gate_left && --v->gate_left == 0)
            fm_env_gate_off(&v->amp_env); /* the queued release */
        level = fm_env_next(&v->amp_env);
        osc = fm_osc_pair_next(&v->osc);
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)osc * level) >> 30);
    }
}
