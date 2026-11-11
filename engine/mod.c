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
