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

/* One source routed to two destinations moves both in the same tick. */
TT_TEST(one_source_reaches_two_destinations_at_once)
{
    fm_mod_routes r;
    fm_mod m;
    int32_t src[FM_MOD_SRC_COUNT];
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH, 3 << 16);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, -(1 << 28));
    src[FM_MOD_SRC_LFO] = 1 << 29; /* +0.5 */
    TT_CHECK_EQ(fm_mod_tick(&m, &r, src),
                (1u << FM_MOD_PITCH) | (1u << FM_MOD_PW));
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH), 3 << 15);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PW), -(1 << 27));
}

/* Two sources into one destination add: each product, then the sum.
 * (fm_mod_sum takes any number of sources, so two can be tested before
 * the engine has a second one.) */
TT_TEST(two_sources_add_in_a_destination)
{
    int32_t depth[2][FM_MOD_DEST_COUNT];
    int32_t src[2];
    fm_mod m;
    memset(depth, 0, sizeof depth);
    fm_mod_reset(&m);
    depth[0][FM_MOD_PITCH] = 2 << 16;
    depth[1][FM_MOD_PITCH] = -(1 << 16);
    src[0] = 1 << 29;  /* +0.5 * 2 oct = +1 oct */
    src[1] = -(1 << 30); /* -1.0 * -1 oct = +1 oct */
    fm_mod_sum(&m, depth, src, 2);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH), 2 << 16);
}

/* Two products that each fit in int32 but whose sum does not: the sum
 * holds at the limit, on either side. */
TT_TEST(sum_of_two_products_clamps)
{
    int32_t depth[2][FM_MOD_DEST_COUNT];
    int32_t src[2];
    fm_mod m;
    memset(depth, 0, sizeof depth);
    fm_mod_reset(&m);
    depth[0][FM_MOD_PW] = depth[1][FM_MOD_PW] = 3 << 29; /* 1.5 G each */
    src[0] = src[1] = 1 << 30;
    fm_mod_sum(&m, depth, src, 2);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PW), INT32_MAX);
    src[0] = src[1] = -(1 << 30);
    fm_mod_sum(&m, depth, src, 2);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PW), INT32_MIN);
}

static void run_mod_dest_tests(void)
{
    TT_RUN(one_route_gives_source_times_depth);
    TT_RUN(sum_clamps_instead_of_wrapping);
    TT_RUN(tick_reports_the_destinations_that_changed);
    TT_RUN(one_source_reaches_two_destinations_at_once);
    TT_RUN(two_sources_add_in_a_destination);
    TT_RUN(sum_of_two_products_clamps);
}
