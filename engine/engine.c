/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "fm1_8p.h"

#define MAX_SAMPLE_RATE 192000u
/* A stolen voice fades out over this long before its new note starts. */
#define STEAL_FADE_MS 3

/*
 * The engine relies on >> of a negative value being an arithmetic shift
 * (implementation-defined in C99; true for GCC, Clang and the JieLi
 * toolchain). Refuse to build where it is not.
 */
typedef char fm_require_arithmetic_shift[((-8 >> 1) == -4) ? 1 : -1];

/* Milliseconds to samples at the engine rate (fits: 10 s * 192 kHz). */
static uint32_t ms_to_samples(const fm_engine *e, int32_t ms)
{
    return (uint32_t)(((uint64_t)(uint32_t)ms * e->sample_rate) / 1000u);
}

/* Recomputes the derived settings a parameter feeds. */
static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = e->params[id];
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_env *env = &e->voices[i].amp_env;
        switch (id) {
        case FM_PARAM_AMP_ATTACK_MS:
            env->attack_samples = ms_to_samples(e, v);
            break;
        case FM_PARAM_AMP_DECAY_MS:
            env->decay_samples = ms_to_samples(e, v);
            break;
        case FM_PARAM_AMP_SUSTAIN:
            /* per mille -> Q1.30 */
            env->sustain = (int32_t)(((int64_t)v << 30) / 1000);
            break;
        case FM_PARAM_AMP_RELEASE_MS:
            env->release_samples = ms_to_samples(e, v);
            break;
        default:
            break;
        }
        fm_env_retime(env);
    }
}

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count)
{
    uint16_t id;
    uint8_t i;
    if (!e || sample_rate == 0 || sample_rate > MAX_SAMPLE_RATE ||
        voice_count == 0 || voice_count > FM_MAX_VOICES)
        return FM_ERR_ARG;
    e->sample_rate = sample_rate;
    e->voice_count = voice_count;
    /* each voice at 1 / voice_count, then -6 dB of headroom */
    e->mix_gain = (int32_t)((1u << 29) / voice_count);
    e->stamp = 0;
    e->pedal_down = 0;
    e->fade_samples = ms_to_samples(e, STEAL_FADE_MS);
    for (i = 0; i < FM_MAX_VOICES; i++) {
        fm_osc_reset(&e->voices[i].osc);
        fm_env_reset(&e->voices[i].amp_env);
        e->voices[i].note = 0;
        e->voices[i].key_down = 0;
        e->voices[i].stamp = 0;
        e->voices[i].next_inc = 0;
        e->voices[i].pending = 0;
        e->voices[i].sustained = 0;
    }
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        e->params[id] = fm_param_info_get(id)->def;
        apply_param(e, id);
    }
    return FM_OK;
}

fm_status fm_engine_set_param(fm_engine *e, uint16_t id, int32_t value)
{
    const fm_param_info *info = fm_param_info_get(id);
    if (!e || !info) return FM_ERR_ARG;
    if (value < info->min) value = info->min;
    if (value > info->max) value = info->max;
    e->params[id] = value;
    apply_param(e, id);
    return FM_OK;
}

int32_t fm_engine_get_param(const fm_engine *e, uint16_t id)
{
    return (e && id < FM_PARAM_COUNT) ? e->params[id] : 0;
}

/* Starts the voice's note from silence (or retriggers it). */
static void start_note(fm_voice *v)
{
    v->pending = 0;
    fm_osc_set_inc(&v->osc, v->next_inc);
    fm_env_gate_on(&v->amp_env);
    if (!v->key_down && !v->sustained) fm_env_gate_off(&v->amp_env);
}

/* Lets go of a voice's note: it releases now, or once its fade ends. */
static void release_voice(fm_engine *e, fm_voice *v)
{
    v->key_down = 0;
    v->sustained = 0;
    v->stamp = ++e->stamp;
    if (!v->pending) fm_env_gate_off(&v->amp_env);
}

/* The sounding voice that has `note`, or null. At most one voice has a
 * given note, because a repeated note reuses its voice. */
