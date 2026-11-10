/*
 * fm1_8p.h - public interface of the fm1-8p synth engine.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Plain C99, fixed-point only, no hardware or OS code, no dynamic
 * allocation: the caller owns the fm_engine storage (static or on a task
 * stack) and fm_engine_init sizes everything inside it. See
 * docs/fixed-point.md for the number formats.
 *
 * The struct layout is exposed only so callers can allocate it; treat
 * every field as private.
 */
#ifndef FM1_8P_H
#define FM1_8P_H

#include <stdint.h>

#include "osc.h"

#define FM_MAX_VOICES 8

typedef enum {
    FM_OK = 0,
    FM_ERR_ARG = -1 /* bad argument (null pointer, rate or voice count) */
} fm_status;

typedef struct {
    uint32_t sample_rate;
    uint8_t voice_count;
    fm_osc osc;
} fm_engine;

fm_status fm_engine_init(fm_engine *e, uint32_t sample_rate,
                         uint8_t voice_count);
void fm_engine_note_on(fm_engine *e, uint8_t note, uint8_t velocity);
void fm_engine_note_off(fm_engine *e, uint8_t note);

/* Renders `frames` interleaved stereo frames (L, R) of signed 16-bit. */
void fm_engine_render(fm_engine *e, int16_t *out, uint32_t frames);

#endif /* FM1_8P_H */
