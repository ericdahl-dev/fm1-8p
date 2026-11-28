/*
 * voice.c - one note: oscillator pair, filter and its envelope, and amp
 * envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifdef FM_CHECK_BUSY
#include <assert.h>
#endif
#include "voice.h"
#include "dyna.h"

/* Keeps a control-rate step inside the render loop that runs it: a call
 * there saves and restores every register, which costs more than the
 * step on the FM-1. */
#if defined(__GNUC__) || defined(__clang__)
#define FM_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define FM_ALWAYS_INLINE inline
#endif

/* Keeps a control-rate step out of the render loop: inline, its
 * registers crowd the per-sample path around it, which costs more on the
 * FM-1 than one call each 16 samples (measured for update_mix). */
#if defined(__GNUC__) || defined(__clang__)
#define FM_NOINLINE __attribute__((noinline))
#else
#define FM_NOINLINE
#endif

void fm_voice_reset(fm_voice *v, uint32_t seed, int32_t rate_cents,
                    const fm_env_settings *amp, const fm_env_settings *filter)
{
    fm_osc_pair_reset(&v->osc, seed);
    fm_env_init(&v->amp_env, amp);
    v->stamp = 0;
    v->pitch = 0;
    v->next_pitch = 0;
    fm_mod_reset(&v->mod);
    v->lfo_start = 0;
    v->velocity = 127;
    v->note = 0;
    v->key_down = 0;
    v->sustained = 0;
    fm_vfilter_reset(&v->vf, rate_cents);
    v->mix_base = 0;
    v->mix_step = 0;
    v->next_key = 0;
    fm_env_init(&v->filter_env, filter);
    fm_env_init(&v->env2, amp); /* its level is its share: peak is full */
    v->busy = FM_BUSY_CUTOFF;
}

/* With the gate (#63) the amp envelope runs the gate's settings, and
 * env2 the amp settings it was given. */
static int gated(const fm_voice *v)
{
    return v->amp_env.set != v->env2.set;
}

/* `n` samples pass for the two envelopes the render does not step (lazy:
 * each catches itself up when read or changed). envelope 2 is idle
 * without the gate, and an idle one stays idle. */
static inline void lazy_envs_pass(fm_voice *v, uint32_t n)
{
    fm_env_skip(&v->filter_env, n);
    fm_env_skip(&v->env2, n);
}

/* The filter envelope's level now (it is behind between control points). */
static int32_t filter_env_level(fm_voice *v)
{
    return fm_env_level(&v->filter_env);
}

/* The key is up for the filter envelope, and with the gate envelope 2. */
static void filter_env_gate_off(fm_voice *v)
{
    fm_env_gate_off(&v->filter_env);
    if (gated(v)) fm_env_gate_off(&v->env2);
}

int fm_voice_active(const fm_voice *v)
{
    return v->amp_env.stage != FM_ENV_IDLE;
}

/* The filter's settings changed: its busy bit follows whether it is now a
 * wire. The one place that sets FM_BUSY_FILTER. */
static void mark_filter(fm_voice *v)
{
    fm_voice_mark(v, FM_BUSY_FILTER, !fm_vfilter_is_wire(&v->vf));
}

void fm_voice_set_highpass(fm_voice *v, uint8_t step)
{
    fm_vfilter_set_highpass(&v->vf, step);
    mark_filter(v);
}

void fm_voice_set_cutoff_linear(fm_voice *v, int32_t linear_cents)
{
    fm_vfilter_set_base(&v->vf, linear_cents);
    fm_voice_mark(v, FM_BUSY_CUTOFF, 1);
}

void fm_voice_set_key_track(fm_voice *v, int32_t permille)
{
    fm_vfilter_set_key_track(&v->vf, permille);
    fm_voice_mark(v, FM_BUSY_CUTOFF, 1);
}

void fm_voice_set_output_level(fm_voice *v, int32_t permille)
{
    /* FM_ENV_FLOOR x 1000 / level: under 2^27, a 32-bit divide. At 0 the
     * output is silent: full, so a release frees the voice at once, as when
     * the level was the note's peak (0); a release so ended does not come
     * back if the level returns. A held note's decay is untouched (env.c) */
    fm_env_set_floor(&v->amp_env, permille > 0 ? FM_ENV_FLOOR * 1000 / permille : FM_ENV_FULL);
}

void fm_voice_set_resonance(fm_voice *v, int32_t permille)
{
    fm_vfilter_set_resonance(&v->vf, permille);
}

