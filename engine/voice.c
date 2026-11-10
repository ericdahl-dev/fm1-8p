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
    v->pitch_oct = 0;
    v->pw_off = 0;
    v->lfo_start = 0;
    v->next_peak = FM_ENV_FULL;
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
    v->cutoff_now = INT32_MIN;
    v->cutoff_dirty = 1;
    v->ctrl_phase = 0;
    v->fenv_lag = 0;
}

void fm_voice_sync_filter_env(fm_voice *v)
{
    if (v->fenv_lag) {
        fm_env_advance(&v->filter_env, v->fenv_lag);
        v->fenv_lag = 0;
    }
}

static void filter_env_gate_off(fm_voice *v)
{
    fm_voice_sync_filter_env(v);
    fm_env_gate_off(&v->filter_env);
}

int fm_voice_active(const fm_voice *v)
{
    return v->amp_env.stage != FM_ENV_IDLE || v->pending;
}

/* Samples between cutoff updates (control rate: 2756 Hz at 44.1 kHz). */
#define FILTER_CTRL_SAMPLES 16

/* render_plain counts the control phase down modulo this */
typedef char fm_require_ctrl_samples_power_of_two[
    (FILTER_CTRL_SAMPLES & (FILTER_CTRL_SAMPLES - 1)) == 0 ? 1 : -1];

/* A skipped filter envelope is caught up at least this often (6 minutes
 * at 44.1 kHz), so fenv_lag never wraps. */
#define FENV_LAG_MAX (1u << 24)

/* Recomputes the cutoff from the settings, the voice's note and its
 * filter envelope level `fenv`. */
static void update_cutoff(fm_voice *v, int32_t fenv)
{
    int32_t cents = fm_filter_voice_cents(v->cutoff_cents, v->key_track,
                                          v->key_note, fenv, v->env_amount);
    v->cutoff_dirty = 0;
    if (cents == v->cutoff_now) return; /* the coefficients stand */
    v->cutoff_now = cents;
    fm_filter_set_cutoff(&v->filter, cents, v->rate_cents);
}

/* Gates both envelopes on. The filter envelope starts from where the amp
 * envelope does: from its level on a retrigger, from 0 on an idle voice
 * (fresh, or after a steal fade), whatever level it was left at. */
static void gate_on(fm_voice *v)
{
    if (v->amp_env.stage == FM_ENV_IDLE) {
        fm_env_reset(&v->filter_env);
        v->fenv_lag = 0;
    }
    fm_voice_sync_filter_env(v);
    fm_env_gate_on(&v->amp_env);
    fm_env_gate_on(&v->filter_env);
}

/* The oscillators apply the LFO's current offsets. */
static void apply_pitch(fm_voice *v)
{
    fm_osc_pair_set_pitch_mod(&v->osc, v->pitch_oct);
    fm_osc_pair_set_pw_mod(&v->osc, v->pw_off);
}

/*
 * A note starts from silence: the LFO's offsets drop to 0 and its delay
 * counts from control tick `now`, the first tick after the start. A note
 * restruck while it still sounds (the same note again, a take back, a
 * unison retrigger) skips this, so the vibrato carries on from where it
 * is instead of jumping.
 */
static void restart_lfo(fm_voice *v, uint32_t now)
{
    if (v->amp_env.stage != FM_ENV_IDLE) return;
    v->pitch_oct = 0;
    v->pw_off = 0;
    v->lfo_start = now;
}

/* Starts the note from the current level (0 when idle). A note whose
 * key came up while it waited for a steal fade queues its release for as
 * long as it waited, so it is heard rather than released at level 0. */
static void start(fm_voice *v, uint32_t sample_rate, int32_t peak,
                  uint32_t now)
{
    v->pending = 0;
    v->ctrl_phase = 0; /* the new note's cutoff from its first sample */
    restart_lfo(v, now);
    apply_pitch(v);
    v->pitch = v->next_pitch;
    v->key_note = v->next_key;
    v->cutoff_dirty = 1;
    fm_osc_pair_set_pitch(&v->osc, v->pitch, sample_rate);
    v->amp_env.peak = peak;
    gate_on(v); /* both envelopes */
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

void fm_voice_note_on(fm_voice *v, uint8_t note, int32_t pitch, int32_t peak,
                      uint32_t now, uint32_t sample_rate,
                      uint32_t fade_samples)
{
    int same = fm_voice_active(v) && v->note == note;
    uint8_t old = v->note;
    hold(v, note, pitch);
    v->next_peak = peak;
    if (v->pending) return; /* starts when the fade ends */
    if (same || v->amp_env.stage == FM_ENV_IDLE) {
        start(v, sample_rate, peak, now);
    } else {
        v->old_note = old;
        v->pending = 1;
        v->wait = fade_samples;
        fm_env_fade(&v->amp_env, fade_samples);
    }
}

void fm_voice_take_back(fm_voice *v, int32_t pitch, int32_t peak,
                        uint32_t now, uint32_t sample_rate)
{
    hold(v, v->old_note, pitch);
    start(v, sample_rate, peak, now); /* next_peak stays the dropped note's */
}

void fm_voice_change_note(fm_voice *v, uint8_t note, int32_t pitch,
                          int32_t peak, uint32_t now, uint32_t sample_rate,
                          int retrigger)
{
    hold(v, note, pitch);
    if (retrigger) v->next_peak = peak;
    if (v->pending) return; /* starts when the fade ends */
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE) {
        restart_lfo(v, now); /* before the gate: it checks for silence */
        apply_pitch(v);
        if (retrigger) v->amp_env.peak = peak; /* legato keeps its own */
        gate_on(v); /* both envelopes */
    }
    v->pitch = pitch;
    v->key_note = note;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
    /* the new note's cutoff at once; the 16-sample cadence carries on */
    update_cutoff(v, v->filter_env.level);
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
        filter_env_gate_off(v);
    }
}

