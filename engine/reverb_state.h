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

#ifndef FM_REVERB_DESIGN
#define FM_REVERB_DESIGN FM_REVERB_FDN4
#endif

/* The int16 words FM_REVERB_DESIGN's lines take at 44.1 kHz, the
 * device's rate (fm_engine_reverb_words; tests/test_reverb.c holds them
 * equal): powers of two (reverb.c, layout), 32 KiB for the FDN4, 34 KiB for the plate */
#define FM_REVERB_WORDS_44K (FM_REVERB_DESIGN == FM_REVERB_PLATE ? 16384u + 1024u : 16384u)

#define FM_REVERB_LINES 12u

typedef struct {
    int16_t *mem;       /* the caller's memory, or null: no reverb */
    uint32_t words;
    uint32_t sample_rate;
    uint8_t design;
    uint8_t ready;      /* the lines fit in mem */
    uint8_t odd;        /* the plate: the second sample of a pair is next */
    uint8_t clear;      /* the lines must be emptied before the next sample */
    int32_t mix;        /* the wet's level now, Q15: it glides to mix_to, MIX_GLIDE a block (0 and mix_to 0: off) */
    int32_t mix_to;     /* the level MIX asks for, Q15 */
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
    int16_t *buf, *buf2; /* the lines (reverb.c, layout): the main ones, and the plate's input diffusers */
    uint32_t mask, mask2;
    uint32_t p;          /* the pointer the lines share, one step down a sample (the plate: a tick) */
    uint16_t base[FM_REVERB_LINES], len[FM_REVERB_LINES];
    uint32_t quiet;      /* samples (the plate: ticks) every line has taken only zeros, its states 0 */
    uint32_t longest;    /* the longer buffer: quiet past a full sweep of it, every word is 0 */
    uint8_t idle;        /* the last block skipped the loop */
} fm_reverb;

#endif /* FM_REVERB_STATE_H */