/*
 * The amp envelope's level `level` against the note's peak (Q1.30), so
 * velocity doesn't scale the depths that follow envelope 2 (the mix's,
 * the cutoff's). A divide, once a control point (16 samples), only when
 * one of them follows it.
 */
static int32_t amp_share(const fm_voice *v, int32_t level)
{
    int64_t r;
    if (v->amp_env.peak <= 0) return 0;
    r = ((int64_t)level << 30) / v->amp_env.peak;
    return r > FM_ENV_FULL ? FM_ENV_FULL : (int32_t)r;
}

/* Envelope 2 for the depths that follow it: the amp envelope's share at
 * its level `level`, or with the gate env2's own level (#63). */
static int32_t env2_share(fm_voice *v, int32_t level)
{
    if (!gated(v)) return amp_share(v, level);
    return fm_env_level(&v->env2);
}

/* Recomputes the cutoff from the settings, the voice's note and its
 * modulation, with the filter envelope (envelope 1) at level `fenv` and,
 * when it reaches the cutoff, envelope 2 at the amp envelope's share. The
 * envelope keeps the voice busy while it moves the cutoff, and until a
 * cutoff it no longer reaches is summed back to rest. */
static FM_ALWAYS_INLINE void update_cutoff(fm_voice *v, const fm_voice_mod *m,
                                           int32_t fenv)
{
    int32_t env2 = 0;
    if (m->routes.depth[FM_MOD_SRC_ENV2][FM_MOD_CUTOFF])
        env2 = env2_share(v, v->amp_env.level);
    if (m->fenv_dyna) { /* VCF DYNAMICS on either envelope: Q1.30 * Q15 */
        int32_t share = (int32_t)fm_dyna_share_q15(m->fenv_dyna, v->velocity);
        fenv = (int32_t)(((int64_t)fenv * share) >> 15);
        env2 = (int32_t)(((int64_t)env2 * share) >> 15);
    }
    fm_mod_set_source(&v->mod, FM_MOD_SRC_ENV1, fenv);
    if (m->routes.depth[FM_MOD_SRC_ENV2][FM_MOD_CUTOFF])
        fm_mod_set_source(&v->mod, FM_MOD_SRC_ENV2, env2);
    if (fm_vfilter_update(&v->vf, fm_mod_update(&v->mod, &m->routes,
                                                FM_MOD_CUTOFF)))
        mark_filter(v); /* else the coefficients stand */
    fm_voice_mark(v, FM_BUSY_FILTER_ENV,
                  !fm_mod_at_rest(&v->mod, &m->routes, FM_MOD_CUTOFF));
    fm_voice_mark(v, FM_BUSY_CUTOFF, 0);
}

/* Gates both envelopes on. The filter envelope starts from where the amp
 * envelope does: from its level on a retrigger or a steal, from 0 on an
 * idle voice, whatever level it was left at. */
static void gate_on(fm_voice *v)
{
    if (v->amp_env.stage == FM_ENV_IDLE) {
        fm_env_reset(&v->filter_env);
        fm_env_reset(&v->env2);
    }
    fm_env_key(&v->amp_env, v->note);
    fm_env_key(&v->filter_env, v->note);
    fm_env_gate_on(&v->amp_env);
    fm_env_gate_on(&v->filter_env);
    if (gated(v)) {
        fm_env_key(&v->env2, v->note);
        fm_env_gate_on(&v->env2);
    }
}

/* The oscillators apply the LFO's current offsets. */
static void apply_pitch(fm_voice *v)
{
    fm_osc_pair_set_pitch_mod(&v->osc, fm_mod_get(&v->mod, FM_MOD_PITCH1),
                              fm_mod_get(&v->mod, FM_MOD_PITCH2));
    fm_osc_pair_set_pw_mod(&v->osc, fm_mod_get(&v->mod, FM_MOD_PW));
}

/* Applies the modulation sums in `changed` (bit 1 << dest): pitch and
 * pulse width to the oscillators, level to the amp envelope's peak. */
static void apply_mod(fm_voice *v, uint32_t changed)
{
    if (changed & ((1u << FM_MOD_PITCH1) | (1u << FM_MOD_PITCH2) |
                   (1u << FM_MOD_PW)))
        apply_pitch(v);
    if (changed & (1u << FM_MOD_LEVEL))
        v->amp_env.peak = fm_mod_get(&v->mod, FM_MOD_LEVEL);
}

/* The note's velocity source is `peak`: summed with the voice's other
 * level sources, as they were last set, it sets the level the amp
 * envelope rises to. Only the level is taken again: the other sums (a
 * restruck note's vibrato) carry on, and a note on costs little code. */
