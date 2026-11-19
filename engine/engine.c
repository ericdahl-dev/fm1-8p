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
/* the gate's release time constant (#63), tenths of a ms: PG-8X 2.0.13's, from
 * its 1.4 / 2.6 ms to -20 / -40 dB (#160) */
#define GATE_RELEASE_TENTHS_MS 6u

/*
 * The engine relies on >> of a negative value being an arithmetic shift
 * (implementation-defined in C99; true for GCC, Clang and the JieLi
 * toolchain). Refuse to build where it is not.
 */
typedef char fm_require_arithmetic_shift[((-8 >> 1) == -4) ? 1 : -1];

/*
 * fm_engine's budget on 32-bit targets (the FM-1, Cortex-M), where it
 * lives: FM_ENGINE_BUDGET_32 in fm1_8p.h, a base plus 4 bytes a param. The
 * FM-1 build fails past it; 64-bit desktops check theirs in
 * tests/test_chorus.c (pointers there are 8 bytes).
 */
typedef char fm_require_engine_within_its_32_bit_budget[
    (sizeof(void *) != 4 || sizeof(fm_engine) <= FM_ENGINE_BUDGET_32) ? 1 : -1];

/* Pitch of voice `i` playing `note`, Q16.16 semitones: the note, the
 * bend and the voice's unison detune. Both oscillators follow it. */
static int32_t voice_pitch(const fm_engine *e, uint8_t i, uint8_t note)
{
    return ((int32_t)note << 16) + e->bend + e->detune[i] + e->porta[i];
}

/* Portamento (#62): a note comes in; it glides from the last one played
 * (bit 1 of porta_has: a note was played; bit 0: there is one to glide from) */
static void porta_note(fm_engine *e, uint8_t note)
{
    if (e->porta_has & 2u) {
        e->porta_from = e->porta_newest;
        e->porta_has |= 1u;
    }
    e->porta_newest = (int32_t)note << 16;
    e->porta_has |= 2u;
}

/* Portamento: voice `i` starts `note`, from the note glided from (none:
 * no glide, as with portamento off) */
