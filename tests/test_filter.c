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
 * at feedback k; k = 4.2 at resonance 1000) and costs some bass. Midway,
 * where the cutoff point is near the passband level, the octave above
 * cutoff is about 24 dB below the cutoff point (theory 24.4 dB at 600).
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
    TT_CHECK(at500 > at0 + 5.0);    /* theory +6.5 dB */
    TT_CHECK(at900 > at500 + 12.0); /* theory +18.7 dB */
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
            int32_t peak = 0;
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
            }
            if (peak > FILTER_BOUND)
                printf("    cutoff %d, input %d: peak %.3f\n", (int)cutoffs[c],
                       (int)kind, peak / 16777216.0);
            TT_CHECK(peak <= FILTER_BOUND);
        }
    }
}

/*
 * Resonance 1000 self-oscillates: a single click sets the filter ringing
 * at a steady level near the cutoff frequency, and never at Nyquist (the
 * one-sample feedback delay can make a naive 4-pole loop buzz there).
 */
TT_TEST(max_resonance_self_oscillates_near_cutoff)
{
    static const int32_t cutoffs[] = { 2400, 4800, 6900, 9000, 10500, 12000,
                                       13000 };
    static fm_filter f;
    size_t c, i;
    for (c = 0; c < sizeof cutoffs / sizeof cutoffs[0]; c++) {
        double fc = 440.0 * pow(2.0, (cutoffs[c] - 6900) / 1200.0);
        double first = -1, last = -1, hz;
        int32_t prev = 0, peak = 0;
        int crossings = 0;
        lowpass_at(&f, cutoffs[c], 1000);
        for (i = 0; i < 4 * (size_t)FRATE; i++) {
            int32_t y = fm_filter_process(&f, i == 0 ? 1 << 24 : 0);
            if (i >= 3 * (size_t)FRATE) {
                if (prev < 0 && y >= 0) {
                    double t = (double)i - 1 +
                               (double)-prev / ((double)y - prev);
                    if (first < 0) first = t;
                    last = t;
                    crossings++;
                }
                if ((y < 0 ? -y : y) > peak) peak = y < 0 ? -y : y;
            }
            prev = y;
        }
        hz = crossings > 1 ? FRATE * (crossings - 1) / (last - first) : 0;
        printf("    cutoff %5d (%7.1f Hz): rings at %7.1f Hz, peak %.3f\n",
               (int)cutoffs[c], fc, hz, peak / 16777216.0);
        TT_CHECK(peak > (1 << 24) / 10); /* still ringing after 3 s */
        TT_CHECK(hz < 15000.0);           /* not a Nyquist buzz */
        if (fc < 4000.0) /* tuned within a semitone up to a few kHz */
            TT_CHECK(fabs(1200.0 * log2(hz / fc)) < 100.0);
    }
}

/*
 * At FM_FILTER_OPEN_CENTS (the default) the lowpass is an exact wire,
 * even at maximum resonance: the stage zero, the feedback and 1 - g all
 * fade to zero over the top of the range. That is what keeps the default
 * patch sounding exactly as it did before the filter.
 */
TT_TEST(open_lowpass_is_an_exact_wire_at_any_resonance)
{
    static const int32_t res[] = { 0, 500, 1000 };
    static const uint32_t rates[] = { 8000, 44100, 192000 };
    static fm_filter f;
    size_t r, q, i;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (q = 0; q < sizeof res / sizeof res[0]; q++) {
            int same = 1;
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, res[q]);
            fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS,
                                 fm_filter_rate_cents(rates[r]));
            for (i = 0; i < 20000; i++) {
                /* a 1 kHz sine, then a +-1.0 saw at 220 Hz */
                int32_t x = i < 10000
                    ? sine_q24(1.0, 1000.0, i)
                    : (int32_t)((i * 220 * 2 % FRATE) * (2u << 24) / FRATE) -
                          (1 << 24);
                if (fm_filter_process(&f, x) != x) same = 0;
            }
            TT_CHECK(same);
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

/* Sets every filter control to something audible. */
static void busy_filter_patch(fm_engine *e)
{
    fm_engine_set_param(e, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(e, FM_PARAM_FILTER_RESONANCE, 850);
    fm_engine_set_param(e, FM_PARAM_FILTER_KEY_TRACK, 700);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_ATTACK_MS, 20);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_DECAY_MS, 150);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_SUSTAIN, 300);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_RELEASE_MS, 60);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 3600);
    fm_engine_set_param(e, FM_PARAM_HIGHPASS_STEP, 1);
}

/* The cutoff updates at control rate on a counter kept in the engine,
 * so the block size still must not change the output. */
