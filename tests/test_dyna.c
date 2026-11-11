/* DYNAMICS table tests (engine/dyna.h): the share of a velocity-sensitive
 * depth, Q15 (32768 = full), per DYNAMICS position and velocity, as
 * PG-8X 2.0.13 plays it (#8). */
#include "../engine/dyna.h"

/* Off (position 0) never scales: full depth at every velocity. */
TT_TEST(dyna_off_is_full_depth)
{
    TT_CHECK_EQ(fm_dyna_share_q15(0, 1), 32768u);
    TT_CHECK_EQ(fm_dyna_share_q15(0, 64), 32768u);
    TT_CHECK_EQ(fm_dyna_share_q15(0, 127), 32768u);
}

/* Position 2 is one line through 0 at v1 and 0.987 at v127:
 * share = 0.00786 v - 0.0115, clamped at 0, in Q15. */
TT_TEST(dyna_position_2_is_linear)
{
    TT_CHECK_EQ(fm_dyna_share_q15(2, 1), 0u);
    TT_CHECK_EQ(fm_dyna_share_q15(2, 97), 24606u);
    TT_CHECK_EQ(fm_dyna_share_q15(2, 98), 24864u);
    TT_CHECK_EQ(fm_dyna_share_q15(2, 127), 32333u);
}

/* Position 1 starts at 0.375 and drops at v98, from 0.749 at v97 to 0.617:
 * PG-8X's own curve, kept as measured. */
TT_TEST(dyna_position_1_drops_at_v98)
{
    TT_CHECK_EQ(fm_dyna_share_q15(1, 1), 12285u);
    TT_CHECK_EQ(fm_dyna_share_q15(1, 97), 24553u);
    TT_CHECK_EQ(fm_dyna_share_q15(1, 98), 20207u);
    TT_CHECK_EQ(fm_dyna_share_q15(1, 127), 27724u);
}

/* Position 3 is the steepest: 0.025 at v1, 0.973 at v127. */
TT_TEST(dyna_position_3_ends)
{
    TT_CHECK_EQ(fm_dyna_share_q15(3, 1), 810u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 97), 16721u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 98), 16980u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 127), 31891u);
}

/* Position 3's segment ends: the slope steps up at v64 and again at v112. */
TT_TEST(dyna_position_3_segment_ends)
{
    TT_CHECK_EQ(fm_dyna_share_q15(3, 63), 7982u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 64), 8167u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 111), 20349u);
    TT_CHECK_EQ(fm_dyna_share_q15(3, 112), 20787u);
}

/* Out of range clamps, never reads past the table: velocity 0 plays as 1,
 * velocity over 127 as 127, a position past 3 as 3, below 0 as off. */
TT_TEST(dyna_clamps_out_of_range)
{
    TT_CHECK_EQ(fm_dyna_share_q15(3, 0), fm_dyna_share_q15(3, 1));
    TT_CHECK_EQ(fm_dyna_share_q15(3, -5), fm_dyna_share_q15(3, 1));
    TT_CHECK_EQ(fm_dyna_share_q15(3, 300), fm_dyna_share_q15(3, 127));
    TT_CHECK_EQ(fm_dyna_share_q15(9, 64), fm_dyna_share_q15(3, 64));
    TT_CHECK_EQ(fm_dyna_share_q15(-1, 64), 32768u);
}

static void run_dyna_tests(void)
{
    TT_RUN(dyna_off_is_full_depth);
    TT_RUN(dyna_position_2_is_linear);
    TT_RUN(dyna_position_1_drops_at_v98);
    TT_RUN(dyna_position_3_ends);
    TT_RUN(dyna_position_3_segment_ends);
    TT_RUN(dyna_clamps_out_of_range);
}