static void set_level(fm_voice *v, const fm_voice_mod *m, int32_t peak)
{
    fm_mod_set_source(&v->mod, FM_MOD_SRC_VEL, peak);
    v->amp_env.peak = fm_mod_update(&v->mod, &m->routes, FM_MOD_LEVEL);
}

/*
 * A note starts from silence: the LFO's offsets drop to 0 and its delay
 * counts from control tick `now`, the first tick after the start. A note
 * restruck while it still sounds (the same note again, a unison retrigger)
 * skips this (a steal starts it again: fm_voice_note_on), so the vibrato carries on from where it
 * is instead of jumping.
 */
static void restart_lfo(fm_voice *v, uint32_t now)
{
    if (v->amp_env.stage != FM_ENV_IDLE) return;
    fm_mod_reset(&v->mod);
    v->lfo_start = now;
}

/* Starts the note from the current level (0 when idle). */
static void start(fm_voice *v, const fm_voice_mod *m, uint32_t sample_rate,
                  int32_t peak, uint8_t velocity, uint32_t now)
{
    v->velocity = velocity;
    fm_vfilter_due_next(&v->vf); /* the new note's cutoff from its first sample */
    restart_lfo(v, now);
    apply_pitch(v);
    v->pitch = v->next_pitch;
    fm_vfilter_set_key(&v->vf, v->next_key);
    fm_voice_mark(v, FM_BUSY_CUTOFF, 1);
    fm_osc_pair_set_pitch(&v->osc, v->pitch, sample_rate);
    set_level(v, m, peak);
    gate_on(v); /* both envelopes */
}

/* The voice now plays `note` at `pitch`, held by its key. */
static void hold(fm_voice *v, uint8_t note, int32_t pitch)
{
    v->note = note;
    v->next_key = note;
    v->key_down = 1;
    v->sustained = 0;
    v->next_pitch = pitch;
}

void fm_voice_note_on(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                      int32_t pitch, int32_t peak, uint8_t velocity,
                      uint32_t now, uint32_t sample_rate)
{
    if (fm_voice_active(v) && v->note != note) {
        /* a steal: the LFO starts again, as PG-8X's (#312); the envelopes'
         * sources carry on, as the envelopes do */
        fm_mod_set_source(&v->mod, FM_MOD_SRC_LFO, 0);
        fm_mod_tick(&v->mod, &m->routes);
        v->lfo_start = now;
    }
    hold(v, note, pitch);
    start(v, m, sample_rate, peak, velocity, now);
}

void fm_voice_change_note(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                          int32_t pitch, int32_t peak, uint8_t velocity,
                          uint32_t now, uint32_t sample_rate, int retrigger)
{
    hold(v, note, pitch);
    if (retrigger || v->amp_env.stage == FM_ENV_IDLE) {
        restart_lfo(v, now); /* before the gate: it checks for silence */
        apply_pitch(v);
        if (retrigger) { /* legato keeps its own */
            set_level(v, m, peak);
            v->velocity = velocity;
        }
        gate_on(v); /* both envelopes */
    }
    v->pitch = pitch;
    fm_vfilter_set_key(&v->vf, note);
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
    /* the new note's cutoff at once; the 16-sample cadence carries on */
    update_cutoff(v, m, filter_env_level(v));
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
    fm_env_gate_off(&v->amp_env);
    filter_env_gate_off(v); /* and env2 */
}

int fm_voice_let_go(fm_voice *v)
{
    int held = v->key_down || v->sustained;
#ifdef FM_CHECK_ENGINE /* debug builds: a key or the pedal holds only a sounding voice */
    assert(!held || fm_voice_active(v));
#endif
    if (held) fm_voice_release(v);
    if (fm_voice_active(v)) v->note = FM_VOICE_NO_NOTE;
    return held;
}

/*
 * Aims osc2's level at osc2_level plus its modulation, with envelope 1
 * (the filter envelope) at `env1` and envelope 2 (the amp envelope's share) at `env2`,
 * reached by the next control tick: it glides there a sample at a time
 * (no click when the depth changes, no steps as the envelope moves).
 */