TT_TEST(filter_output_does_not_depend_on_block_size)
{
    static const uint32_t sizes[] = { 9000, 1, 15, 16, 17, 255, 256, 4096 };
    size_t k;
    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        int16_t *out = k == 0 ? fbuf_a : fbuf_b;
        uint32_t done = 0;
        fm_engine_init(&feng, FRATE, 1);
        busy_filter_patch(&feng);
        while (done < 9000) {
            uint32_t n = 9000 - done < sizes[k] ? 9000 - done : sizes[k];
            if (done == 1000) fm_engine_note_on(&feng, 57, 100);
            if (done == 6000) fm_engine_note_off(&feng, 57);
            if (done < 1000 && done + n > 1000) n = 1000 - done;
            if (done < 6000 && done + n > 6000) n = 6000 - done;
            fm_engine_render(&feng, out + 2 * done, n);
            done += n;
        }
        if (k > 0) TT_CHECK(memcmp(fbuf_a, fbuf_b, 9000 * 4) == 0);
    }
    TT_CHECK(peak_abs(fbuf_a, 2000, 5000) > 1000);
}

/*
 * Every filter extreme from the lowest to the highest engine rate:
 * nothing may divide by zero (the desktop traps), and the voice must
 * still fall silent after release. Includes the cutoff swept past both
 * ends by key tracking and the envelope. The filter module on its own
 * also takes rates far below the engine's (1 Hz here).
 */
TT_TEST(extreme_filter_settings_stay_bounded_and_release)
{
    static const uint32_t rates[] = { 8000, 22050, 44100, 192000 };
    static fm_filter f;
    int32_t low_rate = fm_filter_rate_cents(1), cents;
    for (cents = 0; cents <= FM_FILTER_OPEN_CENTS; cents += 600) {
        int32_t y;
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, 1000);
        fm_filter_set_highpass(&f, 3, low_rate);
        fm_filter_set_cutoff(&f, cents, low_rate);
        y = fm_filter_process(&f, 1 << 24);
        TT_CHECK(y <= FILTER_BOUND && y >= -FILTER_BOUND);
    }
    static const uint8_t notes[] = { 0, 60, 127 };
    static const int32_t cutoffs[] = { 0, FM_FILTER_OPEN_CENTS };
    static const int32_t amounts[] = { -9600, 9600 };
    size_t r, n, c, a, i;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++)
    for (n = 0; n < sizeof notes / sizeof notes[0]; n++)
    for (c = 0; c < 2; c++)
    for (a = 0; a < 2; a++) {
        int loud_after = 0;
        TT_CHECK_EQ(fm_engine_init(&feng, rates[r], 1), FM_OK);
        fm_engine_set_param(&feng, FM_PARAM_AMP_RELEASE_MS, 0);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoffs[c]);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_RESONANCE, 1000);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_DECAY_MS, 0);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
                            amounts[a]);
        fm_engine_set_param(&feng, FM_PARAM_HIGHPASS_STEP, 3);
        fm_engine_note_on(&feng, notes[n], 127);
        fm_engine_render(&feng, fbuf_a, 4000);
        fm_engine_note_off(&feng, notes[n]);
        fm_engine_render(&feng, fbuf_a, 2000);
        for (i = 2; i < 2 * 2000; i++)
            if (fbuf_a[i] != 0) loud_after = 1;
        TT_CHECK(!loud_after);
    }
}

/*
 * Plays note 57 with every filter control in use, setting parameter `id`
 * again to the value it already has every 10 ms (none if `id` is
 * FM_PARAM_COUNT). That must change nothing: in particular it must not
 * restart a running segment of either envelope.
 *
 * Re-setting an envelope's own setting does restart its running segment
 * (fm_env_retime: a new time applies at once), so each case keeps the
 * envelope being touched steady and the other one moving:
 *   0  a filter control: both envelopes releasing (note off at 200 ms)
 *   1  an amp setting: amp sustaining, filter envelope mid-decay
 *   2  a filter envelope setting: filter envelope sustaining, then idle
 *      (0 ms release), amp mid-decay and then releasing
 */
#define RESET_FRAMES 52920 /* 1.2 s */

static void play_resetting(int16_t *out, uint16_t id, int scenario)
{
    uint32_t done = 0, off = scenario == 1 ? 39690 : 8820;
    fm_engine_init(&feng, FRATE, 1);
    busy_filter_patch(&feng);
    fm_engine_set_param(&feng, FM_PARAM_AMP_DECAY_MS, scenario == 1 ? 20 : 400);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 300);
    fm_engine_set_param(&feng, FM_PARAM_AMP_RELEASE_MS, 300);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_DECAY_MS,
                        scenario == 2 ? 20 : 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_RELEASE_MS,
                        scenario == 2 ? 0 : 500);
    fm_engine_note_on(&feng, 57, 100);
    while (done < RESET_FRAMES) {
        if (done == off) fm_engine_note_off(&feng, 57);
        /* scenario 1 stops before the amp envelope releases */
        if (done >= 4410 && id != FM_PARAM_COUNT && (scenario != 1 || done < off))
            fm_engine_set_param(&feng, id, fm_engine_get_param(&feng, id));
        fm_engine_render(&feng, out + 2 * done, 441);
        done += 441;
    }
}

