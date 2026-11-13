/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "chorus.h"
#include "dyna.h"
#include "fm1_8p.h"
#include "units.h"
#ifdef FM_CHECK_ENGINE
#include <assert.h>
#endif

/* Below 8 kHz slow LFO and delay settings lose precision. */
#define MIN_SAMPLE_RATE 8000u
#define MAX_SAMPLE_RATE 192000u
/* A stolen voice fades out over this long before its new note starts. */
#define STEAL_FADE_MS 3

/*
 * The engine relies on >> of a negative value being an arithmetic shift
 * (implementation-defined in C99; true for GCC, Clang and the JieLi
 * toolchain). Refuse to build where it is not.
 */
typedef char fm_require_arithmetic_shift[((-8 >> 1) == -4) ? 1 : -1];

/*
 * fm_engine's budget on 32-bit targets (the FM-1, Cortex-M), where it
 * lives: the size fm1_8p.h documents. Growing it must update that comment
 * and this number. The FM-1 build fails past it; 64-bit desktops only
 * have the loose guard in tests/test_chorus.c (pointers there are 8 bytes).
 */
#define FM_ENGINE_BUDGET_32 6008u
typedef char fm_require_engine_within_its_32_bit_budget[
    (sizeof(void *) != 4 || sizeof(fm_engine) <= FM_ENGINE_BUDGET_32) ? 1 : -1];

/* Pitch of voice `i` playing `note`, Q16.16 semitones: the note, the
 * bend and the voice's unison detune. Both oscillators follow it. */
static int32_t voice_pitch(const fm_engine *e, uint8_t i, uint8_t note)
{
    return ((int32_t)note << 16) + e->bend + e->detune[i];
}

/*
 * Unison detune of voice `i`, Q16.16 semitones: the voices spread evenly
 * over `unison_detune_cents`, lowest to highest, centered on the note.
 * cents * 65536 / 100 * (2i - (n - 1)) / (2 (n - 1)), at most
 * 100 * 65536 * 7 before the divide, inside int32_t; the divisor is at
 * least 200.
 */
static int32_t unison_detune(const fm_engine *e, uint8_t i)
{
    int32_t n = e->voice_count;
    if (n < 2) return 0;
    return e->params[FM_PARAM_UNISON_DETUNE_CENTS] * 65536 *
           (2 * (int32_t)i - (n - 1)) / (200 * (n - 1));
}

/* Retunes every sounding voice after the bend changes. A stolen voice's old note
 * keeps its pitch for the few ms it takes to fade. */
static void retune(fm_engine *e)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) /* idle voices get theirs at note on */
        if (fm_voice_active(&e->voices[i]) &&
            e->voices[i].note != FM_VOICE_NO_NOTE)
            fm_voice_set_pitch(&e->voices[i], voice_pitch(e, i, e->voices[i].note),
                               e->sample_rate);
}

/* MIDI bend (-8192..8191 from center) to Q16.16 semitones: exact, since
 * value / 8192 * range * 65536 = value * range * 8. */
static void apply_bend(fm_engine *e, int32_t centered)
{
    e->bend = centered * e->params[FM_PARAM_BEND_RANGE] * 8;
    retune(e);
}

/*
 * Oscillator `which`'s settings on every voice. Each oscillator's
 * parameters run in the same order (wave, pulse width, octave, fine), so
 * `rel` is the offset from its wave parameter. The typedefs below fail
 * the build if that order ever changes. osc2's coarse tune is outside that
 * block (it came later): it retunes through retune_osc like octave and fine.
 */
typedef char fm_require_osc1_width_at_1[
    FM_PARAM_OSC1_PULSE_WIDTH_PCT - FM_PARAM_OSC1_WAVE == 1 ? 1 : -1];
typedef char fm_require_osc1_octave_at_2[
    FM_PARAM_OSC1_OCTAVE - FM_PARAM_OSC1_WAVE == 2 ? 1 : -1];
typedef char fm_require_osc1_fine_at_3[
    FM_PARAM_OSC1_FINE_CENTS - FM_PARAM_OSC1_WAVE == 3 ? 1 : -1];
typedef char fm_require_osc2_width_at_1[
    FM_PARAM_OSC2_PULSE_WIDTH_PCT - FM_PARAM_OSC2_WAVE == 1 ? 1 : -1];
typedef char fm_require_osc2_octave_at_2[
    FM_PARAM_OSC2_OCTAVE - FM_PARAM_OSC2_WAVE == 2 ? 1 : -1];
typedef char fm_require_osc2_fine_at_3[
    FM_PARAM_OSC2_FINE_CENTS - FM_PARAM_OSC2_WAVE == 3 ? 1 : -1];

/* Oscillator `which`'s offset from the played note, in cents. */
static int32_t osc_tune_cents(const fm_engine *e, uint8_t which)
{
    const int32_t *p = &e->params[which ? FM_PARAM_OSC2_WAVE : FM_PARAM_OSC1_WAVE];
    int32_t cents = p[2] * 1200 + p[3];
    if (which)
        cents += e->params[FM_PARAM_OSC2_COARSE_SEMIS] * 100;
    return cents;
}