static FM_NOINLINE void update_mix(fm_voice *v, const fm_voice_mod *m,
                                   int32_t env1, int32_t env2)
{
    int64_t mix;
    if (m->mix_dyna) { /* MIX DYNAMICS: the depth times the share (Q15) */
        uint32_t s = fm_dyna_share_q15(m->mix_dyna, v->velocity);
        env1 = (int32_t)(((int64_t)env1 * s) >> 15);
        env2 = (int32_t)(((int64_t)env2 * s) >> 15);
    }
    fm_mod_set_source(&v->mod, FM_MOD_SRC_ENV1, env1);
    fm_mod_set_source(&v->mod, FM_MOD_SRC_ENV2, env2);
    mix = (int64_t)v->mix_base + fm_mod_update(&v->mod, &m->routes, FM_MOD_MIX);
    if (mix < 0) mix = 0;
    if (mix > FM_OSC_LEVEL_FULL) mix = FM_OSC_LEVEL_FULL;
    v->mix_step = ((int32_t)mix - v->osc.req[1]) / FM_VFILTER_CTRL_SAMPLES;
    if (!v->mix_step) /* within a glide's rounding: there now */
        fm_osc_pair_set_level(&v->osc, 1, (int32_t)mix);
    fm_voice_mark_mix(v, m);
}

void fm_voice_silence(fm_voice *v)
{
    fm_env_reset(&v->amp_env);
    fm_env_reset(&v->filter_env);
    fm_env_reset(&v->env2);
    fm_vfilter_clear(&v->vf); /* no ringing left for the next note */
    v->key_down = 0;
    v->sustained = 0;
}

void fm_voice_set_amp(fm_voice *v, const fm_env_settings *amp)
{
    if (v->amp_env.set == amp) return;
    v->amp_env.set = amp;
    if (v->amp_env.stage != FM_ENV_IDLE) fm_env_retime(&v->amp_env);
    if (!gated(v)) fm_env_reset(&v->env2); /* idle without the gate */
}

void fm_voice_set_pitch(fm_voice *v, int32_t pitch, uint32_t sample_rate)
{
    v->next_pitch = pitch;
    v->pitch = pitch;
    fm_osc_pair_set_pitch(&v->osc, pitch, sample_rate);
}

void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m,
                   uint32_t now)
{
    uint32_t since;
    /* zero for delay_ticks, then up by fade_step a tick to full at 3 x delay_ticks (#384); kept as
     * a start tick, so a tick without modulation has nothing to count */
    since = now - v->lfo_start; /* wraps after 2^32 ticks (36 days) */
    if (m->delay_ticks && since < 3 * m->delay_ticks) {
        int64_t fade = since <= m->delay_ticks
                           ? 0
                           : (int64_t)(since - m->delay_ticks) * m->fade_step;
        if (fade < FM_LFO_FULL) /* Q1.30 * Q1.30 */
            lfo = (int32_t)(((int64_t)lfo * fade) >> 30);
    }
    fm_mod_set_source(&v->mod, FM_MOD_SRC_LFO, lfo);
    if (fm_mod_source_routed(&m->routes, FM_MOD_SRC_PENV))
    {
        int32_t penv = m->penv_amp ? env2_share(v, v->amp_env.level)
                                   : filter_env_level(v);
        if (m->penv_dyna) /* Q1.30 * Q15 share */
            penv = (int32_t)(((int64_t)penv *
                              fm_dyna_share_q15(m->penv_dyna, v->velocity)) >> 15);
        fm_mod_set_source(&v->mod, FM_MOD_SRC_PENV, penv);
    }
    apply_mod(v, fm_mod_tick(&v->mod, &m->routes));
}

void fm_voice_set_tune(fm_voice *v, uint8_t which, int32_t cents,
                       uint32_t sample_rate)
{
    fm_osc_pair_set_tune(&v->osc, which, cents, v->pitch, sample_rate);
}

uint32_t fm_voice_busy_derived(const fm_voice *v, const fm_voice_mod *m)
{
    return !fm_mod_at_rest(&v->mod, &m->routes, FM_MOD_CUTOFF) ||
           !fm_mod_at_rest(&v->mod, &m->routes, FM_MOD_MIX) ||
           v->mix_step || v->osc.req[1] != v->mix_base ||
           !fm_vfilter_is_wire(&v->vf) || !fm_vfilter_at_rest(&v->vf);
}

/*
 * 1 if the oscillators can render the whole chunk at once
 * (fm_osc_pair_render): the voice plays all of it and nothing in it can
 * change them. Not while the amp envelope is idle or in its release (the
 * voice stops mid-chunk), nor with the mix moving (FM_BUSY_MIX).
 * Otherwise the oscillators run a sample at a time, as far as the voice
 * plays. The one rule for both render loops.
 */
static int osc_chunk_safe(const fm_voice *v)
{
    uint8_t stage = v->amp_env.stage;
    return stage != FM_ENV_IDLE && stage != FM_ENV_RELEASE &&
           !(fm_voice_busy(v) & FM_BUSY_MIX);
}

