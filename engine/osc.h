/*
 * osc.h - band-limited oscillator (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_OSC_H
#define FM_OSC_H

#include <stdint.h>

typedef struct {
    uint32_t phase; /* Q0.32, one full cycle = 2^32 */
    uint32_t inc;   /* Q0.32 phase increment per sample, < 2^31 */
} fm_osc;

/* Highest allowed increment: just under Nyquist. */
#define FM_OSC_MAX_INC 0x7FFF0000u

/* Phase increment (Q0.32 per sample) for a MIDI note at a sample rate. */
uint32_t fm_osc_note_inc(uint8_t note, uint32_t sample_rate);

void fm_osc_reset(fm_osc *o);
void fm_osc_set_inc(fm_osc *o, uint32_t inc);

/* Next sample of a polyBLEP saw, Q8.24 (nominal range -1.0..+1.0). */
int32_t fm_osc_saw_next(fm_osc *o);

#endif /* FM_OSC_H */
