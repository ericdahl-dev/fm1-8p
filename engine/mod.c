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

void fm_mod_set_route(fm_mod_routes *r, unsigned src, unsigned dest,
                      int32_t depth)
{
#ifdef FM_CHECK_ENGINE /* debug builds: a route that exists */
    assert(src < FM_MOD_SRC_COUNT && dest < FM_MOD_DEST_COUNT);
#endif
    r->depth[src][dest] = depth;
}