/*
 * A block with every processor at rest (fm_voice_busy 0, as in the default
 * patch): the filter a wire, no envelope amount and nothing marking the
 * cutoff, osc2 at osc2_level. Then nothing
 * about the filter can change within the block, so the loop runs just the
 * amp envelope and the oscillators, and the filter's bookkeeping (its last
 * sample, the envelope's lag, the control phase) is done once at the end,
 * with the same result as the full loop below. Returns 1 if it ran. (No
 * call in the loop but the two every voice makes: one more costs a voice
 * about 15 %, as the compiler must then reload the voice's fields.)
 */
static int render_plain(fm_voice *v, const fm_voice_mod *m, int32_t *mix,
                        int32_t *osc, uint32_t frames)
{
    int32_t last = 0; /* kept by the wire only if a sample ran */
    uint32_t i;
    (void)m; /* read by the FM_CHECK_BUSY check only */
    if (fm_voice_busy(v) || v->amp_env.stage == FM_ENV_IDLE)
        return 0;
#ifdef FM_CHECK_BUSY
    assert(!fm_voice_busy_derived(v, m)); /* the busy word hid a processor */
#endif
    if (osc_chunk_safe(v) && frames) {
        /* the note plays the whole chunk: the oscillators all at once */
        fm_osc_pair_render(&v->osc, osc, frames);
        for (i = 0; i < frames; i++)
            mix[i] += (int32_t)(((int64_t)osc[i] * fm_env_step(&v->amp_env)) >> 30);
        last = osc[frames - 1];
    } else for (i = 0; i < frames; i++) {
        int32_t level;
        if (v->amp_env.stage == FM_ENV_IDLE) break; /* the release ended */
        level = fm_env_step(&v->amp_env);
        /* the wire: the pair peaks at 2.0 (both levels full; a ring's up to 3.5, held at 4.0),
         * within the filter's hold (+-8.0), sync and cross-mod included
         * (extreme_osc_settings_stay_bounded_and_release,
         * max_xmod_and_sync_stay_in_range) */
        last = fm_osc_pair_next(&v->osc);
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)last * level) >> 30);
    }
    fm_vfilter_pass(&v->vf, last, i);
    lazy_envs_pass(v, i);
    return 1;
}

void fm_voice_render_add(fm_voice *v, const fm_voice_mod *m,
                         int32_t *mix, int32_t *osc,
                         uint32_t frames)
{
    uint32_t i, told = 0; /* the lazy envelopes know of samples < told */
    int whole;
    if (render_plain(v, m, mix, osc, frames)) return;
    whole = osc_chunk_safe(v);
    if (whole) fm_osc_pair_render(&v->osc, osc, frames);
    for (i = 0; i < frames; i++) {
        int32_t level, fenv, out;
        if (v->amp_env.stage == FM_ENV_IDLE) { /* the release ended */
            lazy_envs_pass(v, i - told);
            return;
        }
        level = fm_env_step(&v->amp_env);
        if (fm_vfilter_due(&v->vf)) {
            /* the filter envelope is read only at the control rate: told
             * of the samples so far, this one too, and caught up exactly
             * when read */
            lazy_envs_pass(v, i + 1 - told);
            told = i + 1;
            fenv = 0;
            if ((fm_voice_busy(v) & FM_BUSY_FILTER_ENV) ||
                m->routes.depth[FM_MOD_SRC_ENV1][FM_MOD_MIX])
                fenv = filter_env_level(v);
            /* with no envelope amount the cutoff changes only when a
             * setting or the note does, which sets FM_BUSY_CUTOFF */
            if (fm_voice_busy(v) & (FM_BUSY_FILTER_ENV | FM_BUSY_CUTOFF))
                update_cutoff(v, m, fenv);
            /* the mix envelope, or a glide back to osc2_level after it (until
             * it lands: a glide can land exactly with its step still set) */
            if (fm_voice_busy(v) & FM_BUSY_MIX) /* the divide only if routed */
                update_mix(v, m, fenv,
                           m->routes.depth[FM_MOD_SRC_ENV2][FM_MOD_MIX] ?
                           env2_share(v, level) : 0);
        }
        if (v->mix_step)
            fm_osc_pair_set_level(&v->osc, 1, v->osc.req[1] + v->mix_step);
        out = fm_vfilter_run(&v->vf, whole ? osc[i] : fm_osc_pair_next(&v->osc));
        /* Q8.24 * Q1.30 -> Q8.24 */
        mix[i] += (int32_t)(((int64_t)out * level) >> 30);
    }
    lazy_envs_pass(v, frames - told);
}