static fm_voice *find_note(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->note == note &&
            (v->amp_env.stage != FM_ENV_IDLE || v->pending))
            return v;
    }
    return 0;
}

/* Steal preference: lower ranks are taken first. */
static int voice_rank(const fm_voice *v)
{
    if (v->amp_env.stage == FM_ENV_IDLE) return 0;
    if (v->key_down) return 3;   /* held */
    if (v->sustained) return 2;  /* held by the pedal */
    return 1;                    /* releasing */
}

/*
 * The voice a new note takes: an idle one if any, else the oldest
 * released one, else the oldest held one. "Oldest" is the longest since
 * its last note on or off (stamps compared as a wrapping difference).
 */
static fm_voice *pick_voice(fm_engine *e)
{
    fm_voice *best = &e->voices[0];
    int best_rank = voice_rank(best);
    uint8_t i;
    for (i = 1; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        int r = voice_rank(v);
        if (r < best_rank ||
            (r == best_rank && (int32_t)(v->stamp - best->stamp) < 0)) {
            best = v;
            best_rank = r;
        }
    }
    return best;
}

void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity)
{
    if (e->sample_rate == 0 || note > 127 || velocity > 127)
        return; /* not initialized, or not MIDI */
    if (velocity == 0) {
        fm_engine_note_off(e, note);
        return;
    }
    {
        fm_voice *v = find_note(e, note);
        int retrigger = v != 0;
        if (!v) v = pick_voice(e);
        v->note = note;
        v->key_down = 1;
        v->sustained = 0;
        v->stamp = ++e->stamp;
        v->next_inc = fm_osc_note_inc(note, e->sample_rate);
        if (v->pending) {
            /* already waiting for its fade to end */
        } else if (retrigger || v->amp_env.stage == FM_ENV_IDLE) {
            start_note(v); /* attack from the current level */
        } else { /* steal: fade the old note out, start this one after */
            v->pending = 1;
            fm_env_fade(&v->amp_env, e->fade_samples);
        }
    }
}

void fm_engine_note_off(fm_engine *e, uint8_t note)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        if (v->key_down && v->note == note) {
            if (e->pedal_down) {
                v->key_down = 0;
                v->sustained = 1;
                v->stamp = ++e->stamp;
            } else {
                release_voice(e, v);
            }
        }
    }
}

void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value)
{
    uint8_t i;
    if (controller == 64) e->pedal_down = value >= 64;
    for (i = 0; i < e->voice_count; i++) {
        fm_voice *v = &e->voices[i];
        switch (controller) {
        case 64: /* sustain pedal: up lets go of the notes it held */
            if (!e->pedal_down && v->sustained) release_voice(e, v);
            break;
        case 120: /* all sound off: silent now */
            fm_env_reset(&v->amp_env);
            v->pending = 0;
            v->key_down = 0;
            v->sustained = 0;
            break;
        case 123: /* all notes off: release, even notes the pedal holds */
            if (v->key_down || v->sustained) release_voice(e, v);
            break;
        default:
            break;
        }
    }
}

uint8_t fm_engine_active_voices(const fm_engine *e)
{
    uint8_t i, n = 0;
    for (i = 0; i < e->voice_count; i++)
        if (e->voices[i].amp_env.stage != FM_ENV_IDLE || e->voices[i].pending)
            n++;
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

void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t sum = 0, s;
        uint8_t k;
        for (k = 0; k < e->voice_count; k++) {
            fm_voice *v = &e->voices[k];
            int32_t level, saw;
            if (v->amp_env.stage == FM_ENV_IDLE) {
                if (!v->pending) continue;
                start_note(v); /* a stolen voice has faded out */
            }
            level = fm_env_next(&v->amp_env);
            saw = fm_osc_saw_next(&v->osc);
            /* Q8.24 * Q1.30 -> Q8.24 */
            sum += (int32_t)(((int64_t)saw * level) >> 30);
        }
        s = (int32_t)(((int64_t)sum * e->mix_gain) >> 30);
        out[2 * i] = to_int16(s);
        out[2 * i + 1] = out[2 * i];
    }
}