static void porta_start(fm_engine *e, uint8_t i, uint8_t note)
{
    e->porta[i] = e->params[FM_PARAM_PORTAMENTO_MS] && (e->porta_has & 1u) ?
                  e->porta_from - ((int32_t)note << 16) : 0;
    if (e->porta[i])
        e->porta_moving = 1;
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
    for (i = 0; i < e->voice_count; i++) { /* idle voices get theirs at note on */
        fm_voice *v = &e->voices[i];
        uint8_t note = fm_voice_note(v); /* its note, or the one a steal waits to start */
        if (fm_voice_active(v) && note != FM_VOICE_NO_NOTE)
            fm_voice_set_pitch(v, voice_pitch(e, i, note), e->sample_rate);
    }
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
    return cents + e->params[which ? FM_PARAM_OSC2_COARSE_SEMIS : FM_PARAM_OSC1_COARSE_SEMIS] * 100;
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

/* PG-8X 2.0.13's fastest amp attack, ENV-2 ATTACK 0, reaches full in about 1.5 ms from a slow start, on every key
 * and at every ENV-2 KEY position; a shorter attack starts the note on a step heard as a click (#274). Every amp
 * attack, from any source and after key follow (the settings' attack_min), is at least this: 1.4 ms, which with
 * the envelope's ease-in (engine/env.c) is at full by about 1.5 ms, as PG-8X's. */
#define AMP_ATTACK_FLOOR_TENTHS_MS 14u
static uint32_t amp_attack_floor(const fm_engine *e)
{
    return e->sample_rate * AMP_ATTACK_FLOOR_TENTHS_MS / 10000u;
}

/* Voice i's envelope on the filter settings (`filter`) or the amp ones:
 * with the gate (#63) the amp settings run env2. */
static fm_env *set_env(fm_engine *e, uint8_t i, int filter)
{
    fm_voice *vc = &e->voices[i];
    return filter ? &vc->filter_env
           : e->params[FM_PARAM_AMP_GATE] ? &vc->env2 : &vc->amp_env;
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
    /* catch up before writing shared settings, or the lag steps the past
     * on the new value: every voice's envelope on them (filter_env and,
     * with the gate, env2 are lazy; amp_env is stepped, never behind) */
    for (i = 0; i < e->voice_count; i++)
        fm_env_catch_up(set_env(e, i, filter));
    if (time) {
        *time = fm_ms_to_samples(v, e->sample_rate);
    } else
        *sustain = fm_permille_to_q30(v);
    for (i = 0; i < e->voice_count; i++) {
        fm_env *env = set_env(e, i, filter);
        /* an idle one leaves IDLE only by fm_env_gate_on, which reads
         * the settings and recomputes sus_level itself */
        if (env->stage != FM_ENV_IDLE)
            fm_env_retime(env);
    }
    return 1;
}

/*
 * The mix gain at VCA LEVEL `permille`: the voices' sum times the gain for
 * the voice count (headroom, set at init) times the level, as PG-8X's VCA
 * LEVEL scales its output and follows a change mid-note (#169). The level
 * in Q30 is permille x 2^30 / 1000 in 32 bits, exact at 1000 (so a full
 * level leaves the gain as it was). Event rate.
 */
static int32_t level_gain(const fm_engine *e, int32_t permille)
{
    uint32_t p = (uint32_t)permille, q = p * 1073741u + p * 824u / 1000u;
    return (int32_t)(((int64_t)(386669466u / e->voice_count) * q) >> 30);
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
    FM_PARAM_LFO_PITCH_CENTS, FM_PARAM_LFO_PITCH_OSC2_CENTS, FM_PARAM_AMP_LEVEL
};
/* PG-8X's power-on values in engine units: DCO-1 at full mix, DCO-2 off,
 * VCF FREQ 127 (tools/jx8p.py cutoff_cents), VCA LEVEL full (#169), the
 * rest 0 */
static const int16_t glide_power_on[FM_GLIDE_SETTINGS] = { 1000, 0, 14728, 0, 0, 0, 0, 0, 1000 };
#define AT_TAU_MS 16  /* aftertouch's one-pole (#61): PG-8X's, as the setting glide's */
#define GLIDE_TAU_MS 16
/* a glide steps every 4 control ticks (2.7 ms at 48 kHz, 6 steps a tau):
 * each step re-applies the settings that moved to every voice */
#define GLIDE_TICKS 4

/* Event-rate code kept out of its callers: inline at -O2 (the FM-1's)
 * it copied the parameter loops into each, about 700 B */
#if defined(__GNUC__) || defined(__clang__)
#define GLIDE_NOINLINE __attribute__((noinline))
#else
#define GLIDE_NOINLINE
#endif

static GLIDE_NOINLINE int glide_index(uint16_t id)
{
    int k;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++)
        if (glide_id[k] == id) return k;
    return -1;
}

/* Where setting k is going, Q8: its target (osc 2's LFO depth -1 is osc
 * 1's, the 7th). */
static int32_t glide_target(const fm_engine *e, int k)
{
    int32_t v = e->glide_to[k];
    if (glide_id[k] == FM_PARAM_LFO_PITCH_OSC2_CENTS && v < 0) v = e->glide_to[6];
    return v * 256;
}

/* One step of 1 - exp(-x), x = a step / tau, Q16, from its series in 32
 * bits (a step of GLIDE_TICKS ticks is 1 tau at 8 kHz, where the series'
 * fourth term, dropped, is 1/24). */
static void glide_init(fm_engine *e)
{
    int32_t x = (int32_t)((uint32_t)FM_LFO_TICK_FRAMES * GLIDE_TICKS * (1000u * 65536u / GLIDE_TAU_MS) /
                          e->sample_rate);
    int32_t x2 = ((x >> 4) * (x >> 4)) >> 8, x3 = ((x2 >> 4) * (x >> 4)) >> 8;
    e->glide_k = x - x2 / 2 + x3 / 6;
    e->glide_moving = 0;
}

static void apply_param(fm_engine *e, uint16_t id);

/*
 * The mod wheel (#41): CC1 adds each patch's wheel amount to the LFO's
 * vibrato (both oscillators) and pulse-width depths, in proportion. A
 * move is smoothed: the wheel steps to CC1 over WHEEL_TICKS control ticks
 * (about 12 ms at 44.1 kHz), so a depth never jumps. At 0 every depth is
 * the patch's own, exactly, and nothing else changes.
 */
#define WHEEL_FULL (127 << 8)
#define WHEEL_TICKS 16

/* The LFO's route depths: the patch's, plus the wheel's share. Pitch in
 * cents -> Q16.16 octaves (exact for multiples of 75 cents); osc 2 has its
 * own depth, or (-1) osc 1's. Pulse width in percent -> Q0.32, at most
 * 0.49 in all. */
