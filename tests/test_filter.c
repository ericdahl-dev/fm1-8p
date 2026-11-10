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

/*
 * Maximum resonance self-oscillates; the soft clip in the feedback path
 * must hold every output within +-4.0 (Q8.24) at any cutoff, for any
 * input, for as long as it runs: 10 s per case, with a loud saw, loud
 * noise and a single huge impulse followed by silence.
 */
#define FILTER_BOUND (4 << 24)

TT_TEST(max_resonance_stays_bounded_over_a_long_render)
{
    static const int32_t cutoffs[] = { 0, 2400, 6900, 11000, 13600,
                                       14399, FM_FILTER_OPEN_CENTS };
    static fm_filter f;
    size_t c, kind, i;
    uint32_t noise = 12345;
    for (c = 0; c < sizeof cutoffs / sizeof cutoffs[0]; c++) {
        for (kind = 0; kind < 3; kind++) {
            int32_t peak = 0, late_peak = 0;
            lowpass_at(&f, cutoffs[c], 1000);
            for (i = 0; i < 10 * (size_t)FRATE; i++) {
                int32_t x, y;
                if (kind == 0) { /* saw at +-1.0, 220 Hz */
                    x = (int32_t)((i * 220 * 2 % FRATE) * (2u << 24) / FRATE) -
                        (1 << 24);
                } else if (kind == 1) { /* noise at +-2.0 */
                    noise = noise * 1664525u + 1013904223u;
                    x = (int32_t)(noise >> 6) - (1 << 25);
                } else { /* impulse of 100.0, then silence */
                    x = i == 0 ? 100 << 24 : 0;
                }
                y = fm_filter_process(&f, x);
                if (y < 0) y = -y;
                if (y > peak) peak = y;
                if (i >= 9 * (size_t)FRATE && y > late_peak) late_peak = y;
            }
            if (peak > FILTER_BOUND || late_peak > FILTER_BOUND)
                printf("    cutoff %d, input %d: peak %.3f, last second "
                       "%.3f\n", (int)cutoffs[c], (int)kind,
                       peak / 16777216.0, late_peak / 16777216.0);
            TT_CHECK(peak <= FILTER_BOUND);
        }
    }
}

/* ---- the filter in the voice, through fm_engine_* ---- */

#define FRAMES_HALF_S (FRATE / 2)

static fm_engine feng;
static int16_t fbuf_a[FRATE * 2], fbuf_b[FRATE * 2];

/* Plays `note` for half a second with the given filter settings. */
static void play_filtered(int16_t *out, uint8_t note, int32_t cutoff,
                          int32_t res, int32_t track)
{
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_RESONANCE, res);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, track);
    fm_engine_note_on(&feng, note, 100);
    fm_engine_render(&feng, out, FRAMES_HALF_S);
}

/* RMS of one channel over frames [from, to). */
static double rms(const int16_t *stereo, size_t from, size_t to)
{
    double sum = 0;
    size_t i;
    for (i = from; i < to; i++) sum += (double)stereo[2 * i] * stereo[2 * i];
    return sqrt(sum / (double)(to - from));
}

/*
 * Key tracking is per mille of the note's distance from middle C (note
 * 60): at 1000 the cutoff follows the keyboard one to one. So note 72
 * with tracking T must sound exactly like note 72 with the cutoff raised
 * by T * 1200 / 1000 cents and no tracking, and full tracking must be
 * brighter than none.
 */
TT_TEST(key_tracking_moves_cutoff_with_note_pitch)
{
    static const int32_t tracks[] = { 0, 250, 500, 1000 };
    size_t t;
    for (t = 0; t < sizeof tracks / sizeof tracks[0]; t++) {
        play_filtered(fbuf_a, 72, 5000, 300, tracks[t]);
        play_filtered(fbuf_b, 72, 5000 + tracks[t] * 1200 / 1000, 300, 0);
        TT_CHECK(memcmp(fbuf_a, fbuf_b, FRAMES_HALF_S * 4) == 0);
    }
    /* below middle C tracking lowers the cutoff */
    play_filtered(fbuf_a, 48, 5000, 300, 500);
    play_filtered(fbuf_b, 48, 5000 - 600, 300, 0);
    TT_CHECK(memcmp(fbuf_a, fbuf_b, FRAMES_HALF_S * 4) == 0);
    /* and it is audible: same note, full tracking vs none */
    play_filtered(fbuf_a, 72, 5000, 300, 1000);
    play_filtered(fbuf_b, 72, 5000, 300, 0);
    printf("    note 72, cutoff 5000: RMS tracked %.0f, untracked %.0f\n",
           rms(fbuf_a, 4410, FRAMES_HALF_S), rms(fbuf_b, 4410, FRAMES_HALF_S));
    TT_CHECK(rms(fbuf_a, 4410, FRAMES_HALF_S) >
             2.0 * rms(fbuf_b, 4410, FRAMES_HALF_S));
}

