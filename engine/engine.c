/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "fm1_8p.h"

#define MAX_SAMPLE_RATE 192000u

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

/* Percent (0..100) to a Q0.32 fraction; 2^32 / 100 rounded. */
static uint32_t pct_to_q32(int32_t pct)
{
    return (uint32_t)pct * 42949673u;
}

/*
 * Oscillator `which`'s settings. Each oscillator's parameters run in the
 * same order (wave, pulse width, octave, fine), so `rel` is the offset
 * from its wave parameter.
 */
static void apply_osc_param(fm_engine *e, uint8_t which, int rel)
{
    const int32_t *p = &e->params[which ? FM_PARAM_OSC2_WAVE : FM_PARAM_OSC1_WAVE];
    switch (rel) {
    case 0:
        fm_osc_pair_set_wave(&e->osc, which, (uint8_t)p[0]);
        break;
    case 1:
        fm_osc_pair_set_width(&e->osc, which, pct_to_q32(p[1]));
        break;
    default: /* octave or fine tune */
        fm_osc_pair_set_tune(&e->osc, which, p[2] * 1200 + p[3]);
        break;
    }
}

/* Recomputes the derived settings a parameter feeds. */
static void apply_param(fm_engine *e, uint16_t id)
{
    int32_t v = e->params[id];
    switch (id) {
    case FM_PARAM_AMP_ATTACK_MS:
        e->amp_env.attack_samples = ms_to_samples(e, v);
        break;
    case FM_PARAM_AMP_DECAY_MS:
        e->amp_env.decay_samples = ms_to_samples(e, v);
        break;
    case FM_PARAM_AMP_SUSTAIN:
        /* per mille -> Q1.30 */
        e->amp_env.sustain = (int32_t)(((int64_t)v << 30) / 1000);
        break;
    case FM_PARAM_AMP_RELEASE_MS:
        e->amp_env.release_samples = ms_to_samples(e, v);
        break;
    case FM_PARAM_OSC1_WAVE:
    case FM_PARAM_OSC1_PULSE_WIDTH_PCT:
    case FM_PARAM_OSC1_OCTAVE:
    case FM_PARAM_OSC1_FINE_CENTS:
        apply_osc_param(e, 0, id - FM_PARAM_OSC1_WAVE);
        break;
    case FM_PARAM_OSC2_WAVE:
    case FM_PARAM_OSC2_PULSE_WIDTH_PCT:
    case FM_PARAM_OSC2_OCTAVE:
    case FM_PARAM_OSC2_FINE_CENTS:
        apply_osc_param(e, 1, id - FM_PARAM_OSC2_WAVE);
        break;
    case FM_PARAM_OSC_MIX:
        /* per mille -> Q1.30 */
        fm_osc_pair_set_mix(&e->osc, (int32_t)(((int64_t)v << 30) / 1000));
        break;
    default:
        break;
    }
    fm_env_retime(&e->amp_env);
}

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count)
{
    uint16_t id;
    if (!e || sample_rate == 0 || sample_rate > MAX_SAMPLE_RATE ||
        voice_count == 0 || voice_count > FM_MAX_VOICES)
        return FM_ERR_ARG;
    e->sample_rate = sample_rate;
    e->voice_count = voice_count;
    fm_osc_pair_reset(&e->osc);
    fm_env_reset(&e->amp_env);
    e->note = 0;
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

void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity)
{
    if (e->sample_rate == 0 || note > 127 || velocity > 127)
        return; /* not initialized, or not MIDI */
    if (velocity == 0) {
        fm_engine_note_off(e, note);
        return;
    }
    e->note = note;
    fm_osc_pair_set_note(&e->osc, note, e->sample_rate);
    fm_env_gate_on(&e->amp_env);
}

void fm_engine_note_off(fm_engine *e, uint8_t note)
{
    if (note == e->note) fm_env_gate_off(&e->amp_env);
}

void fm_engine_controller(fm_engine *e, uint8_t controller, uint8_t value)
{
    (void)value;
    switch (controller) {
    case 120: /* all sound off */
        fm_env_reset(&e->amp_env);
        break;
    case 123: /* all notes off */
        fm_env_gate_off(&e->amp_env);
        break;
    default:
        break;
    }
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
        int32_t level = fm_env_next(&e->amp_env);
        int32_t s = 0;
        if (e->amp_env.stage != FM_ENV_IDLE) {
            int32_t osc = fm_osc_pair_next(&e->osc);
            /* Q8.24 * Q1.30 -> Q8.24, then the fixed voice gain of 0.5 */
            s = (int32_t)(((int64_t)osc * level) >> 31);
        }
        out[2 * i] = to_int16(s);
        out[2 * i + 1] = out[2 * i];
    }
}