TT_TEST(setting_a_param_does_not_restart_the_other_envelopes)
{
    static int16_t untouched[2 * RESET_FRAMES], touched[2 * RESET_FRAMES];
    static const struct { uint16_t id; int scenario; } cases[] = {
        { FM_PARAM_FILTER_CUTOFF_CENTS, 0 }, { FM_PARAM_FILTER_RESONANCE, 0 },
        { FM_PARAM_FILTER_KEY_TRACK, 0 }, { FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0 },
        { FM_PARAM_HIGHPASS_STEP, 0 }, { FM_PARAM_BEND_RANGE, 0 },
        { FM_PARAM_AMP_ATTACK_MS, 1 }, { FM_PARAM_AMP_SUSTAIN, 1 },
        { FM_PARAM_AMP_RELEASE_MS, 1 },
        { FM_PARAM_FILTER_ENV_ATTACK_MS, 2 }, { FM_PARAM_FILTER_ENV_SUSTAIN, 2 },
        { FM_PARAM_FILTER_ENV_DECAY_MS, 2 }
    };
    size_t k;
    int scenario;
    for (scenario = 0; scenario < 3; scenario++) {
        play_resetting(untouched, FM_PARAM_COUNT, scenario);
        TT_CHECK(peak_abs(untouched, 2000, 8000) > 1000);
        if (scenario != 1) /* the release has ended */
            TT_CHECK(peak_abs(untouched, 30000, RESET_FRAMES) == 0);
        for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
            if (cases[k].scenario != scenario) continue;
            play_resetting(touched, cases[k].id, scenario);
            if (memcmp(untouched, touched, sizeof untouched) != 0)
                printf("    setting %s changed the output\n",
                       fm_param_info_get(cases[k].id)->name);
            TT_CHECK(memcmp(untouched, touched, sizeof untouched) == 0);
        }
    }
}

/*
 * In the voice: with the cutoff at its default (open), even maximum
 * resonance changes nothing at all, so the default patch is the plain
 * oscillator and amp envelope.
 */
TT_TEST(default_patch_is_unfiltered_even_with_resonance)
{
    static const uint8_t notes[] = { 33, 60, 96 };
    size_t n;
    for (n = 0; n < sizeof notes / sizeof notes[0]; n++) {
        fm_engine_init(&feng, FRATE, 1);
        fm_engine_note_on(&feng, notes[n], 100);
        fm_engine_render(&feng, fbuf_a, FRAMES_HALF_S);
        fm_engine_init(&feng, FRATE, 1);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_RESONANCE, 1000);
        fm_engine_note_on(&feng, notes[n], 100);
        fm_engine_render(&feng, fbuf_b, FRAMES_HALF_S);
        TT_CHECK(peak_abs(fbuf_a, 4410, FRAMES_HALF_S) > 1000);
        TT_CHECK(memcmp(fbuf_a, fbuf_b, FRAMES_HALF_S * 4) == 0);
    }
}

/* Sets a patch where the filter envelope alone decides the brightness:
 * amp full and slow to release, cutoff 4800 cents plus 6000 from a
 * filter envelope that holds full and releases in 30 ms. */
static void release_patch(fm_engine *e)
{
    fm_engine_set_param(e, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(e, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(e, FM_PARAM_AMP_RELEASE_MS, 3000);
    fm_engine_set_param(e, FM_PARAM_FILTER_CUTOFF_CENTS, 4800);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_DECAY_MS, 0);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_RELEASE_MS, 30);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 6000);
}

/*
 * Every way a note is released also releases its filter envelope: the
 * key coming up, the sustain pedal coming up, and a note whose key came
 * up while it waited for a steal fade (its release is queued). After the
 * release the tone darkens to the bare cutoff while the amp still sounds.
 */
TT_TEST(every_release_path_releases_the_filter_envelope)
{
    double held, direct, pedal, queued;
    /* the key comes up at 300 ms */
    fm_engine_init(&feng, FRATE, 1);
    release_patch(&feng);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_render(&feng, fbuf_a, 13230);
    fm_engine_note_off(&feng, 36);
    fm_engine_render(&feng, fbuf_a + 2 * 13230, 13230);
    held = brightness(fbuf_a, 2205, 13230);
    direct = brightness(fbuf_a, 13230 + 8820, 26460);
    /* held by the pedal, which comes up at 300 ms */
    fm_engine_init(&feng, FRATE, 1);
    release_patch(&feng);
    fm_engine_controller(&feng, 64, 127);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_note_off(&feng, 36);
    fm_engine_render(&feng, fbuf_b, 13230);
    fm_engine_controller(&feng, 64, 0);
    fm_engine_render(&feng, fbuf_b + 2 * 13230, 13230);
    pedal = brightness(fbuf_b, 13230 + 8820, 26460);
    /* one voice: note 36 steals it and its key comes up during the fade */
    fm_engine_init(&feng, FRATE, 1);
    release_patch(&feng);
    fm_engine_note_on(&feng, 40, 100);
    fm_engine_render(&feng, fbuf_b, 2000);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_note_off(&feng, 36);
    fm_engine_render(&feng, fbuf_b, 13230);
    queued = brightness(fbuf_b, 8820, 13230);
    printf("    brightness held %.4f; after release: key %.4f, pedal %.4f, "
           "queued %.4f\n", held, direct, pedal, queued);
    TT_CHECK(peak_abs(fbuf_a, 13230 + 8820, 26460) > 5000); /* amp sounds */
    TT_CHECK(direct < 0.5 * held);
    TT_CHECK(pedal < 0.5 * held);
    TT_CHECK(queued < 0.5 * held);
}

