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

/* Phase increment for `note` with the current bend. */
static uint32_t note_inc(const fm_engine *e, uint8_t note)
{
    return fm_osc_pitch_inc(((int32_t)note << 16) + e->bend, e->sample_rate);
}

/* Retunes every sounding voice after the bend changes. A stolen voice's old note
 * keeps its pitch for the few ms it takes to fade. */
static void retune(fm_engine *e)
{
    uint8_t i;
    for (i = 0; i < e->voice_count; i++) /* idle voices get theirs at note on */
        if (fm_voice_active(&e->voices[i]))
            fm_voice_set_inc(&e->voices[i], note_inc(e, e->voices[i].note));
}

/* MIDI bend (-8192..8191 from center) to Q16.16 semitones: exact, since
 * value / 8192 * range * 65536 = value * range * 8. */
static void apply_bend(fm_engine *e, int32_t centered)
{
    e->bend = centered * e->params[FM_PARAM_BEND_RANGE] * 8;
    retune(e);
}

/* Recomputes the derived settings a parameter feeds. */
static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = e->params[id];
    uint8_t i;
    if (id == FM_PARAM_BEND_RANGE) {
        apply_bend(e, e->bend_value);
        return;
    }
    if (id == FM_PARAM_CHORUS_MODE) {
        fm_chorus_set_mode(&e->chorus, (uint8_t)v);
        return;
    }
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
    e->bend = 0;
    e->bend_value = 0;
    e->fade_samples = ms_to_samples(e, STEAL_FADE_MS);
    for (i = 0; i < FM_MAX_VOICES; i++) fm_voice_reset(&e->voices[i]);
    fm_chorus_init(&e->chorus, sample_rate);
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

/* Releases a voice's note and ages it from now. */
static void release(fm_engine *e, fm_voice *v)
{
    v->stamp = ++e->stamp;
    fm_voice_release(v);
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
 * the newest note wins.
 */
static void take_back(fm_engine *e, fm_voice *f, uint8_t note)
{
    uint8_t moved = f->note, key_down = f->key_down, sustained = f->sustained;
    fm_voice *w;
    fm_voice_take_back(f, note_inc(e, note));
    f->stamp = ++e->stamp;
    if (!key_down && !sustained) return;
    w = pick_voice(e, f);
    if (!w) return;
    w->stamp = ++e->stamp;
    fm_voice_note_on(w, moved, note_inc(e, moved), e->fade_samples);
    if (!key_down) fm_voice_key_up(w, 1); /* held by the pedal */
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
        if (!v) {
            fm_voice *f = find_fading(e, note);
            if (f) {
                take_back(e, f, note);
                return;
            }
            v = pick_voice(e, 0);
        }
        v->stamp = ++e->stamp;
        fm_voice_note_on(v, note, note_inc(e, note), e->fade_samples);
    }
}

void fm_engine_note_off(fm_engine *e, uint8_t note)
{
    uint8_t i;
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

void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames)
{
    while (frames) {
        uint32_t n = frames < FM_MIX_FRAMES ? frames : FM_MIX_FRAMES, i;
        uint8_t k;
        for (i = 0; i < n; i++) e->mix[i] = 0;
        for (k = 0; k < e->voice_count; k++)
            if (fm_voice_active(&e->voices[k]))
                fm_voice_render_add(&e->voices[k], e->mix, n);
        for (i = 0; i < n; i++) {
            /* sum of the voices (Q8.24) times the mix gain (Q1.30) */
            int32_t s = (int32_t)(((int64_t)e->mix[i] * e->mix_gain) >> 30);
            int32_t left, right;
            fm_chorus_process(&e->chorus, s, &left, &right);
            out[2 * i] = to_int16(left);
            out[2 * i + 1] = to_int16(right);
        }
        out += 2 * n;
        frames -= n;
    }
}
