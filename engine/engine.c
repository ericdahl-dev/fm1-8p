/*
 * engine.c - fm1-8p engine entry points.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "fm1_8p.h"

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count)
{
    if (!e || sample_rate == 0 || voice_count == 0 ||
        voice_count > FM_MAX_VOICES)
        return FM_ERR_ARG;
    e->sample_rate = sample_rate;
    e->voice_count = voice_count;
    fm_osc_reset(&e->osc);
    return FM_OK;
}

void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity)
{
    (void)velocity;
    if (note > 127) return;
    fm_osc_set_inc(&e->osc, fm_osc_note_inc(note, e->sample_rate));
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
        int16_t s = (int16_t)(fm_osc_saw_next(&e->osc) >> 10);
        out[2 * i] = s;
        out[2 * i + 1] = s;
    }
}