/* Retunes oscillator `which` on every voice to its octave, coarse and fine. */
static void retune_osc(fm_engine *e, uint8_t which)
{
    int32_t cents = osc_tune_cents(e, which);
    uint8_t i;
    for (i = 0; i < FM_MAX_VOICES; i++)
        fm_voice_set_tune(&e->voices[i], which, cents, e->sample_rate);
}

static void apply_osc_param(fm_engine *e, uint8_t which, int rel)
{
    const int32_t *p = &e->params[which ? FM_PARAM_OSC2_WAVE : FM_PARAM_OSC1_WAVE];
    uint8_t i;
    if (rel >= 2) { /* octave or fine */
        retune_osc(e, which);
        return;
    }
    for (i = 0; i < FM_MAX_VOICES; i++) {
        fm_voice *v = &e->voices[i];
        switch (rel) {
        case 0:
            fm_osc_pair_set_wave(&v->osc, which, (uint8_t)p[0]);
            break;
        default: /* 1, the pulse width */
            fm_osc_pair_set_width(&v->osc, which, p[1]);
            break;
        }
    }
}

/*
 * An envelope time or sustain: written once into the settings every
 * voice's envelope of that kind shares (#19), then each running one is
 * retimed; only that kind, since a retime restarts the running segment
 * from its level. An idle envelope needs no retime: its next gate on
 * reads the settings. Returns 0 for any other parameter.
 */
static int apply_env_param(fm_engine *e, uint16_t id, int32_t v)
{
    uint32_t *time = 0; /* the setting: a time in samples, */
    int32_t *sustain = 0; /* or a sustain level */
    int filter = 0;
    uint8_t i;
    switch (id) {
    case FM_PARAM_AMP_ATTACK_MS: time = &e->amp_set.attack_samples; break;
    case FM_PARAM_AMP_DECAY_MS: time = &e->amp_set.decay_samples; break;
    case FM_PARAM_AMP_SUSTAIN: sustain = &e->amp_set.sustain; break;
    case FM_PARAM_AMP_RELEASE_MS: time = &e->amp_set.release_samples; break;
    case FM_PARAM_AMP_KEY_FOLLOW: /* read at the next note on */
        e->amp_set.key_follow = v;
        return 1;
    case FM_PARAM_FILTER_ENV_KEY_FOLLOW:
        e->filter_set.key_follow = v;
        return 1;
    case FM_PARAM_FILTER_ENV_ATTACK_MS:
        time = &e->filter_set.attack_samples, filter = 1;
        break;
    case FM_PARAM_FILTER_ENV_DECAY_MS:
        time = &e->filter_set.decay_samples, filter = 1;
        break;
    case FM_PARAM_FILTER_ENV_SUSTAIN:
        sustain = &e->filter_set.sustain, filter = 1;
        break;
    case FM_PARAM_FILTER_ENV_RELEASE_MS:
        time = &e->filter_set.release_samples, filter = 1;
        break;
    default:
        return 0;
    }
    if (filter) /* each voice's envelope current before it changes */
        for (i = 0; i < e->voice_count; i++)
            fm_voice_sync_filter_env(&e->voices[i]);
    if (time)
        *time = fm_ms_to_samples(v, e->sample_rate);
    else
        *sustain = fm_permille_to_q30(v);
    for (i = 0; i < e->voice_count; i++) {
        fm_env *env = filter ? &e->voices[i].filter_env : &e->voices[i].amp_env;
        /* an idle one leaves IDLE only by fm_env_gate_on, which reads
         * the settings and recomputes sus_level itself */
        if (env->stage != FM_ENV_IDLE)
            fm_env_retime(env);
    }
    return 1;
}

/*
 * setting_glide (#146): PG-8X 2.0.13 glides these settings to a new value
 * as a one-pole of about 16 ms (measured mid-note through DawDreamer
 * automation: VCA LEVEL 15.9 ms, MIX DCO-2 16.7 / 15.7, VCF FREQ 16.4 /
 * 15.9, VCF ENV, KEY and RES alike, the LFO depth about 20). From init
 * they start at PG-8X's power-on values, so a patch set before the first
 * render glides in from them, as PG-8X's renders start.
 */
static const uint8_t glide_id[FM_GLIDE_SETTINGS] = {
    FM_PARAM_OSC1_LEVEL, FM_PARAM_OSC2_LEVEL, FM_PARAM_FILTER_CUTOFF_CENTS,
    FM_PARAM_FILTER_RESONANCE, FM_PARAM_FILTER_KEY_TRACK, FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
    FM_PARAM_LFO_PITCH_CENTS, FM_PARAM_LFO_PITCH_OSC2_CENTS
};
/* PG-8X's power-on values in engine units: DCO-1 at full mix, DCO-2 off,
 * VCF FREQ 127 (tools/jx8p.py cutoff_cents), the rest 0 */
static const int16_t glide_power_on[FM_GLIDE_SETTINGS] = { 1000, 0, 14728, 0, 0, 0, 0, 0 };
#define GLIDE_TAU_MS 16
/* a glide steps every 4 control ticks (2.7 ms at 48 kHz, 6 steps a tau):
 * each step re-applies the settings that moved to every voice */
#define GLIDE_TICKS 4