static void lfo_routes(fm_engine *e)
{
    int32_t add = e->params[FM_PARAM_WHEEL_PITCH_CENTS] * e->wheel / WHEEL_FULL;
    int32_t c1 = e->params[FM_PARAM_LFO_PITCH_CENTS];
    int32_t c2 = e->params[FM_PARAM_LFO_PITCH_OSC2_CENTS];
    int32_t pw = e->params[FM_PARAM_LFO_PW_PCT] +
                 e->params[FM_PARAM_WHEEL_PW_PCT] * e->wheel / WHEEL_FULL;
    if (c2 < 0) c2 = c1;
    if (pw > 49) pw = 49;
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PITCH1,
                     (c1 + add) * 65536 / 1200);
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PITCH2,
                     (c2 + add) * 65536 / 1200);
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PW,
                     pw * 42949673);
    /* aftertouch's vibrato (#61): both oscillators, cents at full pressure */
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ATLFO, FM_MOD_PITCH1,
                     e->params[FM_PARAM_AT_VIB_CENTS] * 65536 / 1200);
    fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ATLFO, FM_MOD_PITCH2,
                     e->params[FM_PARAM_AT_VIB_CENTS] * 65536 / 1200);
}

/* A control tick while the wheel moves: one step of its smoothing. */
static void wheel_tick(fm_engine *e)
{
    int32_t step = WHEEL_FULL / WHEEL_TICKS, d = e->wheel_to - e->wheel;
    e->wheel += d > step ? step : d < -step ? -step : d;
    e->wheel_moving = (uint8_t)(e->wheel != e->wheel_to);
    lfo_routes(e);
}

/*
 * The glide turned on: its settings' parameters now hold where they
 * play, their targets apart (glide_to). After a render they start where
 * they are; before one, at PG-8X's power-on values, as its renders do.
 * Turned off, they play their targets at once.
 */
static GLIDE_NOINLINE void glide_switch(fm_engine *e, int on)
{
    int k;
    e->glide_moving = (uint8_t)on;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++) { /* osc 2's LFO depth after osc 1's, which it may follow */
        int32_t *p = &e->params[glide_id[k]];
        if (on) {
            e->glide_to[k] = (int16_t)*p;
            e->glide_cur[k] = e->ticks ? glide_target(e, k) : glide_power_on[k] * 256;
            *p = (e->glide_cur[k] + 128) >> 8;
        } else {
            *p = e->glide_to[k];
        }
        apply_param(e, glide_id[k]);
    }
}

/* Every GLIDE_TICKS control ticks while one moves: each setting one step
 * on its way; what moves is applied as its parameter is. */
static GLIDE_NOINLINE void glide_tick(fm_engine *e)
{
    int k;
    uint8_t moving = 0;
    for (k = 0; k < FM_GLIDE_SETTINGS; k++) {
        int32_t *cur = &e->glide_cur[k], d = glide_target(e, k) - *cur, was = *cur + 128;
        if (!d) continue;
        *cur += (uint32_t)d + 255u < 511u ? d : ((d >> 8) * e->glide_k) >> 8; /* under a unit: there */
        moving |= *cur != glide_target(e, k);
        if (((*cur + 128) ^ was) >> 8) {                                     /* its value moved */
            e->params[glide_id[k]] = (*cur + 128) >> 8;
            apply_param(e, glide_id[k]);
        }
    }
    e->glide_moving = moving;
}

/* Portamento's step a control tick: 1 - exp(-x), x = a tick / tau, Q30
 * (Q16 is 4 at 10 s, 9% off), from its series (x is at most 0.67 at 1 ms
 * and 48 kHz, where the fourth term, dropped, is 0.008). Off (0): every
 * glide ends where it is now. */
static void porta_init(fm_engine *e)
{
    uint32_t ms = (uint32_t)e->params[FM_PARAM_PORTAMENTO_MS];
    int64_t x, x2, x3;
    uint8_t i;
    if (!ms || !e->sample_rate) {
        e->porta_k = 0;
        if (e->porta_moving) {
            for (i = 0; i < e->voice_count; i++)
                e->porta[i] = 0;
            e->porta_moving = 0;
            retune(e);
        }
        return;
    }
    x = ((int64_t)FM_LFO_TICK_FRAMES * 1000 << 30) / ((int64_t)e->sample_rate * ms);
    if (x >= (int64_t)1 << 30) {                /* tau under a tick: there in one */
        e->porta_k = 1 << 30;
        return;
    }
    x2 = (x * x) >> 30;
    x3 = (x2 * x) >> 30;
    e->porta_k = (int32_t)(x - x2 / 2 + x3 / 6);
}

