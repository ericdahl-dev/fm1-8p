/*
 * The engine's filter held to the filter as merged in #25 (filter_oracle.c, test-only): whatever
 * arithmetic the engine uses, its output must stay within the stated error of that reference.
 */
#include "filter_oracle.c"

#define ORACLE_RATE 44100u

/* Q8.24 input: two naive saws (130.8 and 196 Hz), 0.4 each: a chord with plenty of harmonics. */
static int32_t oracle_input(size_t i)
{
    double a = fmod(130.81 * (double)i / ORACLE_RATE, 1.0), b = fmod(196.0 * (double)i / ORACLE_RATE, 1.0);
    return (int32_t)lround(0.4 * 16777216.0 * ((2.0 * a - 1.0) + (2.0 * b - 1.0)));
}

/* The full patch's cutoff: up from `base` by `amount` cents over 5 ms (the attack), then down to half
 * of it over 300 ms (the decay to sustain 500), set every 16 samples as the engine does. */
static int32_t oracle_cutoff_at(size_t i, int32_t base, int32_t amount)
{
    double t = (double)(i - i % 16) / ORACLE_RATE, env;
    env = t < 0.005 ? t / 0.005 : t < 0.305 ? 1.0 - 0.5 * (t - 0.005) / 0.3 : 0.5;
    return base + (int32_t)lround(amount * env);
}

/* SNR in dB of the engine's filter against the oracle, both run on oracle_input for 1 s with the
 * given settings; 999 when they match exactly. */
static double oracle_snr_db(int32_t base, int32_t amount, int32_t res, uint8_t hp)
{
    static fm_filter f;
    static oracle_filter o;
    int32_t rate = fm_filter_rate_cents(ORACLE_RATE);
    double sig = 0, err = 0;
    size_t i;
    fm_filter_reset(&f);
    oracle_filter_reset(&o);
    fm_filter_set_resonance(&f, res);
    oracle_filter_set_resonance(&o, res);
    fm_filter_set_highpass(&f, hp, rate);
    oracle_filter_set_highpass(&o, hp, rate);
    for (i = 0; i < ORACLE_RATE; i++) {
        int32_t x = oracle_input(i), y, r;
        if (i % 16 == 0) {
            fm_filter_set_cutoff(&f, oracle_cutoff_at(i, base, amount), rate);
            oracle_filter_set_cutoff(&o, oracle_cutoff_at(i, base, amount), rate);
        }
        y = fm_filter_run(&f, x);
        r = oracle_filter_process(&o, x);
        sig += (double)r * r;
        err += ((double)y - r) * ((double)y - r);
    }
    return err == 0 ? 999.0 : 10.0 * log10(sig / err);
}

/* The full patch (CPU test step F): cutoff 10500 swept +3600 cents, resonance 700, highpass step 1. */
TT_TEST(filter_matches_the_reference_on_the_full_patch)
{
    double snr = oracle_snr_db(10500, 3600, 700, 1);
    printf("    full patch: %.1f dB SNR against the reference\n", snr);
    TT_CHECK(snr >= 90.0);
}

/* The default patch: the filter fully open, no resonance, highpass off. */
TT_TEST(filter_matches_the_reference_on_the_default_patch)
{
    double snr = oracle_snr_db(ORACLE_OPEN_CENTS, 0, 0, 0);
    printf("    default patch: %.1f dB SNR against the reference\n", snr);
    TT_CHECK(snr >= 90.0);
}

/* Frequency of a ringing output from its upward zero crossings in seconds 3..4; 0 if it has died. */
static double oracle_ring_hz(const int32_t *y, size_t n)
{
    double first = -1, last = -1;
    int crossings = 0;
    size_t i;
    for (i = 3 * (size_t)ORACLE_RATE; i < n; i++)
        if (y[i - 1] < 0 && y[i] >= 0) {
            double t = (double)i - 1 + (double)-y[i - 1] / ((double)y[i] - y[i - 1]);
            if (first < 0) first = t;
            last = t;
            crossings++;
        }
    return crossings > 1 ? ORACLE_RATE * (crossings - 1) / (last - first) : 0;
}