static int glide_index(uint16_t id)
{
    int k;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++)
        if (glide_id[k] == id) return k;
    return -1;
}

/* Where setting k is going: its parameter (osc 2's LFO depth -1 is osc
 * 1's). */
static int32_t glide_target(const fm_engine *e, int k)
{
    int32_t v = e->params[glide_id[k]];
    if (glide_id[k] == FM_PARAM_LFO_PITCH_OSC2_CENTS && v < 0)
        v = e->params[FM_PARAM_LFO_PITCH_CENTS];
    return v;
}

/* The value a setting plays at now: where its glide is, or (no glide)
 * its parameter. */
static int32_t live_value(const fm_engine *e, uint16_t id)
{
    int k;
    if (!e->params[FM_PARAM_SETTING_GLIDE] || (k = glide_index(id)) < 0)
        return e->params[id];
    return (e->glide_cur[k] + 128) >> 8;
}

/* Nonzero while a setting is still on its way. */
static int glide_live(const fm_engine *e)
{
    int k;
    if (!e->params[FM_PARAM_SETTING_GLIDE]) return 0;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++)
        if (e->glide_cur[k] != glide_target(e, k) * 256) return 1;
    return 0;
}

/* One step of 1 - exp(-x), x = a step / tau, Q16, from its series in 32
 * bits (a step of GLIDE_TICKS ticks is 1 tau at 8 kHz, where the series'
 * fourth term, dropped, is 1/24). */
static void glide_init(fm_engine *e)
{
    int k;
    int32_t x = (int32_t)((uint32_t)FM_LFO_TICK_FRAMES * GLIDE_TICKS * (1000u * 65536u / GLIDE_TAU_MS) /
                          e->sample_rate);
    int32_t x2 = ((x >> 4) * (x >> 4)) >> 8, x3 = ((x2 >> 4) * (x >> 4)) >> 8;
    e->glide_k = x - x2 / 2 + x3 / 6;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++)
        e->glide_cur[k] = glide_power_on[k] * 256;
}

static void apply_param(fm_engine *e, uint16_t id);

/* At a control tick: each setting one step on its way; what moves is
 * applied as its parameter would be. */
static void glide_tick(fm_engine *e)
{
    int k;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++) {
        int32_t *cur = &e->glide_cur[k], d = glide_target(e, k) * 256 - *cur, was = *cur + 128;
        if (!d) continue;
        *cur += (uint32_t)d + 255u < 511u ? d : ((d >> 8) * e->glide_k) >> 8; /* under a unit: there */
        if (((*cur + 128) ^ was) >> 8) apply_param(e, glide_id[k]);    /* its value moved */
    }
}