/* A control tick while a voice glides: its glide still to go falls by
 * porta_k (rounded up, on its size, so a glide either way always ends),
 * and that voice alone is retuned; under 1/1000 semitone it ends. A
 * voice gone quiet stops gliding. */
static void porta_tick(fm_engine *e)
{
    uint8_t i, moving = 0;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        uint8_t note = fm_voice_note(v);
        int32_t g = e->porta[i], d;
        if (!g) continue;
        if (!fm_voice_active(v) || note == FM_VOICE_NO_NOTE) {
            e->porta[i] = 0;
            continue;
        }
        d = (int32_t)(((int64_t)(g < 0 ? -g : g) * e->porta_k + (1 << 30) - 1) >> 30);
        g = g < 0 ? g + d : g - d;
        e->porta[i] = g > -66 && g < 66 ? 0 : g;
        moving |= e->porta[i] != 0;
        fm_voice_set_pitch(v, voice_pitch(e, i, note), e->sample_rate);
    }
    e->porta_moving = moving;
}

/* Recomputes the derived settings a parameter feeds. */
/* Aftertouch's volume (#61), as PG-8X's: voice `i`'s gain is
 * 1 - d/2 + d x pressure at at_vol_pct d (Q1.30; exactly full at d 0). */
static int32_t at_gain(const fm_engine *e, uint8_t i)
{
    int64_t d = e->params[FM_PARAM_AT_VOL_PCT];
    return (int32_t)(FM_ENV_FULL + d * (2 * (int64_t)e->at_now[i] - FM_ENV_FULL) / 200);
}

/* A control tick while a pressure moves: each voice one step of its
 * one-pole, rounded up on its size so it always lands (under 1/65536 of
 * full: there) */
static void at_tick(fm_engine *e)
{
    uint8_t i, moving = 0;
    for (i = 0; i < e->voice_count; i++) {
        int32_t d = e->at_to[i] - e->at_now[i];
        if (!d) continue;
        if (d > -(1 << 14) && d < (1 << 14))
            e->at_now[i] = e->at_to[i];
        else {
            int64_t m = ((int64_t)(d < 0 ? -d : d) * e->at_k + 65535) >> 16;
            e->at_now[i] += (int32_t)(d < 0 ? -m : m);
            moving = 1;
        }
        fm_voice_set_gain(&e->voices[i], at_gain(e, i));
        fm_voice_set_pressure(&e->voices[i], &e->mod, e->at_now[i]);
    }
    e->at_moving = moving;
}

/* Voice `i`'s pressure goes to `p` (Q1.30): by the one-pole, or, with
 * every depth at 0, at once (it does nothing then, so costs nothing; a
 * depth turned up later starts from it) */
static void at_set(fm_engine *e, uint8_t i, int32_t p)
{
    e->at_to[i] = p;
    if (e->params[FM_PARAM_AT_VIB_CENTS] | e->params[FM_PARAM_AT_BRI_CENTS] |
        e->params[FM_PARAM_AT_VOL_PCT]) {
        e->at_moving = 1;
        return;
    }
    e->at_now[i] = p;
    fm_voice_set_pressure(&e->voices[i], &e->mod, p);
}

/* Every voice takes its aftertouch gain. */
static void at_gains(fm_engine *e)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++)
        fm_voice_set_gain(&e->voices[i], at_gain(e, i));
}