/*
 * All sound off (CC 120) also clears the filter's state, so a self-
 * oscillating filter does not ring into the next note: after it, a note
 * sounds the same whatever the filter was doing before. (The oscillator
 * keeps its phase, so the reference plays the same notes, with the
 * filter quiet before the controller.)
 */
TT_TEST(all_sound_off_clears_the_filter_ringing)
{
    int ring;
    for (ring = 0; ring < 2; ring++) {
        int16_t *out = ring ? fbuf_a : fbuf_b;
        fm_engine_init(&feng, FRATE, 1);
        fm_engine_set_param(&feng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 6900);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_RESONANCE, ring ? 1000 : 0);
        fm_engine_note_on(&feng, 48, 100);
        fm_engine_render(&feng, out, FRATE / 4);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_RESONANCE, 1000);
        fm_engine_controller(&feng, 120, 0);
        fm_engine_note_on(&feng, 48, 100);
        fm_engine_render(&feng, out, 4410);
    }
    TT_CHECK(peak_abs(fbuf_a, 0, 4410) > 1000);
    TT_CHECK(memcmp(fbuf_a, fbuf_b, 4410 * 4) == 0);
}

/*
 * A note's cutoff applies from its first sample, even when it starts in
 * the middle of a control period: here the previous note left the
 * counter partway through, and the new note's filter is nearly closed
 * where the old one was open.
 */
TT_TEST(new_note_gets_its_cutoff_from_the_first_sample)
{
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&feng, 60, 100);
    fm_engine_render(&feng, fbuf_a, 5); /* 5 of 16 samples into a period */
    TT_CHECK(peak_abs(fbuf_a, 0, 5) > 1000); /* open */
    fm_engine_controller(&feng, 120, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 0);
    fm_engine_note_on(&feng, 60, 100);
    fm_engine_render(&feng, fbuf_a, 16);
    TT_CHECK(peak_abs(fbuf_a, 0, 16) < 100);
}

/*
 * The cutoff follows its controls at a 16-sample control rate: a cutoff
 * change mid-note is heard within 16 samples.
 */
TT_TEST(cutoff_changes_apply_within_16_samples)
{
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 0);
    fm_engine_note_on(&feng, 60, 100);
    fm_engine_render(&feng, fbuf_a, 100);
    TT_CHECK(peak_abs(fbuf_a, 0, 100) < 100); /* nearly closed */
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS,
                        FM_FILTER_OPEN_CENTS);
    fm_engine_render(&feng, fbuf_a, 64);
    /* open (a wire) from the next update on: the full saw */
    TT_CHECK(peak_abs(fbuf_a, 16, 64) > 10000);
}

/*
 * Unison (docs/voices.md#unison): a new note retriggers every voice, and
 * the filter envelope restarts with the amp envelope; a legato return to
 * a key still down carries on, as the amp envelope does. Here the filter
 * envelope sweeps from +4800 cents down to the bare cutoff in 100 ms.
 */
TT_TEST(unison_retrigger_restarts_the_filter_envelope_and_legato_does_not)
{
    double settled, retriggered, legato;
    fm_engine_init(&feng, FRATE, 4);
    fm_engine_set_param(&feng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&feng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 4800);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_DECAY_MS, 100);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_SUSTAIN, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 4800);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_render(&feng, fbuf_a, 13230); /* 300 ms: swept down */
    settled = brightness(fbuf_a, 8820, 13230);
    fm_engine_note_on(&feng, 36 + 7, 100); /* retrigger */
    fm_engine_render(&feng, fbuf_a, 13230);
    retriggered = brightness(fbuf_a, 0, 1323); /* first 30 ms */
    fm_engine_note_off(&feng, 36 + 7); /* legato back to 36 */
    fm_engine_render(&feng, fbuf_a, 4410);
    legato = brightness(fbuf_a, 0, 1323);
    printf("    unison brightness: settled %.4f, retriggered %.4f, "
           "legato %.4f\n", settled, retriggered, legato);
    TT_CHECK(retriggered > 2.0 * settled);
    TT_CHECK_NEAR(legato / settled, 1.0, 0.1);
}

/* Largest change between neighboring samples of one channel over
 * frames [from, to): large for a bright high note, small once filtered. */
