/*
 * mod.c - modulation destinations.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "mod.h"

void fm_mod_reset(fm_mod *m)
{
    unsigned d;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) m->sum[d] = 0;
}

void fm_mod_set_route(fm_mod_routes *r, unsigned src, unsigned dest,
                      int32_t depth)
{
    r->depth[src][dest] = depth;
}

uint32_t fm_mod_tick(fm_mod *m, const fm_mod_routes *r, const int32_t *src)
{
    uint32_t changed = 0;
    unsigned d;
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) {
        /* Q1.30 * depth -> depth's unit */
        int64_t s = ((int64_t)src[FM_MOD_SRC_LFO] *
                     r->depth[FM_MOD_SRC_LFO][d]) >> 30;
        if (s > INT32_MAX) s = INT32_MAX;
        if (s < INT32_MIN) s = INT32_MIN;
        if ((int32_t)s != m->sum[d]) changed |= 1u << d;
        m->sum[d] = (int32_t)s;
    }
    return changed;
}

int32_t fm_mod_get(const fm_mod *m, unsigned dest)
{
    return m->sum[dest];
}