static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = e->params[id];
    uint8_t i;
    switch (id) {
    case FM_PARAM_PORTAMENTO_MS:
        porta_init(e);
        return;
    case FM_PARAM_BEND_RANGE:
        apply_bend(e, e->bend_value);
        return;
    case FM_PARAM_SETTING_GLIDE: /* glide_switch, from fm_engine_set_param */
        return;
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
    case FM_PARAM_OSC1_COARSE_SEMIS:
        retune_osc(e, 0);
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
    case FM_PARAM_LFO_PITCH_CENTS:
    case FM_PARAM_LFO_PITCH_OSC2_CENTS:
    case FM_PARAM_WHEEL_PITCH_CENTS:
    case FM_PARAM_WHEEL_PW_PCT:
    case FM_PARAM_AT_VIB_CENTS:
        lfo_routes(e);
        return;
    case FM_PARAM_AT_VOL_PCT:
        at_gains(e);
        return;
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
    case FM_PARAM_AMP_LEVEL: /* the VCA LEVEL: the output's gain, at once (#169) */
        e->mix_gain = level_gain(e, v);
        for (i = 0; i < FM_MAX_VOICES; i++) /* every voice frees 80 dB under the output */
            fm_voice_set_output_level(&e->voices[i], v);
        return;
    case FM_PARAM_AMP_GATE: /* every voice, as fm_voice_reset gave them all */
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_voice_set_amp(&e->voices[i], v ? &e->gate_set : &e->amp_set);
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
        int32_t amount = e->params[FM_PARAM_FILTER_ENV_AMOUNT_CENTS];
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF,
                         env2 ? 0 : amount);
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV2, FM_MOD_CUTOFF,
                         env2 ? amount : 0);
    }
    if (id == FM_PARAM_AT_BRI_CENTS) /* aftertouch's brilliance (#61) */
        e->mod.at_bri_cents = v;
    if (id == FM_PARAM_FILTER_CUTOFF_CENTS) /* once for every voice (#253) */
        v = fm_filter_linear_cents(v);
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *vc = &e->voices[i];
        switch (id) {
        case FM_PARAM_FILTER_CUTOFF_CENTS:
            fm_voice_set_cutoff_linear(vc, v);
            break;
        case FM_PARAM_FILTER_RESONANCE:
            fm_voice_set_resonance(vc, v);
            break;
        case FM_PARAM_FILTER_KEY_TRACK:
            fm_voice_set_key_track(vc, v);
            break;
        case FM_PARAM_HIGHPASS_STEP:
            fm_voice_set_highpass(vc, (uint8_t)v);
            break;
        case FM_PARAM_AT_BRI_CENTS:
            fm_voice_mark(vc, FM_BUSY_CUTOFF, 1);
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
    fm_voice_age(v, ++e->stamp);
    fm_voice_release(v);
}

/* A key or the pedal holds the voice's note. */
static int key_or_pedal(const fm_voice *v)
{
    return fm_voice_holding(v) >= FM_VOICE_PEDAL;
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
        if (fm_voice_let_go(v)) fm_voice_age(v, ++e->stamp);
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
    /* each voice at 1 / voice_count, then -8.83 dB: the -12 dB of
     * headroom (#24: both oscillators at full peak at 2.0, and the chorus
     * adds up to +5.9 dB to a chord in phase) less PG-8X 2.0.13's 3.17 dB
     * over it, the same on every path (#102: one DCO or two, any notes,
     * the filter, the highpass, the VCA modes) */
    e->mix_gain = (int32_t)(386669466u / voice_count);
    e->stamp = 0;
    e->pedal_down = 0;
    e->bend = 0;
    e->bend_value = 0;
    unison_reset(e);
    e->fade_samples = fm_ms_to_samples(STEAL_FADE_MS, e->sample_rate);
    /* the gate (#63): up as fast as PG-8X's fastest attack (about 1.5 ms,
     * as its GATE opens, #274), held at full, down with a 0.6 ms time constant, as
     * PG-8X 2.0.13's GATE falls 20 / 40 dB in 1.4 / 2.6 ms */
    e->gate_set.attack_samples = 0;
    e->gate_set.attack_min = amp_attack_floor(e);
    e->amp_set.attack_min = amp_attack_floor(e);
    e->filter_set.attack_min = 0;
    e->gate_set.decay_samples = 0;
    e->gate_set.sustain = fm_permille_to_q30(1000);
    e->gate_set.release_samples = e->sample_rate * GATE_RELEASE_TENTHS_MS / 10000u;
    e->gate_set.key_follow = 0;
    fm_lfo_reset(&e->lfo);
    e->tick_left = 0;
    e->ticks = 0;
    e->mod_on = 0;
    /* no route but the ones set here and by the parameters below;
     * velocity sets the level at full depth: its source is already the
     * velocity's level (velocity_peak; the VCA LEVEL is the output's gain) */
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
    e->wheel = e->wheel_to = 0;                 /* the mod wheel down (#41) */
    {                                           /* no glide, nor a note to glide from (#62) */
        uint8_t q;
        for (q = 0; q < FM_MAX_VOICES; q++)
            e->porta[q] = 0;
        e->porta_moving = e->porta_has = 0;
        e->porta_from = e->porta_newest = 0;
        e->porta_k = 0;
    }
    e->wheel_moving = 0;
    e->at_chan = 0;                             /* no pressure (#61) */
    for (i = 0; i < FM_MAX_VOICES; i++) e->at_now[i] = e->at_to[i] = 0;
    e->at_moving = 0;
    {   /* 1 - exp(-x), x one control tick over 16 ms (as glide_init's) */
        int32_t x = (int32_t)((uint32_t)FM_LFO_TICK_FRAMES * (1000u * 65536u / AT_TAU_MS) / sample_rate);
        int32_t x2 = (x * x) >> 16, x3 = (x2 * x) >> 16;
        e->at_k = x - x2 / 2 + x3 / 6;
    }
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
            assert(e->voices[i].amp_env.set ==
                       (e->params[FM_PARAM_AMP_GATE] ? &e->gate_set : &e->amp_set) &&
                   e->voices[i].filter_env.set == &e->filter_set &&
                   e->voices[i].env2.set == &e->amp_set);
    }
