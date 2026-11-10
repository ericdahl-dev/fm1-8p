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

/* Microseconds to Q16.16 samples: 0, 1 us at 44.1 kHz truncated
 * (2890.1376 -> 2890), and 10 ms at 192 kHz (1920 samples). */
TT_TEST(units_us_to_samples_q16_edges)
{
    TT_CHECK(fm_us_to_samples_q16(0, 44100u) == 0);
    TT_CHECK(fm_us_to_samples_q16(1, 44100u) == 2890);
    TT_CHECK(fm_us_to_samples_q16(10000, 192000u) == (uint64_t)1920 << 16);
}

static void run_units_tests(void)
{
    TT_RUN(units_ms_to_samples_edges);
    TT_RUN(units_permille_to_q30_edges);
    TT_RUN(units_us_to_samples_q16_edges);
}
