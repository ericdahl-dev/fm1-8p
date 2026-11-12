/* Modulation destinations module tests (engine/mod.h): each voice sums
 * its sources into destinations once a control tick. */
#include "../engine/mod.h"

/* One source routed: the destination is the source times the depth, the
 * same Q1.30 product the LFO used before the module. */
TT_TEST(one_route_gives_source_times_depth)
{
    fm_mod_routes r;
    fm_mod m;
    int32_t depth = 100 * 65536 / 1200; /* 100 cents, Q16.16 octaves */
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, depth);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, -(1 << 29)); /* LFO at -0.5 */
    fm_mod_tick(&m, &r);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1),
                (int32_t)(((int64_t)-(1 << 29) * depth) >> 30));
}

/* A sum past the destination's int32 range holds at the limit instead of
 * wrapping to the other sign. */
TT_TEST(sum_clamps_instead_of_wrapping)
{
    fm_mod_routes r;
    fm_mod m;
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, INT32_MIN);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, -(1 << 30)); /* full negative */
    fm_mod_tick(&m, &r);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PW), INT32_MAX);
}

/* A tick reports the destinations whose sum moved (bit 1 << dest), and
 * nothing when the same sources come again. */
TT_TEST(tick_reports_the_destinations_that_changed)
{
    fm_mod_routes r;
    fm_mod m;
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, 1 << 28);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, 1 << 29);
    TT_CHECK_EQ(fm_mod_tick(&m, &r), 1u << FM_MOD_PW);
    TT_CHECK_EQ(fm_mod_tick(&m, &r), 0u);
}

/* One source routed to two destinations moves both in the same tick. */
TT_TEST(one_source_reaches_two_destinations_at_once)
{
    fm_mod_routes r;
    fm_mod m;
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, 3 << 16);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PW, -(1 << 28));
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, 1 << 29); /* +0.5 */
    TT_CHECK_EQ(fm_mod_tick(&m, &r),
                (1u << FM_MOD_PITCH1) | (1u << FM_MOD_PW));
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 3 << 15);
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
    depth[0][FM_MOD_PITCH1] = 2 << 16;
    depth[1][FM_MOD_PITCH1] = -(1 << 16);
    src[0] = 1 << 29;  /* +0.5 * 2 oct = +1 oct */
    src[1] = -(1 << 30); /* -1.0 * -1 oct = +1 oct */
    fm_mod_sum(&m, depth, src, 2);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 2 << 16);
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

/* A source keeps its value from fm_mod_set_source until it is set again:
 * a later tick sums it again, and finds nothing changed. */
TT_TEST(a_set_source_holds_for_later_ticks)
{
    fm_mod_routes r;
    fm_mod m;
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, 1 << 16);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, 1 << 29);
    TT_CHECK_EQ(fm_mod_tick(&m, &r), 1u << FM_MOD_PITCH1);
    TT_CHECK_EQ(fm_mod_tick(&m, &r), 0u);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 1 << 15);
}

/* Velocity routed to level at full depth (1.0) gives the level exactly as
 * the velocity source, for any value: (v * 2^30) >> 30 == v. */
TT_TEST(velocity_at_full_depth_is_the_level_exactly)
{
    static const int32_t vel[] = { 0, 1, 12345, 1 << 29, (1 << 30) - 1,
                                   1 << 30 };
    fm_mod_routes r;
    fm_mod m;
    unsigned i;
    memset(&r, 0, sizeof r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_VEL, FM_MOD_LEVEL, 1 << 30);
    for (i = 0; i < sizeof vel / sizeof vel[0]; i++) {
        fm_mod_set_source(&m, FM_MOD_SRC_VEL, vel[i]);
        fm_mod_tick(&m, &r);
        TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_LEVEL), vel[i]);
    }
}

/* One destination taken again on its own (a note start needs only its
 * level): its sum follows the sources, the other sums stay as they were. */
TT_TEST(updating_one_destination_leaves_the_others)
{
    fm_mod_routes r;
    fm_mod m;
    fm_mod_routes_reset(&r);
    fm_mod_reset(&m);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, 1 << 16);
    fm_mod_set_route(&r, FM_MOD_SRC_VEL, FM_MOD_LEVEL, 1 << 30);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, 1 << 29);
    fm_mod_set_source(&m, FM_MOD_SRC_VEL, 1 << 28);
    TT_CHECK_EQ(fm_mod_update(&m, &r, FM_MOD_LEVEL), 1 << 28);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_LEVEL), 1 << 28);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 0); /* not taken yet */
}

/* The filter envelope routed to cutoff: the level times the amount in
 * cents, the same product the filter took before the module, for either
 * sign of amount. */
TT_TEST(filter_env_to_cutoff_is_level_times_amount_in_cents)
{
    static const int32_t amounts[] = { 4800, -2400, 9600, 1 };
    fm_mod_routes r;
    fm_mod m;
    int32_t level = 0x2A3B4C5D; /* about 0.66 */
    unsigned i;
    fm_mod_routes_reset(&r);
    fm_mod_reset(&m);
    for (i = 0; i < sizeof amounts / sizeof amounts[0]; i++) {
        fm_mod_set_route(&r, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF, amounts[i]);
        fm_mod_set_source(&m, FM_MOD_SRC_ENV1, level);
        TT_CHECK_EQ(fm_mod_update(&m, &r, FM_MOD_CUTOFF),
                    (int32_t)(((int64_t)amounts[i] * level) >> 30));
    }
}