#endif
    if (value < info->min) value = info->min;
    if (value > info->max) value = info->max;
    if (id == FM_PARAM_VOICE_MODE && value != e->params[id])
        end_mode(e);
    if (id == FM_PARAM_SETTING_GLIDE && value != e->params[id])
        glide_switch(e, (int)value); /* set on again, as a patch load does, the glides go on */
    if (e->params[FM_PARAM_SETTING_GLIDE]) {
        int k = glide_index(id);
        if (k >= 0) { /* where it goes; glide_tick takes it there */
            e->glide_to[k] = (int16_t)value;
            e->glide_moving = 1;
            return FM_OK;
        }
    }
    e->params[id] = value;
    apply_param(e, id);
    return FM_OK;
}

int32_t fm_engine_get_param(const fm_engine *e, uint16_t id)
{
    int k;
    if (!e || id >= FM_PARAM_COUNT) return 0;
    if (e->params[FM_PARAM_SETTING_GLIDE] && (k = glide_index(id)) >= 0)
        return e->glide_to[k]; /* where it goes, not where it is on the way */
    return e->params[id];
}

/* The sounding voice that has `note`, or null. At most one voice has a
 * given note, because a repeated note reuses its voice. */
static fm_voice *find_note(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (fm_voice_plays(v, note)) return v;
    }
    return 0;
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
    fm_voice_hold best_rank = FM_VOICE_FREE;
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        fm_voice_hold r = fm_voice_holding(v); /* lower is taken first */
        if (v == skip) continue;
        if (!best || r < best_rank ||
            (r == best_rank && (int32_t)(fm_voice_stamp(v) - fm_voice_stamp(best)) < 0)) {
            best = v;
            best_rank = r;
        }
    }
    return best;
}

/*
 * Amp envelope peak (Q1.30) for a MIDI velocity 1..127: full level less
 * amp_velocity percent of the way down the curve (velocity / 127)^2, or,
 * with amp_dyna (the JX-8P's VCA DYNAMICS), the DYNAMICS share at the
 * velocity as PG-8X 2.0.13 plays it (#8's curve). Velocity 127 is always
 * full. (The VCA LEVEL is the output's gain, mix_gain: live, #169.)
 * Two constant divides, per note on.
 */
static int32_t velocity_peak(const fm_engine *e, uint8_t velocity)
{
    int32_t sens = e->params[FM_PARAM_AMP_VELOCITY];
    int64_t peak;
    if (e->params[FM_PARAM_AMP_DYNA]) {
        peak = ((int64_t)FM_ENV_FULL *
                fm_dyna_share_q15(e->params[FM_PARAM_AMP_DYNA], velocity)) >> 15;
    } else {
        int64_t curve = (int64_t)velocity * velocity * FM_ENV_FULL / (127 * 127);
        peak = FM_ENV_FULL - (FM_ENV_FULL - curve) * sens / 100;
    }
    return (int32_t)peak;
}

/* A note starts on voice `i` (before the voice is told): at the last
 * channel pressure (#61: a key's own poly pressure comes after its note
 * on). From silence at once, no swell up from 0; on a voice still sounding
 * (a restrike, a steal's fade, a unison change) by the one-pole, as any
 * change, so its level and cutoff never step. */