static int max_step(const int16_t *stereo, size_t from, size_t to)
{
    int m = 0;
    size_t i;
    for (i = from + 1; i < to; i++) {
        int d = stereo[2 * i] - stereo[2 * (i - 1)];
        if (d < 0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

/*
 * A unison note change applies the new note's tracked cutoff from its
 * first sample, as a poly note start does. With full key tracking and a
 * 7700-cent cutoff, note 0's cutoff is 1700 cents (22 Hz) and note 127's
 * is past open (a wire), so the new note's 12.5 kHz saw comes straight
 * through; with the old note's cutoff it would be filtered almost away.
 */
TT_TEST(unison_note_change_gets_its_cutoff_from_the_first_sample)
{
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&feng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 7700);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
    fm_engine_note_on(&feng, 0, 100);
    fm_engine_render(&feng, fbuf_a, 4405); /* 5 into a control period */
    TT_CHECK(max_step(fbuf_a, 4000, 4405) < 100);
    fm_engine_note_on(&feng, 127, 100);
    fm_engine_render(&feng, fbuf_a, 11);
    printf("    unison note change: largest step in 11 samples %d\n",
           max_step(fbuf_a, 0, 11));
    TT_CHECK(max_step(fbuf_a, 0, 11) > 8000);
}

/*
 * A voice-mode switch releases every note like a note off, and the voices
 * still sounding then belong to no note. Their filter must keep tracking
 * the key they were playing, so the release sounds exactly like a plain
 * note off.
 */
TT_TEST(mode_switch_release_keeps_the_key_tracked_cutoff)
{
    int sw;
    for (sw = 0; sw < 2; sw++) {
        int16_t *out = sw ? fbuf_a : fbuf_b;
        fm_engine_init(&feng, FRATE, 2);
        fm_engine_set_param(&feng, FM_PARAM_AMP_RELEASE_MS, 500);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
        fm_engine_note_on(&feng, 36, 100);
        fm_engine_render(&feng, out, 4410);
        if (sw)
            fm_engine_set_param(&feng, FM_PARAM_VOICE_MODE,
                                FM_VOICE_MODE_UNISON);
        else
            fm_engine_note_off(&feng, 36);
        fm_engine_render(&feng, out + 2 * 4410, 8820);
    }
    TT_CHECK(peak_abs(fbuf_a, 4410, 8820) > 1000);
    TT_CHECK(memcmp(fbuf_a, fbuf_b, 13230 * 4) == 0);
}

/* The filter envelope patch of the steal tests: a 200 ms attack to full,
 * held, sweeping a 3600-cent cutoff up 4800 cents. */
static void sweep_patch(fm_engine *e)
{
    fm_engine_set_param(e, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(e, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(e, FM_PARAM_FILTER_CUTOFF_CENTS, 3600);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_ATTACK_MS, 200);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 4800);
}

/*
 * A note that steals a voice mid-sustain starts its filter envelope from
 * 0, as its amp envelope does after the steal fade, so its sweep is the
 * same as on a fresh voice whichever voice it lands on.
 */
TT_TEST(stolen_voice_starts_the_filter_envelope_from_zero)
{
    static fm_engine fresh;
    int guard = 0;
    fm_engine_init(&feng, FRATE, 1);
    sweep_patch(&feng);
    fm_engine_note_on(&feng, 40, 100);
    fm_engine_render(&feng, fbuf_a, 22050); /* held at full */
    TT_CHECK_EQ(feng.voices[0].filter_env.level, FM_ENV_FULL);
    fm_engine_note_on(&feng, 70, 100); /* steals the only voice */
    while (feng.voices[0].pending && guard++ < 1000)
        fm_engine_render(&feng, fbuf_a, 1); /* the new note starts in it */
    TT_CHECK(!feng.voices[0].pending);
    fm_engine_render(&feng, fbuf_a, 2000);

    fm_engine_init(&fresh, FRATE, 1);
    sweep_patch(&fresh);
    fm_engine_note_on(&fresh, 70, 100);
    fm_engine_render(&fresh, fbuf_b, 2001);
    printf("    filter env 2001 samples into the note: stolen %ld, "
           "fresh %ld\n", (long)feng.voices[0].filter_env.level,
           (long)fresh.voices[0].filter_env.level);
    TT_CHECK_EQ(feng.voices[0].filter_env.level,
                fresh.voices[0].filter_env.level);
}

/*
 * The same on a voice that went idle (its amp release ended) while its
 * filter envelope was still releasing: the next note's filter envelope
 * starts from 0, not from that leftover level.
 */
TT_TEST(idle_voice_starts_the_filter_envelope_from_zero)
{
    fm_engine_init(&feng, FRATE, 1);
    sweep_patch(&feng);
    fm_engine_set_param(&feng, FM_PARAM_AMP_RELEASE_MS, 10);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_RELEASE_MS, 5000);
    fm_engine_note_on(&feng, 40, 100);
    fm_engine_render(&feng, fbuf_a, 22050);
    fm_engine_note_off(&feng, 40);
    fm_engine_render(&feng, fbuf_a, 4410); /* amp idle, filter env high */
    TT_CHECK(!fm_voice_active(&feng.voices[0]));
    TT_CHECK(feng.voices[0].filter_env.level > FM_ENV_FULL / 2);
    fm_engine_note_on(&feng, 40, 100);
    TT_CHECK_EQ(feng.voices[0].filter_env.level, 0);
}

/*
 * During a steal fade the old note keeps its own key-tracked cutoff; the
 * new note's key tracking applies when the new note starts, as its pitch
 * does. Full key tracking, note 36 stolen by note 96 (5 octaves up), in
 * three cases, since the cutoff is recomputed during the fade only when
 * something asks for it: (0) nothing does, (1) a held filter envelope
 * amount recomputes it every 16 samples, (2) the cutoff setting moves a
 * cent in the middle of the fade. Throughout the fade the coefficient
 * must be note 36's for the cutoff setting in force.
 */
TT_TEST(steal_fade_keeps_the_old_notes_key_tracked_cutoff)
{
    static fm_filter want;
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    int how;
    for (how = 0; how < 3; how++) {
        int32_t g_old, base = 6000, env = how == 1 ? 1200 : 0;
        int changed_in_fade = 0, guard = 0;
        fm_engine_init(&feng, FRATE, 1);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
        fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
        if (how == 1) { /* held at full: +1200 cents, constant */
            fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
            fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
            fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
                                1200);
        }
        fm_engine_note_on(&feng, 36, 100);
        fm_engine_render(&feng, fbuf_a, 4410);
        g_old = feng.voices[0].filter.g;
        fm_engine_note_on(&feng, 96, 100);
        while (feng.voices[0].pending && guard++ < 1000) {
            if (how == 2 && guard == 40) {
                base = 6001;
                fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS,
                                    base);
            }
            fm_engine_render(&feng, fbuf_a, 1);
            if (!feng.voices[0].pending) break; /* 96 has started */
            if (feng.voices[0].ctrl_phase == 15) {
                /* just updated (every 16 samples): note 36's cutoff
                 * for the setting in force */
                fm_filter_reset(&want);
                fm_filter_set_cutoff(&want, base + (36 - 60) * 100 + env,
                                     rate_cents);
                if (feng.voices[0].filter.g != want.g) changed_in_fade = 1;
            }
        }
        if (changed_in_fade)
            printf("    case %d: not note 36's cutoff in the fade\n", how);
        TT_CHECK(!changed_in_fade);
        fm_engine_render(&feng, fbuf_a, 32);
        TT_CHECK(feng.voices[0].filter.g > g_old); /* the new note's */
    }
}

