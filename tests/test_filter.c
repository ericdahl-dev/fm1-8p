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
}
