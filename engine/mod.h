/*
 * mod.h - modulation destinations: where each voice's modulation sums
 * (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * A source (the LFO, ...) reaches a destination (pitch, pulse width, ...)
 * through a route depth. Once a control tick each voice sums every
 * source times its depth into each destination. The depths are one
 * fm_mod_routes shared by every voice (owned by the engine); the sums
 * are each voice's own fm_mod. See docs/modulation.md.
 */
#ifndef FM_MOD_H
#define FM_MOD_H

#include <stdint.h>

/* Sources: values in Q1.30, passed to each tick. */
enum { FM_MOD_SRC_LFO, FM_MOD_SRC_COUNT };

/* Destinations, each summed in its own unit: pitch in Q16.16 octaves,
 * pulse width in Q0.32. */
enum { FM_MOD_PITCH, FM_MOD_PW, FM_MOD_DEST_COUNT };

/* Route depths, in each destination's unit at full source. */
typedef struct {
    int32_t depth[FM_MOD_SRC_COUNT][FM_MOD_DEST_COUNT];
} fm_mod_routes;

/* One voice's sums. */
typedef struct {
    int32_t sum[FM_MOD_DEST_COUNT];
} fm_mod;

/* At rest: every sum 0. */
void fm_mod_reset(fm_mod *m);

/* Sets the depth from source `src` to destination `dest`. */
void fm_mod_set_route(fm_mod_routes *r, unsigned src, unsigned dest,
                      int32_t depth);

/* One control tick: sums the sources `src` (FM_MOD_SRC_COUNT values,
 * Q1.30) through the routes `r`. Returns the destinations whose sum
 * changed, bit 1 << dest. */
uint32_t fm_mod_tick(fm_mod *m, const fm_mod_routes *r, const int32_t *src);

/* The sum at destination `dest`. */
int32_t fm_mod_get(const fm_mod *m, unsigned dest);

#endif /* FM_MOD_H */