/* Brightness of one channel over frames [from, to): RMS of the sample
 * differences over RMS of the signal (rises with high harmonics). */
static double brightness(const int16_t *stereo, size_t from, size_t to)
{
    double d2 = 0, x2 = 0;
    size_t i;
    for (i = from + 1; i < to; i++) {
        double d = (double)stereo[2 * i] - stereo[2 * (i - 1)];
        d2 += d * d;
        x2 += (double)stereo[2 * i] * stereo[2 * i];
    }
    return sqrt(d2 / x2);
}

/* Plays note 36 for one second with a decaying filter envelope (0 ms
 * attack, 400 ms decay to zero sustain) of `amount` cents. */
static void play_filter_env(int16_t *out, int32_t cutoff, int32_t amount)
{
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_DECAY_MS, 400);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_SUSTAIN, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, amount);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_render(&feng, out, FRATE);
}

/*
 * A positive amount opens the filter at the note start and sweeps the
 * cutoff back down as the envelope decays (bright, then dark); a
 * negative amount closes it and sweeps up (dark, then bright). With no
 * amount the tone stays the same.
 */
TT_TEST(filter_envelope_sweeps_cutoff_up_or_down)
{
    double early, late;
    play_filter_env(fbuf_a, 4800, 4800); /* 130 Hz + 4 octaves */
    early = brightness(fbuf_a, 441, 2205);   /* 10..50 ms */
    late = brightness(fbuf_a, 39690, 44100); /* 900..1000 ms */
    printf("    env +4800: brightness early %.4f, late %.4f\n", early, late);
    TT_CHECK(early > 2.0 * late);

    play_filter_env(fbuf_a, 9600, -4800); /* 2.1 kHz - 4 octaves */
    early = brightness(fbuf_a, 441, 2205);
    late = brightness(fbuf_a, 39690, 44100);
    printf("    env -4800: brightness early %.4f, late %.4f\n", early, late);
    TT_CHECK(late > 2.0 * early);

    play_filter_env(fbuf_a, 7200, 0);
    early = brightness(fbuf_a, 441, 2205);
    late = brightness(fbuf_a, 39690, 44100);
    TT_CHECK_NEAR(early / late, 1.0, 0.05);
}

/*
 * The highpass has four positions: 0 is off, 1..3 cut more and more
 * low end. Measured on its own (lowpass open) with low sines, and in the
 * voice on a low note, where it thins out the level.
 */
TT_TEST(each_highpass_step_cuts_low_frequencies_more)
{
    static fm_filter f;
    double g55[4], g110[4], level[4];
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    uint8_t step;
    size_t i;
    for (step = 0; step < 4; step++) {
        fm_filter_reset(&f);
        fm_filter_set_highpass(&f, step, rate_cents);
        g55[step] = filter_gain_db(&f, 55.0, 0.5);
        g110[step] = filter_gain_db(&f, 110.0, 0.5);

        fm_engine_init(&feng, FRATE, 1);
        fm_engine_set_param(&feng, FM_PARAM_HIGHPASS_STEP, step);
        fm_engine_note_on(&feng, 33, 100); /* 55 Hz */
        fm_engine_render(&feng, fbuf_a, FRAMES_HALF_S);
        level[step] = rms(fbuf_a, 4410, FRAMES_HALF_S);
        printf("    highpass step %u: 55 Hz %.2f dB, 110 Hz %.2f dB, "
               "note 33 RMS %.0f\n", step, g55[step], g110[step],
               level[step]);
    }
    /* step 0 is a wire */
    fm_filter_reset(&f);
    fm_filter_set_highpass(&f, 0, rate_cents);
    for (i = 0; i < 1000; i++) {
        int32_t x = sine_q24(0.9, 55.0, i);
        TT_CHECK_EQ(fm_filter_process(&f, x), x);
    }
    for (step = 1; step < 4; step++) {
        TT_CHECK(g55[step] < g55[step - 1] - 2.0);
        TT_CHECK(g110[step] < g110[step - 1] - 2.0);
        TT_CHECK(level[step] < level[step - 1]);
    }
    /* the highpass is out of range of the setting: clamped to step 3 */
    TT_CHECK_EQ(fm_engine_set_param(&feng, FM_PARAM_HIGHPASS_STEP, 9), FM_OK);
    TT_CHECK_EQ(fm_engine_get_param(&feng, FM_PARAM_HIGHPASS_STEP), 3);
}

static void run_filter_tests(void)
{
    TT_RUN(lowpass_passes_low_tones_and_rolls_off_24_db_per_octave);
    TT_RUN(resonance_raises_gain_near_cutoff);
    TT_RUN(max_resonance_stays_bounded_over_a_long_render);
    TT_RUN(key_tracking_moves_cutoff_with_note_pitch);
    TT_RUN(filter_envelope_sweeps_cutoff_up_or_down);
    TT_RUN(each_highpass_step_cuts_low_frequencies_more);
}