/* Recomputes the derived settings a parameter feeds. */
static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = live_value(e, id);
    uint8_t i;
    switch (id) {
    case FM_PARAM_BEND_RANGE:
        apply_bend(e, e->bend_value);
        return;
    case FM_PARAM_SETTING_GLIDE: { /* the settings play at their glides now, or not */
        int k;
        for (k = 0; k < FM_GLIDE_SETTINGS; k++)
            apply_param(e, glide_id[k]);
        return;
    }
    case FM_PARAM_OSC1_WAVE:
    case FM_PARAM_OSC1_PULSE_WIDTH_PCT:
    case FM_PARAM_OSC1_OCTAVE:
    case FM_PARAM_OSC1_FINE_CENTS:
        apply_osc_param(e, 0, id - FM_PARAM_OSC1_WAVE);
        return;
    case FM_PARAM_OSC2_WAVE:
    case FM_PARAM_OSC2_PULSE_WIDTH_PCT:
    case FM_PARAM_OSC2_OCTAVE:
    case FM_PARAM_OSC2_FINE_CENTS:
        apply_osc_param(e, 1, id - FM_PARAM_OSC2_WAVE);
        return;
    case FM_PARAM_OSC2_COARSE_SEMIS:
        retune_osc(e, 1);
        return;
    case FM_PARAM_OSC_XMOD:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_xmod(&e->voices[i].osc, fm_permille_to_q30(v));
        return;
    case FM_PARAM_OSC_SYNC:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_sync(&e->voices[i].osc, (uint8_t)v);
        return;
    case FM_PARAM_OSC_RING:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_ring(&e->voices[i].osc, (uint8_t)v);
        return;
    case FM_PARAM_OSC1_LEVEL:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_level(&e->voices[i].osc, 0, fm_permille_to_q30(v));
        return;
    case FM_PARAM_OSC2_LEVEL:
        for (i = 0; i < FM_MAX_VOICES; i++) {
            fm_voice *vc = &e->voices[i];
            vc->mix_base = fm_permille_to_q30(v);
            /* with an envelope routed the next tick glides there */
            if (!e->mod.routes.depth[FM_MOD_SRC_ENV1][FM_MOD_MIX] &&
                !e->mod.routes.depth[FM_MOD_SRC_ENV2][FM_MOD_MIX]) {
                vc->mix_step = 0;
                fm_osc_pair_set_level(&vc->osc, 1, vc->mix_base);
            }
            fm_voice_mark_mix(vc, &e->mod);
        }
        return;
    case FM_PARAM_OSC_MIX_ENV_SOURCE: /* the filter envelope is caught up when read */
    case FM_PARAM_OSC_MIX_ENV: {
        /* one route to the mix: the source's envelope at the depth; off,
         * osc2's level glides back to osc2_level */
        int32_t depth = fm_permille_to_q30(e->params[FM_PARAM_OSC_MIX_ENV]);
        int amp = e->params[FM_PARAM_OSC_MIX_ENV_SOURCE] != 0;
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV1, FM_MOD_MIX,
                         amp ? 0 : depth);
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV2, FM_MOD_MIX,
                         amp ? depth : 0);
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_voice_mark_mix(&e->voices[i], &e->mod);
        return;
    }
    default:
        break;
    }
    switch (id) {
    case FM_PARAM_LFO_WAVE:
        fm_lfo_set_wave(&e->lfo, (uint8_t)v);
        return;
    case FM_PARAM_LFO_RATE_CENTIHZ:
        fm_lfo_set_rate(&e->lfo, v, e->sample_rate);
        return;
    case FM_PARAM_LFO_DELAY_MS:
        e->mod.delay_ticks = fm_ms_to_samples(v, e->sample_rate) >> FM_LFO_TICK_SHIFT;
        /* fades in over as long as the delay; the divisor is at least 1 */
        e->mod.fade_step = e->mod.delay_ticks
                               ? (int32_t)(FM_LFO_FULL / e->mod.delay_ticks)
                               : FM_LFO_FULL;
        return;
    case FM_PARAM_LFO_PW_PCT:
        /* percent -> Q0.32, at most 0.49 */
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PW,
                         v * 42949673);
        return;
    case FM_PARAM_LFO_PITCH_CENTS:
    case FM_PARAM_LFO_PITCH_OSC2_CENTS: {
        /* cents -> Q16.16 octaves: exact for multiples of 75 cents. Osc 2
         * has its own depth, or (-1) osc 1's */
        int32_t c1 = live_value(e, FM_PARAM_LFO_PITCH_CENTS);
        int32_t c2 = live_value(e, FM_PARAM_LFO_PITCH_OSC2_CENTS);
        if (c2 < 0) c2 = c1;
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PITCH1,
                         c1 * 65536 / 1200);
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PITCH2,
                         c2 * 65536 / 1200);
        return;
    }
    case FM_PARAM_PITCH_ENV_OSC1_CENTS: /* cents -> Q16.16 octaves */
    case FM_PARAM_PITCH_ENV_OSC2_CENTS:
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_PENV,
                         id == FM_PARAM_PITCH_ENV_OSC1_CENTS ? FM_MOD_PITCH1
                                                             : FM_MOD_PITCH2,
                         v * 65536 / 1200);
        return;
    case FM_PARAM_PITCH_ENV_SOURCE: /* read at the next tick */
        e->mod.penv_amp = (uint8_t)(v != 0);
        return;
    case FM_PARAM_PITCH_ENV_DYNA: /* read at the next tick */
        e->mod.penv_dyna = (uint8_t)v;
        return;
    case FM_PARAM_FILTER_ENV_DYNA: /* read at the next cutoff update */
        e->mod.fenv_dyna = (uint8_t)v;
        return;
    case FM_PARAM_OSC_MIX_ENV_DYNA: /* read at the next tick */
        e->mod.mix_dyna = (uint8_t)v;
        return;
    case FM_PARAM_AMP_VELOCITY: /* read at the next note on */
    case FM_PARAM_AMP_DYNA:
        return;
    default:
        break;
    }
    if (id == FM_PARAM_VOICE_MODE || id == FM_PARAM_UNISON_DETUNE_CENTS) {
        /* every voice's detune: the unison spread, or none in poly */
        int unison = e->params[FM_PARAM_VOICE_MODE] == FM_VOICE_MODE_UNISON;
        for (i = 0; i < e->voice_count; i++)
            e->detune[i] = unison ? unison_detune(e, i) : 0;
        if (unison && id == FM_PARAM_UNISON_DETUNE_CENTS)
            retune(e); /* a sounding unison note follows at once */
        return;
    }
    if (id == FM_PARAM_CHORUS_MODE) {
        fm_chorus_set_mode(&e->chorus, (uint8_t)v);
        return;
    }
    if (apply_env_param(e, id, v))
        return;
    if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS ||
        id == FM_PARAM_FILTER_ENV_SOURCE) { /* every voice's, shared */
        int env2 = e->params[FM_PARAM_FILTER_ENV_SOURCE] != 0;
        int32_t amount = live_value(e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS);
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF,
                         env2 ? 0 : amount);
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV2, FM_MOD_CUTOFF,
                         env2 ? amount : 0);
    }
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *vc = &e->voices[i];
        if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS)
            fm_voice_sync_filter_env(vc); /* current before it changes */
        switch (id) {
        case FM_PARAM_FILTER_CUTOFF_CENTS:
            vc->cutoff_cents = v;
            fm_voice_mark(vc, FM_BUSY_CUTOFF, 1);
            break;
        case FM_PARAM_FILTER_RESONANCE:
            fm_filter_set_resonance(&vc->filter, v);
            break;
        case FM_PARAM_FILTER_KEY_TRACK:
            vc->key_track = v;
            fm_voice_mark(vc, FM_BUSY_CUTOFF, 1);
            break;
        case FM_PARAM_HIGHPASS_STEP:
            fm_voice_set_highpass(vc, (uint8_t)v);
            break;
        case FM_PARAM_FILTER_ENV_AMOUNT_CENTS:
            /* busy while the envelope reaches the cutoff, and after it
             * stops until the cutoff is summed back (update_cutoff) */
            fm_voice_mark(vc, FM_BUSY_FILTER_ENV,
                          !fm_mod_at_rest(&vc->mod, &e->mod.routes,
                                          FM_MOD_CUTOFF));
            fm_voice_mark(vc, FM_BUSY_CUTOFF, 1);
            break;
        default:
            break;
        }
    }
}

