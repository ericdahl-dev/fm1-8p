/*
 * Filter tests: frequency response through the filter module API
 * (engine/filter.h), and the voice wiring through fm_engine_* calls.
 */
#include "../engine/filter.h"

#define FRATE 44100u

/* Q8.24 sine sample of amplitude `amp` (1.0 = full) at `hz`. */
static int32_t sine_q24(double amp, double hz, size_t i)
{
    return (int32_t)lround(amp * 16777216.0 *
                           sin(2.0 * AN_PI * hz * (double)i / FRATE));
}

/*
 * Gain in dB of the filter `f` (already set up) for a sine at `hz`:
 * RMS out / RMS in, measured over 0.5 s after 0.5 s of settling.
 */
static double filter_gain_db(fm_filter *f, double hz, double amp)
{
    double in2 = 0, out2 = 0;
    size_t i;
    for (i = 0; i < FRATE; i++) {
        int32_t x = sine_q24(amp, hz, i);
        int32_t y = fm_filter_process(f, x);
        if (i >= FRATE / 2) {
            in2 += (double)x * x;
            out2 += (double)y * y;
        }
    }
    return 10.0 * log10(out2 / in2);
}

/* A lowpass with no resonance at `cutoff_cents`, at 44.1 kHz. */
static void lowpass_at(fm_filter *f, int32_t cutoff_cents, int32_t res)
{
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    fm_filter_reset(f);
    fm_filter_set_resonance(f, res);
    fm_filter_set_cutoff(f, cutoff_cents, rate_cents);
}

/*
 * Cutoff 440 Hz (6900 cents above MIDI note 0), no resonance. Four equal
 * real poles meet at the cutoff, so the cutoff point itself is -12 dB and
 * the slope steepens toward 24 dB/octave: the theory for an ideal 4-pole
 * gives 15.9 dB for the first octave above cutoff, 21.2 dB for the second
 * and 23.3 dB for the third. Tolerance: +-2 dB on each.
 */
TT_TEST(lowpass_passes_low_tones_and_rolls_off_24_db_per_octave)
{
    static fm_filter f;
    double below, at, oct1, oct2, oct3;
    lowpass_at(&f, 6900, 0);
    below = filter_gain_db(&f, 55.0, 0.5); /* 3 octaves below */
    at = filter_gain_db(&f, 440.0, 0.5);
    oct1 = filter_gain_db(&f, 880.0, 0.5);
    oct2 = filter_gain_db(&f, 1760.0, 0.5);
    oct3 = filter_gain_db(&f, 3520.0, 0.5);
    printf("    lowpass 440 Hz: 55 Hz %.2f dB, 440 %.2f, 880 %.2f, "
           "1760 %.2f, 3520 %.2f\n", below, at, oct1, oct2, oct3);
    TT_CHECK(below > -0.5);
    TT_CHECK_NEAR(at, -12.0, 2.0);
    TT_CHECK_NEAR(at - oct1, 15.9, 2.0);
    TT_CHECK_NEAR(oct1 - oct2, 21.2, 2.0);
    TT_CHECK_NEAR(oct2 - oct3, 23.3, 2.0);
}

/*
 * Resonance feeds the last stage back to the input, which lifts the
 * response at the cutoff (theory for the analog ladder: 0.25 / (1 - k/4)
 * at feedback k; k = 4 at resonance 1000) and costs some bass. Midway,
 * where the cutoff point is near the passband level, the octave above
 * cutoff is about 24 dB below the cutoff point (theory 23.6 dB at 600).
 */
TT_TEST(resonance_raises_gain_near_cutoff)
{
    static fm_filter f;
    double at0, at500, at900, at600, oct600;
    lowpass_at(&f, 6900, 0);
    at0 = filter_gain_db(&f, 440.0, 0.25);
    lowpass_at(&f, 6900, 500);
    at500 = filter_gain_db(&f, 440.0, 0.25);
    lowpass_at(&f, 6900, 900);
    at900 = filter_gain_db(&f, 440.0, 0.25);
    lowpass_at(&f, 6900, 600);
    at600 = filter_gain_db(&f, 440.0, 0.25);
    oct600 = filter_gain_db(&f, 880.0, 0.25);
    printf("    gain at cutoff: res 0 %.2f dB, 500 %.2f, 900 %.2f; "
           "res 600: cutoff %.2f, octave above %.2f\n",
           at0, at500, at900, at600, oct600);
    TT_CHECK(at500 > at0 + 5.0);  /* theory +6.0 dB */
    TT_CHECK(at900 > at500 + 12.0); /* theory +14.0 dB */
    TT_CHECK_NEAR(at600 - oct600, 24.0, 3.0);
}

static void run_filter_tests(void)
{
    TT_RUN(lowpass_passes_low_tones_and_rolls_off_24_db_per_octave);
    TT_RUN(resonance_raises_gain_near_cutoff);
}
