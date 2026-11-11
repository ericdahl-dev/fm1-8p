/* Units module tests (engine/units.h): every conversion truncates toward
 * zero, and the largest setting fits. */
#include "../engine/units.h"

/* ms to samples: 0, .5 truncated (5 ms at 44.1 kHz is 220.5), and the
 * longest time (10 s) at the highest rate (192 kHz). */
TT_TEST(units_ms_to_samples_edges)
{
    TT_CHECK_EQ(fm_ms_to_samples(0, 44100u), 0u);
    TT_CHECK_EQ(fm_ms_to_samples(5, 44100u), 220u);
    TT_CHECK_EQ(fm_ms_to_samples(32000, 192000u), 6144000u);
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

/* A rate to a Q0.32 phase step: rate 0 gives 0 (no divide by zero), 1 Hz
 * at 44.1 kHz truncated (2^32 / 44100 = 97391.55), the same as 1000 mHz,
 * per 16-sample tick (shift 4: 1558264.77), and half the sample rate is
 * exactly half a cycle. */
TT_TEST(units_rate_to_phase_inc_edges)
{
    TT_CHECK(fm_rate_to_phase_inc(100, 100, 0, 0) == 0);
    TT_CHECK(fm_rate_to_phase_inc(1, 1, 44100u, 0) == 97391);
    TT_CHECK(fm_rate_to_phase_inc(1000, 1000, 44100u, 0) == 97391);
    TT_CHECK(fm_rate_to_phase_inc(100, 100, 44100u, 4) == 1558264);
}

/* Too fast a rate is capped at just under half a cycle per step, the same
 * for every caller: half the sample rate, the sample rate itself (a chorus
 * rate of 44.1 kHz in mHz: wrapped to 0 in 32 bits before) and above. */
TT_TEST(units_rate_to_phase_inc_caps_at_half_a_cycle)
{
    TT_CHECK(fm_rate_to_phase_inc(22050, 1, 44100u, 0) == 0x7FFFFFFFu);
    TT_CHECK(fm_rate_to_phase_inc(44100000u, 1000u, 44100u, 0) == 0x7FFFFFFFu);
    TT_CHECK(fm_rate_to_phase_inc(100000000u, 1000u, 44100u, 0) == 0x7FFFFFFFu);
}

/* Past the 64-bit shortcut (shift >= 32, or value >= 2^(32 - shift)) the
 * step is still exact, or the cap: 1 Hz per 2^32-sample step, 2^28 Hz per
 * 32-sample step at 44.1 kHz, and 2^28 / 10^6 Hz per 32-sample step at
 * 48 kHz, 2^65 / 4.8e10 = 768614336.4. */
TT_TEST(units_rate_to_phase_inc_wide_inputs)
{
    TT_CHECK(fm_rate_to_phase_inc(1, 1, 44100u, 32) == FM_PHASE_INC_MAX);
    TT_CHECK(fm_rate_to_phase_inc(1u << 28, 1, 44100u, 5) == FM_PHASE_INC_MAX);
    TT_CHECK(fm_rate_to_phase_inc(1u << 28, 1000000u, 48000u, 5) == 768614336u);
}

/* ms to samples never wraps: a negative time is 0, and past 32 bits it
 * saturates (at 192 kHz, 22369621 ms is 4294967232 samples; one more ms
 * would be 4294967424). */
TT_TEST(units_ms_to_samples_clamps)
{
    TT_CHECK_EQ(fm_ms_to_samples(-1, 44100u), 0u);
    TT_CHECK_EQ(fm_ms_to_samples(22369621, 192000u), 4294967232u);
    TT_CHECK_EQ(fm_ms_to_samples(22369622, 192000u), 0xFFFFFFFFu);
    TT_CHECK_EQ(fm_ms_to_samples(0x7FFFFFFF, 192000u), 0xFFFFFFFFu);
}

/* Per mille past +-1000 clamps to +-1.0 instead of wrapping (above about
 * 2000 the Q1.30 result would not fit 32 bits). */
TT_TEST(units_permille_to_q30_clamps)
{
    TT_CHECK_EQ(fm_permille_to_q30(1001), 1 << 30);
    TT_CHECK_EQ(fm_permille_to_q30(-1001), -(1 << 30));
    TT_CHECK_EQ(fm_permille_to_q30(0x7FFFFFFF), 1 << 30);
    TT_CHECK_EQ(fm_permille_to_q30(-0x7FFFFFFF - 1), -(1 << 30));
}

/* The real operating points: the LFO's top rate (FM_LFO_MAX_CENTIHZ per
 * FM_LFO_TICK_SHIFT tick) at 44.1 kHz and at the lowest engine rate, 8 kHz
 * (still under the cap), and the chorus's fastest mode (6 Hz) at 8 kHz:
 * 3000 << 37 / 4410000 = 93495886.7, / 800000 = 515396075.5, and
 * 6000 << 32 / 8000000 = 3221225.5. */
TT_TEST(units_rate_to_phase_inc_at_the_lfo_and_chorus_limits)
{
    TT_CHECK(fm_rate_to_phase_inc(FM_LFO_MAX_CENTIHZ, 100u, 44100u,
                                  FM_LFO_TICK_SHIFT) == 93495886u);
    TT_CHECK(fm_rate_to_phase_inc(FM_LFO_MAX_CENTIHZ, 100u, 8000u,
                                  FM_LFO_TICK_SHIFT) == 515396075u);
    TT_CHECK(fm_rate_to_phase_inc(6000u, 1000u, 8000u, 0) == 3221225u);
}

static void run_units_tests(void)
{
    TT_RUN(units_ms_to_samples_edges);
    TT_RUN(units_ms_to_samples_clamps);
    TT_RUN(units_permille_to_q30_edges);
    TT_RUN(units_permille_to_q30_clamps);
    TT_RUN(units_us_to_samples_q16_edges);
    TT_RUN(units_rate_to_phase_inc_edges);
    TT_RUN(units_rate_to_phase_inc_caps_at_half_a_cycle);
    TT_RUN(units_rate_to_phase_inc_wide_inputs);
    TT_RUN(units_rate_to_phase_inc_at_the_lfo_and_chorus_limits);
}