/*
 * A mode switch during a steal fade: the voice still starts the waiting
 * note when the fade ends (it then belongs to no key), and its filter
 * tracks that note, not the no-note marker.
 */
TT_TEST(mode_switch_during_a_steal_fade_tracks_the_waiting_note)
{
    static fm_filter want;
    int guard = 0;
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
    fm_engine_note_on(&feng, 36, 100);
    fm_engine_render(&feng, fbuf_a, 4410);
    fm_engine_note_on(&feng, 96, 100); /* steal: 96 waits for the fade */
    fm_engine_set_param(&feng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    while (feng.voices[0].pending && guard++ < 1000)
        fm_engine_render(&feng, fbuf_a, 1);
    fm_engine_render(&feng, fbuf_a, 32);
    fm_filter_reset(&want);
    fm_filter_set_cutoff(&want, fm_filter_voice_cents(6000, 1000, 96, 0, 0),
                         rate_cents);
    TT_CHECK_EQ(feng.voices[0].filter.g, want.g);
}

/*
 * Turning the highpass on mid-note starts it from the signal as it is:
 * no step in the output from state left over from an earlier time it was
 * on. A DC input of +0.5 with the highpass on, then -0.5 with it off,
 * then on again: the output must move smoothly from -0.5.
 */
TT_TEST(highpass_turned_on_mid_note_does_not_click)
{
    static fm_filter f;
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    int32_t prev, y, worst = 0;
    int i;
    fm_filter_reset(&f);
    fm_filter_set_highpass(&f, 3, rate_cents);
    for (i = 0; i < (int)FRATE; i++) fm_filter_process(&f, 1 << 23);
    fm_filter_set_highpass(&f, 0, rate_cents);
    for (i = 0; i < (int)FRATE; i++) prev = fm_filter_process(&f, -(1 << 23));
    fm_filter_set_highpass(&f, 1, rate_cents);
    for (i = 0; i < 100; i++) {
        int32_t d;
        y = fm_filter_process(&f, -(1 << 23));
        d = y > prev ? y - prev : prev - y;
        if (d > worst) worst = d;
        prev = y;
    }
    printf("    highpass on: largest step %.4f of full scale\n",
           worst / 16777216.0);
    TT_CHECK(worst < (1 << 24) / 100);
}

/*
 * The wide-open default (cutoff at the top, no resonance, highpass off)
 * is a wire, and the filter's state must stay what running the filter
 * would leave: every stage input and output equal to the last sample. So
 * moving the cutoff off the top is the same, bit for bit, as from a filter
 * primed that way, and a sine passes the change without a step.
 */
TT_TEST(leaving_the_open_filter_continues_from_a_consistent_state)
{
    static fm_filter f, ref;
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    int how;
    /* 0: the cutoff leaves the top; 1: the highpass turns on and then,
     * before another sample, the cutoff leaves the top */
    for (how = 0; how < 2; how++) {
        int32_t last = 0, prev = 0, worst = 0;
        int i, j, same = 1, wire = 1;
        fm_filter_reset(&f);
        fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS, rate_cents);
        for (i = 0; i < 1000; i++) {
            last = sine_q24(0.9, 220.0, (size_t)i);
            prev = fm_filter_process(&f, last);
            if (prev != last) wire = 0;
        }
        TT_CHECK(wire);
        ref = f;
        for (j = 0; j < 4; j++) ref.s[j] = ref.x1[j] = last;
        if (how) {
            fm_filter_set_highpass(&f, 1, rate_cents);
            fm_filter_set_highpass(&ref, 1, rate_cents);
        }
        fm_filter_set_resonance(&f, 700);
        fm_filter_set_cutoff(&f, 9000, rate_cents);
        fm_filter_set_resonance(&ref, 700);
        fm_filter_set_cutoff(&ref, 9000, rate_cents);
        for (; i < 2000; i++) {
            int32_t x = sine_q24(0.9, 220.0, (size_t)i);
            int32_t y = fm_filter_process(&f, x), d;
            if (y != fm_filter_process(&ref, x)) same = 0;
            d = y > prev ? y - prev : prev - y;
            if (d > worst) worst = d;
            prev = y;
        }
        TT_CHECK(same);
        TT_CHECK(worst < (1 << 24) / 20); /* the sine moves < 0.03/sample */
    }
}