/* Releases a voice's note and ages it from now. */
static void release(fm_engine *e, fm_voice *v)
{
    v->stamp = ++e->stamp;
    fm_voice_release(v);
}

/* Unison forgets the keys it was holding. */
static void unison_reset(fm_engine *e)
{
    e->held_count = 0;
}

/*
 * Before a voice-mode switch: releases every note, held by a key or the
 * pedal, and leaves each sounding voice to finish at its pitch belonging
 * to no note, so the new mode starts clean.
 */
static void end_mode(fm_engine *e)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->key_down || v->sustained) release(e, v);
        if (fm_voice_active(v)) v->note = FM_VOICE_NO_NOTE;
    }
    unison_reset(e);
}

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count)
{
    uint16_t id;
    uint8_t i;
    if (!e || sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE ||
        voice_count == 0 || voice_count > FM_MAX_VOICES)
        return FM_ERR_ARG;
    e->sample_rate = sample_rate;
    e->voice_count = voice_count;
    /* each voice at 1 / voice_count, then -12 dB of headroom: both
     * oscillators at full peak at 2.0, and the chorus adds up to +5.9 dB
     * to a chord in phase (#24, the_worst_case_chord_stays_below_full_scale) */
    e->mix_gain = (int32_t)((1u << 28) / voice_count);
    e->stamp = 0;
    e->pedal_down = 0;
    e->bend = 0;
    e->bend_value = 0;
    unison_reset(e);
    e->fade_samples = fm_ms_to_samples(STEAL_FADE_MS, e->sample_rate);
    fm_lfo_reset(&e->lfo);
    e->tick_left = 0;
    e->ticks = 0;
    e->mod_on = 0;
    /* no route but the ones set here and by the parameters below;
     * velocity sets the level at full depth: its source is already the
     * note's level (velocity_peak) */
    fm_mod_routes_reset(&e->mod.routes);
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_VEL, FM_MOD_LEVEL, FM_ENV_FULL);
    /* each voice its own noise (an odd multiplier: never 0), so unison
     * voices, which start together, never play the same stream */
    {
        int32_t rate_cents = fm_filter_rate_cents(sample_rate);
        for (i = 0; i < FM_MAX_VOICES; i++) {
            fm_voice_reset(&e->voices[i], (uint32_t)(i + 1) * 0x9E3779B9u,
                           rate_cents, &e->amp_set, &e->filter_set);
            e->detune[i] = 0;
        }
    }
    fm_chorus_init(&e->chorus, sample_rate);
    glide_init(e);
    /* all defaults first: some settings combine several parameters */
    for (id = 0; id < FM_PARAM_COUNT; id++)
        e->params[id] = fm_param_info_get(id)->def;
    for (id = 0; id < FM_PARAM_COUNT; id++)
        apply_param(e, id);
    return FM_OK;
}

fm_status fm_engine_set_param(fm_engine *e, uint16_t id, int32_t value)
{
    const fm_param_info *info = fm_param_info_get(id);
    if (!e || !info) return FM_ERR_ARG;
#ifdef FM_CHECK_ENGINE /* debug builds: the engine was not copied (a
                        * zeroed one, voice_count 0, touches no voice) */
    {
        uint8_t i;
        for (i = 0; e->voice_count && i < FM_MAX_VOICES; i++)
            assert(e->voices[i].amp_env.set == &e->amp_set &&
                   e->voices[i].filter_env.set == &e->filter_set);
    }
#endif
    if (value < info->min) value = info->min;
    if (value > info->max) value = info->max;
    if (id == FM_PARAM_VOICE_MODE && value != e->params[id])
        end_mode(e);
    if (id == FM_PARAM_SETTING_GLIDE && value && !e->params[id] && e->ticks) {
        /* turned on after a render: the glides start where the settings
         * are (only from init do they start at PG-8X's power-on values);
         * set on again, as a patch load does, they go on */
        int k;
        for (k = 0; k < FM_GLIDE_SETTINGS; k++)
            e->glide_cur[k] = glide_target(e, k) * 256;
    }
    e->params[id] = value;
    apply_param(e, id);
    return FM_OK;
}

int32_t fm_engine_get_param(const fm_engine *e, uint16_t id)
{
    return (e && id < FM_PARAM_COUNT) ? e->params[id] : 0;
}

/* The sounding voice that has `note`, or null. At most one voice has a
 * given note, because a repeated note reuses its voice. */
static fm_voice *find_note(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->note == note && fm_voice_active(v)) return v;
    }
    return 0;
}

