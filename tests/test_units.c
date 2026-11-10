/* Units module tests (engine/units.h): every conversion truncates toward
 * zero, and the largest setting fits. */
#include "../engine/units.h"

/* ms to samples: 0, .5 truncated (5 ms at 44.1 kHz is 220.5), and the
 * longest time (10 s) at the highest rate (192 kHz). */
TT_TEST(units_ms_to_samples_edges)
{
    TT_CHECK_EQ(fm_ms_to_samples(0, 44100u), 0u);
    TT_CHECK_EQ(fm_ms_to_samples(5, 44100u), 220u);
    TT_CHECK_EQ(fm_ms_to_samples(10000, 192000u), 1920000u);
}

/* Per mille to Q1.30: 0, the ends +-1000, and +-1 truncated toward zero
 * (2^30 / 1000 = 1073741.824). */
TT_TEST(units_permille_to_q30_edges)
{
    TT_CHECK_EQ(fm_permille_to_q30(0), 0);
    TT_CHECK_EQ(fm_permille_to_q30(1000), 1 << 30);
    TT_CHECK_EQ(fm_permille_to_q30(-1000), -(1 << 30));
    TT_CHECK_EQ(fm_permille_to_q30(1), 1073741);
    TT_CHECK_EQ(fm_permille_to_q30(-1), -1073741);
}

static void run_units_tests(void)
{
    TT_RUN(units_ms_to_samples_edges);
    TT_RUN(units_permille_to_q30_edges);
}