void fm_voice_silence(fm_voice *v)
{
    fm_env_reset(&v->amp_env);
    fm_env_reset(&v->filter_env);
    v->fenv_lag = 0;
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

void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m,
                   uint32_t now)
{
    int32_t pitch, pw;
    uint32_t since;
    /* zero for delay_ticks, then up by fade_step a tick to full; kept as
     * a start tick, so a tick without modulation has nothing to count */
    since = now - v->lfo_start; /* wraps after 2^32 ticks (36 days) */
    if (m->delay_ticks && since < 2 * m->delay_ticks) {
        int64_t fade = since <= m->delay_ticks
                           ? 0
                           : (int64_t)(since - m->delay_ticks) * m->fade_step;
        if (fade < FM_LFO_FULL) /* Q1.30 * Q1.30 */
            lfo = (int32_t)(((int64_t)lfo * fade) >> 30);
    }
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

/*
 * The default patch's block: the filter open with the highpass off (a
 * wire), no envelope amount and nothing marking the cutoff, no note
 * waiting for a steal fade and no queued release. Then nothing about the
 * filter can change within the block, so the loop runs just the amp
 * envelope and the oscillators, and the filter's bookkeeping (its last
 * sample, the envelope's lag, the control phase) is done once at the end,
 * with the same result as the full loop below. Returns 1 if it ran.
 * (No call in the loop but the two every voice makes: one more costs a
 * voice about 15 %, as the compiler must then reload the voice's fields.)
 */
static int render_plain(fm_voice *v, int32_t *mix, uint32_t frames)
{
    int32_t last = v->filter.s[3];
    uint32_t i;
    if (v->env_amount || v->cutoff_dirty || v->pending || v->gate_left ||
        v->filter.fade || v->filter.hp_g ||
        v->amp_env.stage == FM_ENV_IDLE)
        return 0;
    for (i = 0; i < frames; i++) {
        int32_t level;
        if (v->amp_env.stage == FM_ENV_IDLE) break; /* the release ended */
        level = fm_env_next(&v->amp_env);
        /* the wire: the pair peaks at 1.0, inside the soft clip's
         * untouched +-2.0 (extreme_osc_settings_stay_bounded_and_release) */
        last = fm_osc_pair_next(&v->osc);
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)last * level) >> 30);
    }
    v->filter.s[3] = last;
    v->fenv_lag += i;
    /* each sample: 0 -> FILTER_CTRL_SAMPLES, then down one */
    v->ctrl_phase = (uint8_t)((v->ctrl_phase - i) & (FILTER_CTRL_SAMPLES - 1));
    if (v->fenv_lag >= FENV_LAG_MAX) fm_voice_sync_filter_env(v);
    return 1;
}

void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames,
                         uint32_t sample_rate, uint32_t next_tick,
                         uint32_t tick_left)
{
    uint32_t i;
    if (render_plain(v, mix, frames)) return;
    for (i = 0; i < frames; i++) {
        int32_t level, fenv, osc;
        if (v->amp_env.stage == FM_ENV_IDLE) {
            /* the stolen note faded: it starts here, before the ticks
             * still to come in this chunk (one at tick_left, then one
             * every FM_LFO_TICK_FRAMES) */
            uint32_t now = next_tick;
            if (!v->pending) return;
            if (i >= tick_left)
                now += ((i - tick_left) >> FM_LFO_TICK_SHIFT) + 1;
            start(v, sample_rate, v->next_peak, now);
        }
        if (v->gate_left && --v->gate_left == 0) { /* the queued release */
            fm_env_gate_off(&v->amp_env);
            filter_env_gate_off(v);
        }
        level = fm_env_next(&v->amp_env);
        if (v->env_amount) {
            fenv = fm_env_next(&v->filter_env);
        } else { /* no effect on the sound: caught up when needed */
            fenv = 0;
            v->fenv_lag++;
        }
        if (v->ctrl_phase == 0) {
            /* with no envelope amount the cutoff changes only when a
             * setting or the note does, which marks it dirty */
            if (v->env_amount || v->cutoff_dirty) update_cutoff(v, fenv);
            if (v->fenv_lag >= FENV_LAG_MAX) fm_voice_sync_filter_env(v);
            v->ctrl_phase = FILTER_CTRL_SAMPLES;
        }
        v->ctrl_phase--;
        osc = fm_filter_run(&v->filter, fm_osc_pair_next(&v->osc));
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)osc * level) >> 30);
    }
}