/* Steal preference: lower ranks are taken first. */
static int voice_rank(const fm_voice *v)
{
    if (!fm_voice_active(v)) return 0;
    if (v->key_down) return 3;   /* held */
    if (v->sustained) return 2;  /* held by the pedal */
    return 1;                    /* releasing */
}

/*
 * The voice a new note takes: an idle one if any, else the oldest
 * released one, else the oldest held one. "Oldest" is the longest since
 * its last note on or off (stamps compared as a wrapping difference).
 * Never `skip`; null if there is no other voice.
 */
static fm_voice *pick_voice(fm_engine *e, const fm_voice *skip)
{
    fm_voice *best = 0;
    int best_rank = 0;
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        int r = voice_rank(v);
        if (v == skip) continue;
        if (!best || r < best_rank ||
            (r == best_rank && (int32_t)(v->stamp - best->stamp) < 0)) {
            best = v;
            best_rank = r;
        }
    }
    return best;
}

/*
 * Amp envelope peak (Q1.30) for a MIDI velocity 1..127: full level less
 * amp_velocity percent of the way down the curve (velocity / 127)^2.
 * Velocity 127 is always full. Two constant divides, per note on.
 * With amp_dyna (the JX-8P's VCA DYNAMICS) the peak is instead the
 * DYNAMICS share at the velocity, as PG-8X 2.0.13 plays it (#8's curve).
 */
static int32_t velocity_peak(const fm_engine *e, uint8_t velocity)
{
    int32_t sens = e->params[FM_PARAM_AMP_VELOCITY];
    if (e->params[FM_PARAM_AMP_DYNA])
        return (int32_t)(((int64_t)FM_ENV_FULL *
                          fm_dyna_share_q15(e->params[FM_PARAM_AMP_DYNA], velocity)) >> 15);
    int64_t curve = (int64_t)velocity * velocity * FM_ENV_FULL / (127 * 127);
    return FM_ENV_FULL - (int32_t)((FM_ENV_FULL - curve) * sens / 100);
}

/* Poly: voice `v` plays `note` at amp peak `peak` (in poly every detune
 * is 0). */
static void poly_play(fm_engine *e, fm_voice *v, uint8_t note, int32_t peak,
                      uint8_t velocity)
{
    uint8_t i = (uint8_t)(v - e->voices);
    v->stamp = ++e->stamp;
    fm_voice_note_on(v, &e->mod, note, voice_pitch(e, i, note), peak, velocity,
                     e->ticks + 1, e->sample_rate, e->fade_samples);
}

/* The voice fading `note` out after a steal, or null. */
static fm_voice *find_fading(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->pending && v->old_note == note) return v;
    }
    return 0;
}

/*
 * `note` is played again while voice `f` fades it out for a steal: `f`
 * takes it back, so one pitch is never on two voices, and the note that
 * was waiting for `f` gets a voice by the usual rule. With no other
 * voice (or if its key already came up) that waiting note is dropped:
 * the newest note wins. `peak` and `velocity` are the new strike's; the
 * waiting note keeps its own, which `f` still holds in next_peak and
 * next_velocity.
 */
static void take_back(fm_engine *e, fm_voice *f, uint8_t note, int32_t peak,
                      uint8_t velocity)
{
    uint8_t moved = f->note, key_down = f->key_down, sustained = f->sustained;
    fm_voice *w;
    fm_voice_take_back(f, &e->mod, voice_pitch(e, (uint8_t)(f - e->voices), note),
                       peak, velocity, e->ticks + 1, e->sample_rate);
    f->stamp = ++e->stamp;
    if (!key_down && !sustained) return;
    w = pick_voice(e, f);
    if (!w) return;
    poly_play(e, w, moved, f->next_peak, f->next_velocity);
    if (!key_down) fm_voice_key_up(w, 1); /* held by the pedal */
}

/* Drops `note` from the unison held-key list, if it is there. Returns
 * whether it was the newest (the note the voices play). */
static int forget_held(fm_engine *e, uint8_t note)
{
    uint8_t i, j = 0;
    int newest = e->held_count && e->held[e->held_count - 1] == note;
    for (i = 0; i < e->held_count; i++)
        if (e->held[i] != note) e->held[j++] = e->held[i];
    e->held_count = j;
    return newest;
}

/* Every voice plays `note`, each at its detune; a retrigger at amp peak
 * `peak` and `velocity` (a legato change ignores them and keeps each
 * voice's). */
static void unison_play(fm_engine *e, uint8_t note, int retrigger,
                        int32_t peak, uint8_t velocity)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        v->stamp = ++e->stamp;
        fm_voice_change_note(v, &e->mod, note, voice_pitch(e, i, note), peak,
                             velocity, e->ticks + 1, e->sample_rate, retrigger);
    }
}

/* Unison: the newest note wins and retriggers (docs/voices.md#unison). */
static void unison_note_on(fm_engine *e, uint8_t note, int32_t peak,
                           uint8_t velocity)
{
    uint8_t i;
    forget_held(e, note);
    if (e->held_count == FM_HELD_MAX) { /* full: drop the oldest key */
        for (i = 1; i < FM_HELD_MAX; i++) e->held[i - 1] = e->held[i];
        e->held_count--;
    }
    e->held[e->held_count++] = note;
    unison_play(e, note, 1, peak, velocity); /* a legato return keeps them */
}

