/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "chorus.h"
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
#define FM_ENGINE_BUDGET_32 5312u
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

/* Recomputes the derived settings a parameter feeds. */
static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = e->params[id];
    uint8_t i;
    switch (id) {
    case FM_PARAM_BEND_RANGE:
        apply_bend(e, e->bend_value);
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
    case FM_PARAM_OSC_XMOD:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_xmod(&e->voices[i].osc, fm_permille_to_q30(v));
        return;
    case FM_PARAM_OSC_SYNC:
        for (i = 0; i < FM_MAX_VOICES; i++)
            fm_osc_pair_set_sync(&e->voices[i].osc, (uint8_t)v);
        return;
    case FM_PARAM_OSC_MIX:
        for (i = 0; i < FM_MAX_VOICES; i++) {
            e->voices[i].mix_base = fm_permille_to_q30(v);
            if (!e->voices[i].mix_env) { /* else the next tick glides there */
                e->voices[i].mix_step = 0;
                fm_osc_pair_set_mix(&e->voices[i].osc, e->voices[i].mix_base);
            }
            fm_voice_mark_mix(&e->voices[i]);
        }
        return;
    case FM_PARAM_OSC_MIX_ENV_SOURCE: /* the filter envelope is caught up when read */
        for (i = 0; i < FM_MAX_VOICES; i++)
            e->voices[i].mix_src = (uint8_t)v;
        return;
    case FM_PARAM_OSC_MIX_ENV:
        for (i = 0; i < FM_MAX_VOICES; i++) {
            e->voices[i].mix_env = fm_permille_to_q30(v); /* off: glides back to osc_mix */
            fm_voice_mark_mix(&e->voices[i]);
        }
        return;
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
        /* cents -> Q16.16 octaves: exact for multiples of 75 cents */
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_LFO, FM_MOD_PITCH,
                         v * 65536 / 1200);
        return;
    case FM_PARAM_AMP_VELOCITY: /* read at the next note on */
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
    if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS) /* every voice's, shared */
        fm_mod_set_route(&e->mod.routes, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF, v);
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
    /* each voice at 1 / voice_count, then -6 dB of headroom */
    e->mix_gain = (int32_t)((1u << 29) / voice_count);
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
 */
static int32_t velocity_peak(const fm_engine *e, uint8_t velocity)
{
    int32_t sens = e->params[FM_PARAM_AMP_VELOCITY];
    int64_t curve = (int64_t)velocity * velocity * FM_ENV_FULL / (127 * 127);
    return FM_ENV_FULL - (int32_t)((FM_ENV_FULL - curve) * sens / 100);
}

/* Poly: voice `v` plays `note` at amp peak `peak` (in poly every detune
 * is 0). */
static void poly_play(fm_engine *e, fm_voice *v, uint8_t note, int32_t peak)
{
    uint8_t i = (uint8_t)(v - e->voices);
    v->stamp = ++e->stamp;
    fm_voice_note_on(v, &e->mod, note, voice_pitch(e, i, note), peak, e->ticks + 1,
                     e->sample_rate, e->fade_samples);
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
 * the newest note wins. `peak` is the new strike's; the waiting note keeps
 * its own, which `f` still holds in next_peak.
 */
static void take_back(fm_engine *e, fm_voice *f, uint8_t note, int32_t peak)
{
    uint8_t moved = f->note, key_down = f->key_down, sustained = f->sustained;
    fm_voice *w;
    fm_voice_take_back(f, &e->mod, voice_pitch(e, (uint8_t)(f - e->voices), note),
                       peak, e->ticks + 1, e->sample_rate);
    f->stamp = ++e->stamp;
    if (!key_down && !sustained) return;
    w = pick_voice(e, f);
    if (!w) return;
    poly_play(e, w, moved, f->next_peak);
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
 * `peak` (a legato change ignores it and keeps each voice's level). */
static void unison_play(fm_engine *e, uint8_t note, int retrigger,
                        int32_t peak)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        v->stamp = ++e->stamp;
        fm_voice_change_note(v, &e->mod, note, voice_pitch(e, i, note), peak,
                             e->ticks + 1, e->sample_rate, retrigger);
    }
}

/* Unison: the newest note wins and retriggers (docs/voices.md#unison). */
static void unison_note_on(fm_engine *e, uint8_t note, int32_t peak)
{
    uint8_t i;
    forget_held(e, note);
    if (e->held_count == FM_HELD_MAX) { /* full: drop the oldest key */
        for (i = 1; i < FM_HELD_MAX; i++) e->held[i - 1] = e->held[i];
        e->held_count--;
    }
    e->held[e->held_count++] = note;
    unison_play(e, note, 1, peak); /* a legato return later keeps it */
}

/* Unison: releasing the sounding note goes back to the newest key still
 * down, legato; releasing the last one releases (or the pedal holds). */
static void unison_note_off(fm_engine *e, uint8_t note)
{
    uint8_t i;
    if (!forget_held(e, note)) return; /* not the sounding note */
    if (e->held_count) {
        unison_play(e, e->held[e->held_count - 1], 0, 0);
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
        unison_note_on(e, note, velocity_peak(e, velocity));
        return;
    }
    {
        int32_t peak = velocity_peak(e, velocity);
        fm_voice *v = find_note(e, note);
        if (!v) {
            fm_voice *f = find_fading(e, note);
            if (f) {
                take_back(e, f, note, peak);
                return;
            }
            v = pick_voice(e, 0);
        }
        poly_play(e, v, note, peak);
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

/* Q8.24 to int16 with saturation (1.0 maps to 32768). */
static int16_t to_int16(int32_t q24)
{
    int32_t s = q24 >> 9;
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

/* The LFO reaches some destination at a nonzero depth. */
static int lfo_routed(const fm_engine *e)
{
    unsigned d;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++)
        if (e->mod.routes.depth[FM_MOD_SRC_LFO][d] != 0) return 1;
    return 0;
}

/*
 * Control tick, every FM_LFO_TICK_FRAMES samples counted from init (so
 * the caller's block size never moves it): the LFO advances and each
 * sounding voice applies it.
 */
static void control_tick(fm_engine *e)
{
    int32_t lfo = fm_lfo_tick(&e->lfo);
    int depth = lfo_routed(e);
    uint8_t k;
    e->ticks++;
    e->tick_left = FM_LFO_TICK_FRAMES;
    /* with every depth at 0 (after one tick that clears any offset still
     * applied) the voices have nothing to do */
    if (depth || e->mod_on)
        for (k = 0; k < e->voice_count; k++)
            if (fm_voice_active(&e->voices[k]))
                fm_voice_tick(&e->voices[k], lfo, &e->mod, e->ticks);
    e->mod_on = (uint8_t)depth;
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
    if ((e->mod_on || lfo_routed(e)) &&
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
