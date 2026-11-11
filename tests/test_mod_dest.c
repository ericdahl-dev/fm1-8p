/* Modulation destinations module tests (engine/mod.h): each voice sums
 * its sources into destinations once a control tick. */
#include "../engine/mod.h"

/* One source routed: the destination is the source times the depth, the
 * same Q1.30 product the LFO used before the module. */
TT_TEST(one_route_gives_source_times_depth)
{
    fm_mod_routes r;
    fm_mod m;
    int32_t src[FM_MOD_SRC_COUNT];
    int32_t depth = 100 * 65536 / 1200; /* 100 cents, Q16.16 octaves */
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH, depth);
    src[FM_MOD_SRC_LFO] = -(1 << 29); /* LFO at -0.5 */
    fm_mod_tick(&m, &r, src);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH),
                (int32_t)(((int64_t)src[FM_MOD_SRC_LFO] * depth) >> 30));
}

/* A sum past the destination's int32 range holds at the limit instead of
 * wrapping to the other sign. */
TT_TEST(sum_clamps_instead_of_wrapping)
{
    fm_mod_routes r;
    fm_mod m;
    int32_t src[FM_MOD_SRC_COUNT];
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, INT32_MIN);
    src[FM_MOD_SRC_LFO] = -(1 << 30); /* full negative */
    fm_mod_tick(&m, &r, src);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PW), INT32_MAX);
}

/* A tick reports the destinations whose sum moved (bit 1 << dest), and
 * nothing when the same sources come again. */
TT_TEST(tick_reports_the_destinations_that_changed)
{
    fm_mod_routes r;
    fm_mod m;
    int32_t src[FM_MOD_SRC_COUNT];
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, 1 << 28);
    src[FM_MOD_SRC_LFO] = 1 << 29;
    TT_CHECK_EQ(fm_mod_tick(&m, &r, src), 1u << FM_MOD_PW);
    TT_CHECK_EQ(fm_mod_tick(&m, &r, src), 0u);
}

static void run_mod_dest_tests(void)
{
    TT_RUN(one_route_gives_source_times_depth);
    TT_RUN(sum_clamps_instead_of_wrapping);
    TT_RUN(tick_reports_the_destinations_that_changed);
}
