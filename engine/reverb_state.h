/*
 * reverb_state.h - the reverb's state (#197; internal to the engine;
 * fm1_8p.h includes it only so callers can size fm_engine). The delay
 * lines are not in it: the caller gives the engine their memory
 * (fm_engine_set_reverb_memory), so fm_engine stays small and the device
 * keeps them in the module's .bss.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_REVERB_STATE_H
#define FM_REVERB_STATE_H

#include <stdint.h>

/* The designs (one is built for the device: FM_REVERB_DESIGN) */
#define FM_REVERB_FDN4 1u  /* four lines, a Hadamard mix (Jot, Stautner and Puckette) */
#define FM_REVERB_PLATE 2u /* Dattorro's plate (1997), at half the rate */

#define FM_REVERB_LINES 12u

/* One delay line in the caller's memory: `len` samples, `w` the next
 * write. */
typedef struct {
    int16_t *b;
    uint32_t len, w;
} fm_rv_line;

typedef struct {
    int16_t *mem;       /* the caller's memory, or null: no reverb */
    uint32_t words;
    uint32_t sample_rate;
    uint8_t design;
    uint8_t ready;      /* the lines fit in mem */
    uint8_t odd;        /* the plate: the second sample of a pair is next */
    uint8_t clear;      /* the lines must be emptied before the next sample */
    int32_t mix;        /* the wet's level, Q15 (0: off) */
    int32_t gain[4];    /* per line (FDN4) or the tank's decay ([0], plate), Q15 */
    int32_t damp;       /* the damping one-pole's coefficient, Q15 (32767: none) */
    int32_t lp[4];      /* the damping lowpasses' state */
    uint32_t ph[2];     /* the swaying lines' LFO phases */
    uint32_t ph_inc[2];
    uint32_t sw_i[2];   /* their read this block: whole samples back ... */
    int32_t sw_eta[2];  /* ... and the allpass interpolator's coefficient, Q15 */
    int32_t sw_y[2];    /* the interpolators' last outputs */
    int32_t in_acc;     /* the plate: the pair's first input */
    uint32_t exc;       /* the plate: its allpasses' excursion, samples */
    int32_t yl, yr;     /* the plate: the newest output pair */
    int32_t pl, pr;     /* the plate: the one before it */
    uint16_t tap[14];   /* the plate's output taps, scaled to its rate */
    fm_rv_line line[FM_REVERB_LINES];
} fm_reverb;

#endif /* FM_REVERB_STATE_H */
