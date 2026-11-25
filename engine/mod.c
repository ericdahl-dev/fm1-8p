/*
 * mod.c - modulation destinations.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "mod.h"

void fm_mod_reset(fm_mod *m)
{
    unsigned i;
    for (i = 0; i < FM_MOD_SRC_COUNT; i++) m->src[i] = 0;
    for (i = 0; i < FM_MOD_DEST_COUNT; i++) m->sum[i] = 0;
}

void fm_mod_routes_reset(fm_mod_routes *r)
{
    unsigned s, d;
    for (s = 0; s < FM_MOD_SRC_COUNT; s++)
        for (d = 0; d < FM_MOD_DEST_COUNT; d++) r->depth[s][d] = 0;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) r->to[d] = 0;
    r->live = 0;
}

void fm_mod_set_route(fm_mod_routes *r, unsigned src, unsigned dest,
                      int32_t depth)
{
#ifdef FM_CHECK_ENGINE /* debug builds: a route that exists */
    assert(src < FM_MOD_SRC_COUNT && dest < FM_MOD_DEST_COUNT);
#endif
    unsigned d;
    r->depth[src][dest] = depth;
    r->to[dest] = (uint8_t)(depth ? r->to[dest] | 1u << src : r->to[dest] & ~(1u << src));
    r->live = 0;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) r->live |= r->to[d];
}