/*
 * A unison note change keeps the voice's 16-sample cutoff cadence (all
 * voices stay spread as they were) and refreshes the cutoff once, at
 * once, for the new note.
 */
TT_TEST(unison_note_change_keeps_the_control_cadence)
{
    uint8_t phase;
    fm_engine_init(&feng, FRATE, 2);
    fm_engine_set_param(&feng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 7700);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_KEY_TRACK, 1000);
    fm_engine_note_on(&feng, 0, 100);
    fm_engine_render(&feng, fbuf_a, 4405);
    phase = feng.voices[0].ctrl_phase;
    fm_engine_note_on(&feng, 127, 100);
    TT_CHECK_EQ(feng.voices[0].ctrl_phase, phase);
}

/*
 * fm_env_advance(e, n) is n calls of fm_env_next(e) at once: random
 * envelopes through every stage (gate on, retime, gate off, fade), with
 * random advance lengths, against stepping one sample at a time.
 */
TT_TEST(env_advance_matches_stepping_sample_by_sample)
{
    static fm_env a, b;
    uint32_t seed = 12345;
    int trial, op, mismatches = 0;
#define ENV_RAND() (seed = seed * 1664525u + 1013904223u, seed >> 8)
    for (trial = 0; trial < 300; trial++) {
        fm_env_reset(&a);
        a.attack_samples = ENV_RAND() % 3000;
        a.decay_samples = ENV_RAND() % 3000;
        a.release_samples = ENV_RAND() % 3000;
        a.sustain = (int32_t)(ENV_RAND() % ((1u << 30) + 1));
        b = a;
        for (op = 0; op < 40; op++) {
            uint32_t n = ENV_RAND() % 400, k;
            switch (ENV_RAND() % 6) {
            case 0: fm_env_gate_on(&a); fm_env_gate_on(&b); break;
            case 1: fm_env_gate_off(&a); fm_env_gate_off(&b); break;
            case 2: fm_env_fade(&a, n); fm_env_fade(&b, n); break;
            case 3:
                a.sustain = b.sustain =
                    (int32_t)(ENV_RAND() % ((1u << 30) + 1));
                fm_env_retime(&a);
                fm_env_retime(&b);
                break;
            default: break;
            }
            fm_env_advance(&a, n);
            for (k = 0; k < n; k++) fm_env_next(&b);
            if (a.level != b.level || a.stage != b.stage ||
                a.step != b.step)
                mismatches++;
        }
    }
#undef ENV_RAND
    TT_CHECK_EQ(mismatches, 0);
}

/* The patch of the skipped-envelope test, with filter env amount `amount`. */
static void lag_patch(fm_engine *e, int32_t amount)
{
    fm_engine_set_param(e, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_ATTACK_MS, 30);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_DECAY_MS, 70);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_SUSTAIN, 400);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_RELEASE_MS, 50);
    fm_engine_set_param(e, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, amount);
}

/* 1 if every voice's filter envelope matches between `a`, synced first,
 * and `b`. */
static int filter_envs_match(fm_engine *a, fm_engine *b)
{
    uint8_t i;
    for (i = 0; i < FM_MAX_VOICES; i++) {
        fm_voice_sync_filter_env(&a->voices[i]);
        if (a->voices[i].filter_env.level != b->voices[i].filter_env.level ||
            a->voices[i].filter_env.stage != b->voices[i].filter_env.stage)
            return 0;
    }
    return 1;
}

/*
 * With the filter envelope amount at 0 the envelope does not change the
 * sound, so the render skips stepping it every sample and catches it up
 * (fm_env_advance) at each cutoff update and before anything that uses
 * its level: a gate, a setting change, the amount becoming non-zero. It
 * must stay exactly where it would have been: here against an engine
 * whose amount is non-zero all along, through notes, a retrigger, a
 * steal, a queued release, a setting change, and the amount turned up
 * mid-note.
 */