static void at_start(fm_engine *e, uint8_t i)
{
    if (fm_voice_active(&e->voices[i])) {
        at_set(e, i, e->at_chan);
        return;
    }
    e->at_to[i] = e->at_chan;
    e->at_now[i] = e->at_chan;
    fm_voice_set_gain(&e->voices[i], at_gain(e, i));
    fm_voice_set_pressure(&e->voices[i], &e->mod, e->at_now[i]);
}

/* Poly: voice `v` plays `note` at amp peak `peak` (in poly every detune
 * is 0), its glide set (porta_start, or one it brings along). */
static void poly_play(fm_engine *e, fm_voice *v, uint8_t note, int32_t peak,
                      uint8_t velocity)
{
    uint8_t i = (uint8_t)(v - e->voices);
    fm_voice_age(v, ++e->stamp);
    at_start(e, i);
    fm_voice_note_on(v, &e->mod, note, voice_pitch(e, i, note), peak, velocity,
                     e->ticks + 1, e->sample_rate, e->fade_samples);
}

/* The voice fading `note` out after a steal, or null. */
static fm_voice *find_fading(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (fm_voice_fading(v, note)) return v;
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
    fm_voice_waiting moved = fm_voice_waiting_note(f);
    fm_voice *w;
    int32_t glide = e->porta[f - e->voices];    /* the waiting note's: it takes it along */
    at_start(e, (uint8_t)(f - e->voices));
    porta_start(e, (uint8_t)(f - e->voices), note);
    fm_voice_take_back(f, &e->mod, voice_pitch(e, (uint8_t)(f - e->voices), note),
                       peak, velocity, e->ticks + 1, e->sample_rate);
    fm_voice_age(f, ++e->stamp);
    if (!moved.key_down && !moved.sustained) return;
    w = pick_voice(e, f);
    if (!w) return;
    e->porta[w - e->voices] = glide;
    if (glide) e->porta_moving = 1;
    poly_play(e, w, moved.note, moved.peak, moved.velocity);
    if (!moved.key_down) fm_voice_key_up(w, 1); /* held by the pedal */
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
        uint8_t was = fm_voice_note(v);
        fm_voice_age(v, ++e->stamp);
        at_start(e, i);
        if (e->params[FM_PARAM_PORTAMENTO_MS] && fm_voice_active(v) && was != FM_VOICE_NO_NOTE) {
            /* sounding: on from where it is (no jump mid-glide) */
            e->porta[i] += ((int32_t)was - (int32_t)note) * 65536;
            if (e->porta[i]) e->porta_moving = 1;
        } else {
            porta_start(e, i, note);
        }
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
        porta_note(e, e->held[e->held_count - 1]);   /* (a legato return glides too) */
        unison_play(e, e->held[e->held_count - 1], 0, 0, 0);
        return;
    }
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (fm_voice_holding(v) != FM_VOICE_HELD) continue;
        fm_voice_age(v, ++e->stamp);
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
    porta_note(e, note);
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
        porta_start(e, (uint8_t)(v - e->voices), note);
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
        if (fm_voice_key_holds(v, note)) {
            fm_voice_age(v, ++e->stamp);
            fm_voice_key_up(v, e->pedal_down);
        }
    }
}