/* Unison: releasing the sounding note goes back to the newest key still
 * down, legato; releasing the last one releases (or the pedal holds). */
static void unison_note_off(fm_engine *e, uint8_t note)
{
    uint8_t i;
    if (!forget_held(e, note)) return; /* not the sounding note */
    if (e->held_count) {
        unison_play(e, e->held[e->held_count - 1], 0, 0, 0);
        return;
    }
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (!v->key_down) continue;
        v->stamp = ++e->stamp;
        fm_voice_key_up(v, e->pedal_down);
    }
}

void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity)
{
    if (e->sample_rate == 0 || note > 127 || velocity > 127)
        return; /* not initialized, or not MIDI */
    if (velocity == 0) {
        fm_engine_note_off(e, note);
        return;
    }
    if (e->params[FM_PARAM_VOICE_MODE] == FM_VOICE_MODE_UNISON) {
        unison_note_on(e, note, velocity_peak(e, velocity), velocity);
        return;
    }
    {
        int32_t peak = velocity_peak(e, velocity);
        fm_voice *v = find_note(e, note);
        if (!v) {
            fm_voice *f = find_fading(e, note);
            if (f) {
                take_back(e, f, note, peak, velocity);
                return;
            }
            v = pick_voice(e, 0);
        }
        poly_play(e, v, note, peak, velocity);
    }
}

void fm_engine_note_off(fm_engine *e, uint8_t note)
{
    uint8_t i;
    if (e->params[FM_PARAM_VOICE_MODE] == FM_VOICE_MODE_UNISON) {
        unison_note_off(e, note);
        return;
    }
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->key_down && v->note == note) {
            v->stamp = ++e->stamp;
            fm_voice_key_up(v, e->pedal_down);
        }
    }
}

void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value)
{
    uint8_t i;
    if (controller == 64) e->pedal_down = value >= 64;
    if (controller == 120 || controller == 123) unison_reset(e);
    if (controller == 120) fm_chorus_silence(&e->chorus); /* no wet tail */
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        switch (controller) {
        case 64: /* sustain pedal: up lets go of the notes it held */
            if (!e->pedal_down && v->sustained) release(e, v);
            break;
        case 120: /* all sound off: silent now */
            fm_voice_silence(v);
            break;
        case 123: /* all notes off: release, even notes the pedal holds */
            if (v->key_down || v->sustained) release(e, v);
            break;
        default:
            break;
        }
    }
}

void fm_engine_pitch_bend(fm_engine *e, uint16_t value)
{
    if (e->sample_rate == 0 || value > 16383) return;
    e->bend_value = (int32_t)value - 8192;
    apply_bend(e, e->bend_value);
}

uint8_t fm_engine_active_voices(const fm_engine *e)
{
    uint8_t i, n = 0;
    for (i = 0; i < e->voice_count; i++)
        if (fm_voice_active(&e->voices[i])) n++;
    return n;
}

/*
 * Q8.24 to int16 with +6 dB of output makeup (#24), then a soft clip.
 * The mix keeps 12 dB of headroom inside the engine (both oscillators and
 * the chorus); the makeup gives 6 of it back. Up to the knee at -6 dBFS
 * it is a pure gain. Above it, d past the knee becomes
 * knee + h d / (h + d), h = full scale - knee: slope 1 at the knee,
 * monotonic, and short of full scale. That curve is read from a table
 * (tools/gen_softclip_table.py), linearly interpolated, so the render path
 * has no divide; past d = 2.0 it holds at 0.9 of full scale. The worst
 * case short of self-oscillation (d about 1.0) never reaches the rails.
 */
#define OUT_KNEE (1 << 23) /* 0.5 of full scale, after the makeup, in Q8.24 */
#define OUT_CLIP_SHIFT 18  /* table step: 2^18 in Q8.24 */
static const int32_t out_clip[129] = {
    0, 254200, 493448, 719024, 932068, 1133596,
    1324517, 1505648, 1677722, 1841402, 1997288, 2145923,
    2287802, 2423376, 2553055, 2677215, 2796203, 2910333,
    3019899, 3125168, 3226388, 3323788, 3417581, 3507963,
    3595118, 3679214, 3760410, 3838855, 3914684, 3988027,
    4059004, 4127728, 4194304, 4258832, 4321404, 4382109,
    4441028, 4498239, 4553816, 4607827, 4660338, 4711410,
    4761102, 4809469, 4856563, 4902433, 4947128, 4990691,
    5033165, 5074590, 5115005, 5154446, 5192948, 5230544,
    5267265, 5303143, 5338205, 5372479, 5405992, 5438768,
    5470831, 5502205, 5532912, 5562972, 5592405, 5621232,
    5649471, 5677139, 5704253, 5730831, 5756888, 5782439,
    5807498, 5832080, 5856198, 5879865, 5903095, 5925897,
    5948286, 5970271, 5991863, 6013073, 6033911, 6054387,
    6074509, 6094288, 6113731, 6132848, 6151646, 6170133,
    6188317, 6206206, 6223806, 6241124, 6258168, 6274943,
    6291456, 6307713, 6323720, 6339482, 6355006, 6370296,
    6385358, 6400197, 6414818, 6429225, 6443424, 6457418,
    6471212, 6484810, 6498217, 6511437, 6524473, 6537329,
    6550009, 6562516, 6574855, 6587028, 6599038, 6610890,
    6622585, 6634128, 6645521, 6656766, 6667868, 6678828,
    6689649, 6700335, 6710886,
};