/* A destination rests when no route reaches it and its sum is back at 0:
 * a route turned off still leaves it busy until it is summed again. */
TT_TEST(a_destination_rests_with_no_route_and_a_zero_sum)
{
    fm_mod_routes r;
    fm_mod m;
    fm_mod_routes_reset(&r);
    fm_mod_reset(&m);
    TT_CHECK(fm_mod_at_rest(&m, &r, FM_MOD_CUTOFF));
    fm_mod_set_route(&r, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF, 2400);
    TT_CHECK(!fm_mod_at_rest(&m, &r, FM_MOD_CUTOFF));
    fm_mod_set_source(&m, FM_MOD_SRC_ENV1, 1 << 29);
    fm_mod_update(&m, &r, FM_MOD_CUTOFF);
    fm_mod_set_route(&r, FM_MOD_SRC_ENV1, FM_MOD_CUTOFF, 0);
    TT_CHECK(!fm_mod_at_rest(&m, &r, FM_MOD_CUTOFF)); /* sum still 1200 */
    fm_mod_update(&m, &r, FM_MOD_CUTOFF);
    TT_CHECK(fm_mod_at_rest(&m, &r, FM_MOD_CUTOFF));
    TT_CHECK(fm_mod_at_rest(&m, &r, FM_MOD_PITCH1)); /* others untouched */
}

/* Envelope 2 (the amp envelope's share of the note's peak) routed to the
 * mix: the share times the depth, both Q1.30, for either sign of depth. */
TT_TEST(amp_env_to_mix_is_share_times_depth)
{
    static const int32_t depths[] = { 1 << 30, -(1 << 30), 0x2CCCCCCD, 1 };
    fm_mod_routes r;
    fm_mod m;
    int32_t share = 0x1A2B3C4D;
    unsigned i;
    fm_mod_routes_reset(&r);
    fm_mod_reset(&m);
    for (i = 0; i < sizeof depths / sizeof depths[0]; i++) {
        fm_mod_set_route(&r, FM_MOD_SRC_ENV2, FM_MOD_MIX, depths[i]);
        fm_mod_set_source(&m, FM_MOD_SRC_ENV2, share);
        TT_CHECK_EQ(fm_mod_update(&m, &r, FM_MOD_MIX),
                    (int32_t)(((int64_t)depths[i] * share) >> 30));
    }
}

/* A source is routed while it reaches any destination at a nonzero depth,
 * whichever destination that is; the others' routes don't count. */
TT_TEST(a_source_is_routed_while_any_of_its_depths_is_nonzero)
{
    unsigned d;
    fm_mod_routes r;
    fm_mod_routes_reset(&r);
    TT_CHECK(!fm_mod_source_routed(&r, FM_MOD_SRC_LFO));
    fm_mod_set_route(&r, FM_MOD_SRC_VEL, FM_MOD_LEVEL, 1 << 30);
    TT_CHECK(!fm_mod_source_routed(&r, FM_MOD_SRC_LFO)); /* not its route */
    for (d = 0; d < FM_MOD_DEST_COUNT; d++) {
        fm_mod_set_route(&r, FM_MOD_SRC_LFO, d, -1);
        TT_CHECK(fm_mod_source_routed(&r, FM_MOD_SRC_LFO));
        fm_mod_set_route(&r, FM_MOD_SRC_LFO, d, 0);
        TT_CHECK(!fm_mod_source_routed(&r, FM_MOD_SRC_LFO));
    }
}

/* Each oscillator has its own pitch destination: a route to one leaves
 * the other's sum at 0. */
TT_TEST(each_oscillator_pitch_is_its_own_destination)
{
    fm_mod_routes r;
    fm_mod m;
    fm_mod_routes_reset(&r);
    fm_mod_reset(&m);
    fm_mod_set_source(&m, FM_MOD_SRC_LFO, 1 << 30);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, 0x10000);
    fm_mod_tick(&m, &r);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 0x10000);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH2), 0);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH1, 0);
    fm_mod_set_route(&r, FM_MOD_SRC_LFO, FM_MOD_PITCH2, -0x8000);
    fm_mod_tick(&m, &r);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH1), 0);
    TT_CHECK_EQ(fm_mod_get(&m, FM_MOD_PITCH2), -0x8000);
}

static void run_mod_dest_tests(void)
{
    TT_RUN(one_route_gives_source_times_depth);
    TT_RUN(sum_clamps_instead_of_wrapping);
    TT_RUN(tick_reports_the_destinations_that_changed);
    TT_RUN(one_source_reaches_two_destinations_at_once);
    TT_RUN(two_sources_add_in_a_destination);
    TT_RUN(sum_of_two_products_clamps);
    TT_RUN(a_set_source_holds_for_later_ticks);
    TT_RUN(velocity_at_full_depth_is_the_level_exactly);
    TT_RUN(updating_one_destination_leaves_the_others);
    TT_RUN(filter_env_to_cutoff_is_level_times_amount_in_cents);
    TT_RUN(a_destination_rests_with_no_route_and_a_zero_sum);
    TT_RUN(amp_env_to_mix_is_share_times_depth);
    TT_RUN(a_source_is_routed_while_any_of_its_depths_is_nonzero);
    TT_RUN(each_oscillator_pitch_is_its_own_destination);
}