/* At resonance 1000 the lowpass rings on by itself after one impulse; its pitch stays within 1 cent of
 * the reference's at every cutoff. */
TT_TEST(self_oscillation_pitch_matches_the_reference)
{
    static const int32_t cutoffs[] = { 2400, 4800, 6900, 9000, 10500, 12000, 13000 };
    static int32_t y[4 * ORACLE_RATE], r[4 * ORACLE_RATE];
    static fm_filter f;
    static oracle_filter o;
    int32_t rate = fm_filter_rate_cents(ORACLE_RATE);
    size_t c, i;
    for (c = 0; c < sizeof cutoffs / sizeof cutoffs[0]; c++) {
        double hy, hr, cents;
        fm_filter_reset(&f);
        oracle_filter_reset(&o);
        fm_filter_set_resonance(&f, 1000);
        oracle_filter_set_resonance(&o, 1000);
        fm_filter_set_cutoff(&f, cutoffs[c], rate);
        oracle_filter_set_cutoff(&o, cutoffs[c], rate);
        for (i = 0; i < 4 * (size_t)ORACLE_RATE; i++) {
            y[i] = fm_filter_run(&f, i == 0 ? 1 << 24 : 0);
            r[i] = oracle_filter_process(&o, i == 0 ? 1 << 24 : 0);
        }
        hy = oracle_ring_hz(y, 4 * (size_t)ORACLE_RATE);
        hr = oracle_ring_hz(r, 4 * (size_t)ORACLE_RATE);
        cents = hr > 0 && hy > 0 ? 1200.0 * log2(hy / hr) : 9999.0;
        printf("    cutoff %5d: rings at %8.2f Hz, reference %8.2f Hz (%+.3f cent)\n", (int)cutoffs[c], hy, hr,
               cents);
        TT_CHECK(fabs(cents) <= 1.0);
    }
}

/* Full resonance on an 8-voice chord of loud saws, the filter swept by its envelope and the
 * highpass on: every voice's filter state stays within the soft clip's bound (no overflow, no
 * runaway) the whole time, and the chord goes silent once released. */
TT_TEST(full_resonance_8_voice_chord_stays_bounded)
{
    static const uint8_t chord[8] = { 36, 43, 48, 52, 55, 60, 64, 67 };
    static fm_engine e;
    static int16_t out[2 * 256];
    size_t b, i, v;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&e, ORACLE_RATE, 8), FM_OK);
    fm_engine_set_param(&e, FM_PARAM_FILTER_CUTOFF_CENTS, 6900);
    fm_engine_set_param(&e, FM_PARAM_FILTER_RESONANCE, 1000);
    fm_engine_set_param(&e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 3600);
    fm_engine_set_param(&e, FM_PARAM_HIGHPASS_STEP, 1);
    fm_engine_set_param(&e, FM_PARAM_AMP_RELEASE_MS, 0);
    for (i = 0; i < 8; i++)
        fm_engine_note_on(&e, chord[i], 127);
    for (b = 0; b < 2 * ORACLE_RATE / 256; b++) {               /* 2 s held */
        fm_engine_render(&e, out, 256);
        for (v = 0; v < 8; v++)
            for (i = 0; i < 4; i++)
                TT_CHECK(e.voices[v].filter.s[i] <= FILTER_BOUND && e.voices[v].filter.s[i] >= -FILTER_BOUND);
    }
    for (i = 0; i < 8; i++)
        fm_engine_note_off(&e, chord[i]);
    for (b = 0; b < ORACLE_RATE / 256; b++) {                   /* 1 s released */
        fm_engine_render(&e, out, 256);
        if (b >= ORACLE_RATE / 512)                             /* the last half second */
            for (i = 0; i < 2 * 256; i++)
                loud_after |= out[i] != 0;
    }
    TT_CHECK(!loud_after);
}

/* Up to +-31.9 (far beyond any oscillator, which stays near +-1.0) the filter matches the reference
 * exactly at full resonance with the strongest highpass: a square wave at that level through a
 * cutoff swept over the whole range. */