TT_TEST(filter_envelope_skipped_at_zero_amount_stays_in_time)
{
    static fm_engine a, b;
    int ok = 1;
    fm_engine_init(&a, FRATE, 2);
    fm_engine_init(&b, FRATE, 2);
    lag_patch(&a, 0);
    lag_patch(&b, 1200);
    fm_engine_note_on(&a, 60, 100);
    fm_engine_note_on(&b, 60, 100);
    fm_engine_render(&a, fbuf_a, 37);
    fm_engine_render(&b, fbuf_b, 37);
    fm_engine_set_param(&a, FM_PARAM_FILTER_ENV_ATTACK_MS, 5); /* retime */
    fm_engine_set_param(&b, FM_PARAM_FILTER_ENV_ATTACK_MS, 5);
    fm_engine_render(&a, fbuf_a, 1001);
    fm_engine_render(&b, fbuf_b, 1001);
    ok &= filter_envs_match(&a, &b);
    fm_engine_render(&a, fbuf_a, 1205); /* in decay, mid control period */
    fm_engine_render(&b, fbuf_b, 1205);
    fm_engine_note_on(&a, 60, 100); /* retrigger from the current level */
    fm_engine_note_on(&b, 60, 100);
    fm_engine_render(&a, fbuf_a, 50);
    fm_engine_render(&b, fbuf_b, 50);
    ok &= filter_envs_match(&a, &b);
    fm_engine_note_on(&a, 64, 100);
    fm_engine_note_on(&b, 64, 100);
    fm_engine_note_on(&a, 67, 100); /* steals: waits for the fade */
    fm_engine_note_on(&b, 67, 100);
    fm_engine_note_off(&a, 67); /* queued release */
    fm_engine_note_off(&b, 67);
    fm_engine_render(&a, fbuf_a, 333);
    fm_engine_render(&b, fbuf_b, 333);
    ok &= filter_envs_match(&a, &b);
    fm_engine_note_off(&a, 64);
    fm_engine_note_off(&b, 64);
    fm_engine_render(&a, fbuf_a, 21);
    fm_engine_render(&b, fbuf_b, 21);
    fm_engine_set_param(&a, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 1200);
    fm_engine_render(&a, fbuf_a, 777);
    fm_engine_render(&b, fbuf_b, 777);
    {
        uint8_t i;
        for (i = 0; i < FM_MAX_VOICES; i++)
            if (a.voices[i].filter_env.level != b.voices[i].filter_env.level)
                ok = 0; /* no sync: the amount is on, it steps every sample */
    }
    TT_CHECK(ok);
}

/*
 * Turning the envelope amount down to 0 mid-note takes the envelope's
 * part out of the cutoff within one control period, as any cutoff change
 * does: the cutoff returns to the bare setting.
 */
TT_TEST(envelope_amount_to_zero_returns_the_cutoff_to_its_setting)
{
    static fm_filter want;
    int32_t rate_cents = fm_filter_rate_cents(FRATE);
    fm_engine_init(&feng, FRATE, 1);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400);
    fm_engine_note_on(&feng, 60, 100);
    fm_engine_render(&feng, fbuf_a, 4410);
    fm_engine_set_param(&feng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0);
    fm_engine_render(&feng, fbuf_a, 16);
    fm_filter_reset(&want);
    fm_filter_set_cutoff(&want, 6000, rate_cents);
    TT_CHECK_EQ(feng.voices[0].filter.g, want.g);
}

static void run_filter_tests(void)
{
    TT_RUN(lowpass_passes_low_tones_and_rolls_off_24_db_per_octave);
    TT_RUN(resonance_raises_gain_near_cutoff);
    TT_RUN(max_resonance_stays_bounded_over_a_long_render);
    TT_RUN(max_resonance_self_oscillates_near_cutoff);
    TT_RUN(key_tracking_moves_cutoff_with_note_pitch);
    TT_RUN(filter_envelope_sweeps_cutoff_up_or_down);
    TT_RUN(each_highpass_step_cuts_low_frequencies_more);
    TT_RUN(filter_output_does_not_depend_on_block_size);
    TT_RUN(extreme_filter_settings_stay_bounded_and_release);
    TT_RUN(setting_a_param_does_not_restart_the_other_envelopes);
    TT_RUN(open_lowpass_is_an_exact_wire_at_any_resonance);
    TT_RUN(default_patch_is_unfiltered_even_with_resonance);
    TT_RUN(every_release_path_releases_the_filter_envelope);
    TT_RUN(all_sound_off_clears_the_filter_ringing);
    TT_RUN(new_note_gets_its_cutoff_from_the_first_sample);
    TT_RUN(cutoff_changes_apply_within_16_samples);
    TT_RUN(unison_retrigger_restarts_the_filter_envelope_and_legato_does_not);
    TT_RUN(unison_note_change_gets_its_cutoff_from_the_first_sample);
    TT_RUN(mode_switch_release_keeps_the_key_tracked_cutoff);
    TT_RUN(stolen_voice_starts_the_filter_envelope_from_zero);
    TT_RUN(idle_voice_starts_the_filter_envelope_from_zero);
    TT_RUN(steal_fade_keeps_the_old_notes_key_tracked_cutoff);
    TT_RUN(mode_switch_during_a_steal_fade_tracks_the_waiting_note);
    TT_RUN(highpass_turned_on_mid_note_does_not_click);
    TT_RUN(leaving_the_open_filter_continues_from_a_consistent_state);
    TT_RUN(unison_note_change_keeps_the_control_cadence);
    TT_RUN(env_advance_matches_stepping_sample_by_sample);
    TT_RUN(filter_envelope_skipped_at_zero_amount_stays_in_time);
    TT_RUN(envelope_amount_to_zero_returns_the_cutoff_to_its_setting);
}
