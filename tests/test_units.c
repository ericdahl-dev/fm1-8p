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

static void run_units_tests(void)
{
    TT_RUN(units_ms_to_samples_edges);
}
