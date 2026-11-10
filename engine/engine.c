/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "fm1_8p.h"

/*
 * Note frequencies for MIDI notes 120..131 in Q16.16 Hz
 * (440 * 2^((n - 69) / 12) * 65536, rounded). Lower octaves are exact
 * right shifts of these. Regenerate with tools/gen_pitch_table.py.
 */
static const uint32_t top_octave_hz_q16[12] = {
    548668578u, 581294109u, 615859655u, 652480576u,
    691279090u, 732384684u, 775934544u, 822074013u,
    870957077u, 922746880u, 977616265u, 1035748353u
};

/* Phase increment (Q0.32 per sample) for a MIDI note at a sample rate. */
static uint32_t note_increment(uint8_t note, uint32_t sample_rate)
{
    uint32_t hz_q16 = top_octave_hz_q16[note % 12] >> (10 - note / 12);
    return (uint32_t)(((uint64_t)hz_q16 << 16) / sample_rate);
}

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count)
{
    if (!e || sample_rate == 0 || voice_count == 0 ||
        voice_count > FM_MAX_VOICES)
        return FM_ERR_ARG;
    e->sample_rate = sample_rate;
    e->voice_count = voice_count;
    e->osc.phase = 0;
    e->osc.inc = 0;
    return FM_OK;
}

void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity)
{
    (void)velocity;
    if (note > 127) return;
    e->osc.inc = note_increment(note, e->sample_rate);
}

void fm_engine_note_off(fm_engine *e, uint8_t note)
{
    (void)e;
    (void)note;
}

void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int16_t s = (int16_t)((int32_t)e->osc.phase >> 17);
        e->osc.phase += e->osc.inc;
        out[2 * i] = s;
        out[2 * i + 1] = s;
    }
}