TT_TEST(loud_input_matches_the_reference_exactly)
{
    static fm_filter f;
    static oracle_filter o;
    int32_t rate = fm_filter_rate_cents(ORACLE_RATE);
    size_t i;
    int same = 1;
    fm_filter_reset(&f);
    oracle_filter_reset(&o);
    fm_filter_set_resonance(&f, 1000);
    oracle_filter_set_resonance(&o, 1000);
    fm_filter_set_highpass(&f, 3, rate);
    oracle_filter_set_highpass(&o, 3, rate);
    for (i = 0; i < ORACLE_RATE; i++) {
        int32_t x = (i / 50) % 2 ? (int32_t)(31.9 * 16777216.0) : -(int32_t)(31.9 * 16777216.0);
        if (i % 16 == 0) {
            int32_t c = (int32_t)(i * ORACLE_OPEN_CENTS / ORACLE_RATE);
            fm_filter_set_cutoff(&f, c, rate);
            oracle_filter_set_cutoff(&o, c, rate);
        }
        same &= fm_filter_run(&f, x) == oracle_filter_process(&o, x);
    }
    TT_CHECK(same);
}

/* Q8.24 has headroom to +-128.0, and the filter must match the reference bit for bit over all of
 * it: a loud DC offset (40.0) with a 0.5 sine through the strongest highpass, which takes the
 * offset out, at every lowpass setting from open down to a resonant 2400 cents. */
TT_TEST(input_beyond_32_matches_the_reference_exactly)
{
    static const int32_t cutoffs[] = { ORACLE_OPEN_CENTS, 14000, 9000, 2400 };
    static const int32_t dcs[] = { 40, 100, -127 };
    static fm_filter f;
    static oracle_filter o;
    int32_t rate = fm_filter_rate_cents(ORACLE_RATE);
    size_t c, d, i, differ = 0;
    for (d = 0; d < sizeof dcs / sizeof dcs[0]; d++)
        for (c = 0; c < sizeof cutoffs / sizeof cutoffs[0]; c++) {
            fm_filter_reset(&f);
            oracle_filter_reset(&o);
            fm_filter_set_resonance(&f, c == 3 ? 900 : 0);
            oracle_filter_set_resonance(&o, c == 3 ? 900 : 0);
            fm_filter_set_highpass(&f, 3, rate);
            oracle_filter_set_highpass(&o, 3, rate);
            fm_filter_set_cutoff(&f, cutoffs[c], rate);
            oracle_filter_set_cutoff(&o, cutoffs[c], rate);
            for (i = 0; i < ORACLE_RATE; i++) {
                int32_t x = (int32_t)lround(16777216.0 * (dcs[d] + 0.5 * sin(6.283185307179586 * 440.0 * (double)i / ORACLE_RATE)));
                differ += fm_filter_run(&f, x) != oracle_filter_process(&o, x);
            }
        }
    printf("    input beyond 32: %lu of %lu samples differ from the reference\n", (unsigned long)differ,
           (unsigned long)(12 * ORACLE_RATE));
    TT_CHECK_EQ(differ, 0);
}

/* Any int32 input, even the extremes, gives a bounded output: no overflow, whatever the setting. */
TT_TEST(extreme_input_stays_bounded)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(ORACLE_RATE);
    size_t i, c;
    for (c = 0; c <= (size_t)ORACLE_OPEN_CENTS; c += 1200) {
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, 1000);
        fm_filter_set_highpass(&f, 3, rate);
        fm_filter_set_cutoff(&f, (int32_t)c, rate);
        for (i = 0; i < 4096; i++) {
            int32_t y = fm_filter_run(&f, (i / 7) % 2 ? INT32_MAX : INT32_MIN);
            TT_CHECK(y <= FILTER_BOUND && y >= -FILTER_BOUND);
        }
    }
}

static void run_filter_oracle_tests(void)
{
    TT_RUN(loud_input_matches_the_reference_exactly);
    TT_RUN(input_beyond_32_matches_the_reference_exactly);
    TT_RUN(extreme_input_stays_bounded);
    TT_RUN(full_resonance_8_voice_chord_stays_bounded);
    TT_RUN(self_oscillation_pitch_matches_the_reference);
    TT_RUN(filter_matches_the_reference_on_the_default_patch);
    TT_RUN(filter_matches_the_reference_on_the_full_patch);
}
