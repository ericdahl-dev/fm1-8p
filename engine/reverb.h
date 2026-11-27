/*
 * reverb.h - the reverb after the chorus (#197; internal to the engine;
 * not part of the public interface). Mono in, the chorus's two sides
 * summed; stereo out, added to both sides (a send: the dry stays). Its
 * delay lines are int16 in memory the caller gives it.
 *
 * Two designs, picked at init; a device build compiles one
 * (FM_REVERB_ONE with FM_REVERB_DESIGN), so the CPU rows can run each:
 * - FM_REVERB_FDN4: two input allpasses into four lines mixed by a 4x4
 *   Hadamard matrix, a one-pole lowpass and a gain in each, two of them
 *   swaying slowly (Jot 1991; Stautner and Puckette 1982).
 * - FM_REVERB_PLATE: Dattorro's figure-of-eight tank (J. Audio Eng. Soc.
 *   45(9), 1997), at half the sample rate.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_REVERB_H
#define FM_REVERB_H

#include <stdint.h>

#include "reverb_state.h"

/* The int16 words DESIGN's lines need at SAMPLE_RATE Hz (0 for an unknown
 * design, or one this build left out). */
uint32_t fm_reverb_words(uint8_t design, uint32_t sample_rate);

/*
 * Sets up `r`: DESIGN at SAMPLE_RATE Hz, its lines in MEM (WORDS int16
 * words). With no memory, or too little, it stays off whatever its
 * settings. Off (mix 0) until fm_reverb_set.
 */
void fm_reverb_init(fm_reverb *r, uint8_t design, uint32_t sample_rate,
                    int16_t *mem, uint32_t words);

/*
 * MIX_PCT 0..100 (0 off: the engine skips it), TIME_MS the decay to
 * -60 dB, TONE_PCT 0..100 (100: no damping; lower darkens the tail and
 * shortens its highs). Turning it on from off starts from silent lines.
 */
void fm_reverb_set(fm_reverb *r, int32_t mix_pct, int32_t time_ms,
                   int32_t tone_pct);

/* Empties the lines (all sound off); the settings are kept. */
void fm_reverb_silence(fm_reverb *r);

/*
 * Adds the wet of a block to `left` and `right` (Q8.24), in place; its
 * input is their mean.
 */
void fm_reverb_process_block(fm_reverb *r, int32_t *left, int32_t *right,
                             uint32_t frames);

/* Whether it sounds (the engine skips it entirely when not). */
#define fm_reverb_is_on(r) ((r)->mix != 0 && (r)->ready)

#endif /* FM_REVERB_H */
