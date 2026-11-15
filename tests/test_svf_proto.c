/* PROTOTYPE tests for the SVF core (engine/filter.c under FM_FILTER_SVF): its response against the
 * analog two-section model with PG-8X's Q, and its bounds. Run with -DFM_FILTER_SVF only. */
#ifdef FM_FILTER_SVF
#include "../engine/filter.h"

/* the steady gain of f for a sine at hz, dB (settled: the peak over the second half of 40 periods and 8000 samples) */
static double svfp_gain_db(int32_t cut, int32_t res, double hz, uint32_t rate)
{
    static fm_filter f;
    double amp = 0.5 * 16777216.0, peak = 0;
    uint32_t i, n = (uint32_t)(rate / hz * 40.0) + 8000u;
    fm_filter_reset(&f);
    fm_filter_set_resonance(&f, res);
    fm_filter_set_cutoff(&f, cut, fm_filter_rate_cents(rate));
    for (i = 0; i < n; i++) {
        int32_t y = fm_filter_run(&f, (int32_t)lround(amp * sin(2 * AN_PI * hz * i / rate)));
        if (i > n / 2 && fabs((double)y) > peak) peak = fabs((double)y);
    }
    return 20 * log10(peak / amp);
}

/* PG-8X's two equal sections (f0, Q) at the level Q0 / Q, analog, dB */
static double svfp_model_db(double hz, double f0, double q)
{
    double w = hz / f0, re = 1 - w * w, im = w / q, h2 = 1 / (re * re + im * im);
    return 20 * log10(0.69 / q * h2);
}

TT_TEST(svf_proto_follows_pg_two_sections)
{
    static const struct { int32_t cut, res; double q; } at[] = {
        { 6769, 575, 1.585 }, { 6769, 900, 4.325 }, { 6769, 935, 6.715 },
        { 8717, 575, 1.585 }, { 8717, 900, 4.325 }, { 8717, 935, 6.715 } };
    double worst = 0;
    size_t a, k;
    for (a = 0; a < sizeof at / sizeof at[0]; a++) {
        double f0 = 440.0 * pow(2.0, (at[a].cut - 180 - 6900) / 1200.0), best = -1e9, pk = 0;
        for (k = 0; k <= 48; k++) {
            double hz = f0 * pow(2.0, (k - 24.0) / 24.0), g = svfp_gain_db(at[a].cut, at[a].res, hz, 48000);
            double m = svfp_model_db(hz, f0, at[a].q);
            if (m > -30 && fabs(g - m) > worst) worst = fabs(g - m);
            if (m > -30 && fabs(g - m) > 1.0) printf("      %.0f Hz: %+.2f against the model %+.2f\n", hz, g, m);
            if (g > best) best = g, pk = hz;
        }
        printf("    SVF proto cut %d res %d: peak %+.1f dB at %.0f Hz (f0 %.0f)\n", at[a].cut, at[a].res, best, pk, f0);
    }
    printf("    SVF proto against the two-section model: worst %.2f dB (above -30)\n", worst);
    TT_CHECK(worst < 1.0);
}

/* bounded at resonance 1000, every cutoff, 44.1 / 48 / 192 kHz, a +-15.9 square and noise: the output
 * within the hold, and no state near overflow */
TT_TEST(svf_proto_stays_bounded)
{
    static const uint32_t rates[] = { 44100, 48000, 192000 };
    static fm_filter f;
    int32_t worst = 0, out = 0;
    uint32_t seed = 1, r, c, i, j;
    for (r = 0; r < 3; r++)
        for (c = 0; c <= FM_FILTER_OPEN_CENTS; c += 300) {
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, 1000);
            fm_filter_set_cutoff(&f, (int32_t)c, fm_filter_rate_cents(rates[r]));
            for (i = 0; i < 20000; i++) {
                int32_t x;
                seed = seed * 1664525u + 1013904223u;
                x = i < 10000 ? ((i / (40u + c / 100u)) & 1u ? 266000000 : -266000000) : (int32_t)(seed >> 4) - (1 << 27);
                x = fm_filter_run(&f, x);
                out = x < 0 && -x > out ? -x : x > out ? x : out;
                for (j = 0; j < 4; j++) {
                    int32_t v = f.s[j] < 0 ? -f.s[j] : f.s[j];
                    worst = v > worst ? v : worst;
                }
            }
        }
    printf("    SVF proto at resonance 1000: largest output %.2f, largest state %.2f (Q9.23)\n", out / 16777216.0,
           worst / 8388608.0);
    TT_CHECK(out <= FM_FILTER_HOLD);
    TT_CHECK(worst < (1 << 30));
}

static void run_svf_proto_tests(void)
{
    TT_RUN(svf_proto_follows_pg_two_sections);
    TT_RUN(svf_proto_stays_bounded);
}
#else
static void run_svf_proto_tests(void) {}
#endif