static int16_t to_int16(int32_t q24)
{
    int64_t u, a, d;
    uint32_t k;
    int32_t s;
    /* below the knee (the common case): a pure gain, one unsigned compare
     * in 32 bits (|q24| <= knee / 2 before the makeup) */
    if ((uint32_t)q24 + (uint32_t)(OUT_KNEE / 2) <= (uint32_t)OUT_KNEE)
        return (int16_t)(q24 >> 8);
    u = (int64_t)q24 * 2;
    a = u < 0 ? -u : u;
    d = a - OUT_KNEE;
    k = (uint32_t)(d >> OUT_CLIP_SHIFT);
    if (k >= 128) {
        a = OUT_KNEE + out_clip[128];
    } else {
        int64_t frac = d & ((1 << OUT_CLIP_SHIFT) - 1);
        a = OUT_KNEE + out_clip[k] +
            (((int64_t)(out_clip[k + 1] - out_clip[k]) * frac) >> OUT_CLIP_SHIFT);
    }
    u = u < 0 ? -a : a;
    s = (int32_t)(u >> 9);
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

/* The LFO is live: it reaches some destination now, or the last tick
 * applied it (one more tick clears the offset it left). The one test of
 * whether the control ticks have work for the voices. */
static int lfo_live(const fm_engine *e)
{
    return e->mod_on || fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_LFO) ||
           fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_PENV);
}

/*
 * Control tick, every FM_LFO_TICK_FRAMES samples counted from init (so
 * the caller's block size never moves it): the LFO advances and each
 * sounding voice applies it.
 */
static void control_tick(fm_engine *e)
{
    int32_t lfo = fm_lfo_tick(&e->lfo);
    uint8_t k;
    e->ticks++;
    e->tick_left = FM_LFO_TICK_FRAMES;
    if (glide_live(e) && !(e->ticks % GLIDE_TICKS)) glide_tick(e);
    /* with every depth at 0 (after one tick that clears any offset still
     * applied) the voices have nothing to do */
    if (lfo_live(e))
        for (k = 0; k < e->voice_count; k++)
            if (fm_voice_active(&e->voices[k]))
                fm_voice_tick(&e->voices[k], lfo, &e->mod, e->ticks);
    e->mod_on = (uint8_t)(fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_LFO) ||
                          fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_PENV));
}

/*
 * Frames the next chunk renders. While the LFO modulates something (or
 * an offset from before is still applied) a chunk stops at the next
 * control tick, so the output never depends on the caller's block size.
 * With every depth at 0 the ticks change no sample, so a chunk runs to
 * FM_MIX_FRAMES and the ticks it passes run after it (they only advance
 * the LFO and the tick count, so a voice still learns which tick its
 * note started before): rendering costs what it did before the LFO
 * existed.
 */
static uint32_t chunk_frames(fm_engine *e, uint32_t frames)
{
    uint32_t n = frames < FM_MIX_FRAMES ? frames : FM_MIX_FRAMES;
    if (e->tick_left == 0) control_tick(e);
    if ((lfo_live(e) || glide_live(e)) && n > e->tick_left)
        n = e->tick_left;
    return n;
}

/* After a chunk of `n` frames: the ticks a batched chunk passed. */
static void advance_ticks(fm_engine *e, uint32_t n)
{
    while (n > e->tick_left) {
        n -= e->tick_left;
        control_tick(e);
    }
    e->tick_left -= n;
}

void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames)
{
    while (frames) {
        uint32_t n, i;
        uint8_t k;
        n = chunk_frames(e, frames);
        for (i = 0; i < n; i++) e->mix[i] = 0;
        for (k = 0; k < e->voice_count; k++)
            if (fm_voice_active(&e->voices[k]))
                fm_voice_render_add(&e->voices[k], &e->mod, e->mix, e->mix_right, n, e->sample_rate,
                                    e->ticks + 1, e->tick_left);
        /* sum of the voices (Q8.24) times the mix gain (Q1.30) */
        for (i = 0; i < n; i++)
            e->mix[i] = (int32_t)(((int64_t)e->mix[i] * e->mix_gain) >> 30);
        if (fm_chorus_is_on(&e->chorus)) {
            fm_chorus_process_block(&e->chorus, e->mix, e->mix_right, n);
            for (i = 0; i < n; i++) {
                out[2 * i] = to_int16(e->mix[i]);
                out[2 * i + 1] = to_int16(e->mix_right[i]);
            }
        } else {
            for (i = 0; i < n; i++) {
                int16_t s = to_int16(e->mix[i]);
                out[2 * i] = s;
                out[2 * i + 1] = s;
            }
        }
        advance_ticks(e, n);
        out += 2 * n;
        frames -= n;
    }
}
