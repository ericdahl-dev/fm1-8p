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
#ifdef FM_CHECK_ENGINE
#include <assert.h>
#endif

/* Sources, values in Q1.30: the LFO (after the voice's delay and fade),
 * the note's velocity (its amp level, velocity curve applied), envelope 1
 * (the filter envelope's level) and envelope 2 (the amp envelope's level
 * as a share of the note's peak, so velocity doesn't scale it). */
enum { FM_MOD_SRC_LFO, FM_MOD_SRC_VEL, FM_MOD_SRC_ENV1, FM_MOD_SRC_ENV2,
       FM_MOD_SRC_COUNT };

/* Destinations, each summed in its own unit: pitch in Q16.16 octaves,
 * pulse width in Q0.32, level (the amp envelope's peak) in Q1.30, cutoff
 * in cents (added to the filter's base and key tracking), mix in Q1.30
 * (added to osc2_level). */
enum { FM_MOD_PITCH, FM_MOD_PW, FM_MOD_LEVEL, FM_MOD_CUTOFF, FM_MOD_MIX,
       FM_MOD_DEST_COUNT };

/* Route depths, in each destination's unit at full source. */
typedef struct {
    int32_t depth[FM_MOD_SRC_COUNT][FM_MOD_DEST_COUNT];
} fm_mod_routes;

/* One voice's sources (as last set) and sums. */
typedef struct {
    int32_t src[FM_MOD_SRC_COUNT];
    int32_t sum[FM_MOD_DEST_COUNT];
} fm_mod;

/* At rest: every source and every sum 0. */
void fm_mod_reset(fm_mod *m);

/* Every route at depth 0. */
void fm_mod_routes_reset(fm_mod_routes *r);

/* Sets the depth from source `src` to destination `dest`. */
void fm_mod_set_route(fm_mod_routes *r, unsigned src, unsigned dest,
                      int32_t depth);

/* Destination `d`'s sum of `nsrc` sources `src` (Q1.30) through the depth
 * rows `depth` (one row per source), clamped to int32. */
static inline int32_t fm_mod_dest_sum(const int32_t (*depth)[FM_MOD_DEST_COUNT],
                                      const int32_t *src, unsigned nsrc,
                                      unsigned d)
{
    int64_t sum = 0;
    int32_t v;
    unsigned s;
    for (s = 0; s < nsrc; s++)
        if (depth[s][d]) /* most routes are off: skip their multiply */
            /* Q1.30 * depth -> depth's unit */
            sum += ((int64_t)src[s] * depth[s][d]) >> 30;
    v = (int32_t)sum;
    if (v != sum) v = sum < 0 ? INT32_MIN : INT32_MAX; /* clamp, not wrap */
    return v;
}

/* Sums `nsrc` sources `src` (Q1.30) through the depth rows `depth` (one
 * row per source) into `m`, each destination clamped to int32. Returns
 * the destinations whose sum changed, bit 1 << dest. Inline: each voice
 * runs it every tick, and inline it sits in the voice's code instead of a
 * cache line of its own. */
static inline uint32_t fm_mod_sum(fm_mod *m,
                                  const int32_t (*depth)[FM_MOD_DEST_COUNT],
                                  const int32_t *src, unsigned nsrc)
{
    uint32_t changed = 0;
    unsigned d;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) {
        int32_t v = fm_mod_dest_sum(depth, src, nsrc, d);
        if (v != m->sum[d]) changed |= 1u << d;
        m->sum[d] = v;
    }
    return changed;
}

/* Sets source `src` of voice `m` to `value` (Q1.30); it holds until set
 * again (or fm_mod_reset). */
static inline void fm_mod_set_source(fm_mod *m, unsigned src, int32_t value)
{
#ifdef FM_CHECK_ENGINE /* debug builds: a source that exists */
    assert(src < FM_MOD_SRC_COUNT);
#endif
    m->src[src] = value;
}

/* Sums the voice's sources, as last set, through the routes `r`. Returns
 * the destinations whose sum changed, bit 1 << dest. */
static inline uint32_t fm_mod_tick(fm_mod *m, const fm_mod_routes *r)
{
    return fm_mod_sum(m, r->depth, m->src, FM_MOD_SRC_COUNT);
}

/* Sums destination `dest` alone from the voice's sources, as last set,
 * and returns it; the other destinations keep their sums. For a note
 * start, which needs only its level: little code on a cold path. */
static inline int32_t fm_mod_update(fm_mod *m, const fm_mod_routes *r,
                                    unsigned dest)
{
    return m->sum[dest] = fm_mod_dest_sum(r->depth, m->src,
                                          FM_MOD_SRC_COUNT, dest);
}

/* Nonzero while destination `dest` is at rest: no route reaches it and
 * its sum is 0. A route turned off leaves it busy until it is summed
 * again (the voice busy word follows this). */
static inline int fm_mod_at_rest(const fm_mod *m, const fm_mod_routes *r,
                                 unsigned dest)
{
    unsigned s;
    if (m->sum[dest]) return 0;
    for (s = 0; s < FM_MOD_SRC_COUNT; s++)
        if (r->depth[s][dest]) return 0;
    return 1;
}

/* The sum at destination `dest`. */
static inline int32_t fm_mod_get(const fm_mod *m, unsigned dest)
{
#ifdef FM_CHECK_ENGINE /* debug builds: a destination that exists */
    assert(dest < FM_MOD_DEST_COUNT);
#endif
    return m->sum[dest];
}

#endif /* FM_MOD_H */