void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value)
{
    uint8_t i;
    if (controller == 64) e->pedal_down = value >= 64;
    if (controller == 1 || controller == 121) { /* the mod wheel; reset all controllers puts it down */
        e->wheel_to = controller == 1 ? (int32_t)(value & 127u) << 8 : 0;
        e->wheel_moving = (uint8_t)(e->wheel != e->wheel_to);
    }
    if (controller == 121) { /* and every pressure to 0 (#61), smoothed */
        e->at_chan = 0;
        for (i = 0; i < e->voice_count; i++) at_set(e, i, 0);
    }
    if (controller == 120 || controller == 123) unison_reset(e);
    if (controller == 120) fm_chorus_silence(&e->chorus); /* no wet tail */
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        switch (controller) {
        case 64: /* sustain pedal: up lets go of the notes it held */
            if (!e->pedal_down && fm_voice_holding(v) == FM_VOICE_PEDAL) release(e, v);
            break;
        case 120: /* all sound off: silent now */
            fm_voice_silence(v);
            break;
        case 123: /* all notes off: release, even notes the pedal holds */
            if (key_or_pedal(v)) release(e, v);
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

/* MIDI pressure 0..127 as Q1.30 */
static int32_t at_q30(uint8_t p)
{
    return (int32_t)(((int64_t)p << 30) / 127);
}

void fm_engine_channel_pressure(fm_engine *e, uint8_t pressure)
{
    uint8_t i;
    if (e->sample_rate == 0 || pressure > 127) return;
    e->at_chan = at_q30(pressure);
    for (i = 0; i < e->voice_count; i++) at_set(e, i, e->at_chan);
}

void fm_engine_poly_pressure(fm_engine *e, uint8_t note, uint8_t pressure)
{
    uint8_t i;
    if (e->sample_rate == 0 || note > 127 || pressure > 127) return;
    for (i = 0; i < e->voice_count; i++) {      /* in unison, every voice plays it */
        fm_voice *v = &e->voices[i];
        if (fm_voice_active(v) && fm_voice_note(v) == note)
            at_set(e, i, at_q30(pressure));
    }
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
 * The mix plays 3.17 dB over the old -12 dB of headroom, at PG-8X 2.0.13's
 * level (#102); the makeup gives 6 dB back. Up to the knee at -2.85 dBFS
 * it is a pure gain: the knee sat at -6 dBFS and moved up by the same
 * 3.17 dB, since PG-8X stays linear to -0.5 dBFS (a six-note two-DCO
 * chord), so what was linear here stays linear. Above it, d past the knee
 * becomes knee + h d / (h + d), h = full scale - knee: slope 1 at the
 * knee, monotonic, and short of full scale. That curve is read from a
 * table (tools/gen_softclip_table.py), linearly interpolated, so the render
 * path has no divide; past d = 2.0 it holds at 0.966 of full scale.
 */
#define OUT_KNEE 12083420 /* 0.7202 of full scale (-2.85 dBFS), after the makeup, in Q8.24 */
#define OUT_CLIP_SHIFT 18  /* table step: 2^18 in Q8.24 */
static const int32_t out_clip[129] = {
    0, 248278, 471610, 673576, 857103, 1024604,
    1178092, 1319254, 1449518, 1570099, 1682038, 1786232,
    1883457, 1974390, 2059624, 2139677, 2215007, 2286022,
    2353081, 2416505, 2476584, 2533573, 2587707, 2639194,
    2688223, 2734967, 2779582, 2822210, 2862980, 2902012,
    2939415, 2975288, 3009723, 3042806, 3074613, 3105218,
    3134688, 3163084, 3190465, 3216883, 3242389, 3267028,
    3290845, 3313880, 3336171, 3357753, 3378659, 3398922,
    3418569, 3437629, 3456128, 3474090, 3491538, 3508494,
    3524978, 3541010, 3556608, 3571790, 3586571, 3600969,
    3614996, 3628668, 3641998, 3654998, 3667681, 3680058,
    3692140, 3703937, 3715460, 3726718, 3737719, 3748473,
    3758987, 3769271, 3779330, 3789173, 3798807, 3808237,
    3817471, 3826514, 3835372, 3844051, 3852557, 3860894,
    3869067, 3877081, 3884942, 3892652, 3900217, 3907640,
    3914925, 3922077, 3929099, 3935994, 3942766, 3949418,
    3955953, 3962374, 3968685, 3974887, 3980984, 3986979,
    3992874, 3998671, 4004373, 4009983, 4015502, 4020932,
    4026276, 4031536, 4036714, 4041811, 4046830, 4051773,
    4056640, 4061434, 4066156, 4070809, 4075393, 4079910,
    4084361, 4088749, 4093073, 4097337, 4101540, 4105684,
    4109771, 4113801, 4117777,
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
           fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_PENV) ||
           fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_ATLFO);
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
    if (e->glide_moving && !(e->ticks % GLIDE_TICKS)) glide_tick(e);
    if (e->wheel_moving) wheel_tick(e);
    if (e->porta_moving) porta_tick(e);
    if (e->at_moving) at_tick(e);
    /* with every depth at 0 (after one tick that clears any offset still
     * applied) the voices have nothing to do */
    if (lfo_live(e))
        for (k = 0; k < e->voice_count; k++)
            if (fm_voice_active(&e->voices[k]))
                fm_voice_tick(&e->voices[k], lfo, &e->mod, e->ticks);
    e->mod_on = (uint8_t)(fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_LFO) ||
                          fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_PENV) ||
                          fm_mod_source_routed(&e->mod.routes, FM_MOD_SRC_ATLFO));
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
    if ((lfo_live(e) || e->glide_moving || e->wheel_moving || e->porta_moving || e->at_moving) &&
        n > e->tick_left)
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
