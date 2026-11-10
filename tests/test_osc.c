/*
 * Oscillator section tests: two oscillators per voice, each with saw,
 * pulse, square and noise, octave and fine tune, and a mix between them.
 * Driven through the parameter table and fm_engine_* calls, the same way
 * the host plays them.
 */
#include "../engine/fm1_8p.h"

/* Sets up a held note on osc1 alone (mix fully to osc1). */
static void osc_init(void)
{
    fm_engine_init(&eng, RATE, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
}

/* Mean period (in samples) of a held note, skipping the first 0.1 s. */
static double held_period(uint8_t note)
{
    fm_engine_note_on(&eng, note, 100);
    fm_engine_render(&eng, buf, RATE);
    return period_from_crossings(buf + 2 * 4410, RATE - 4410, 0);
}

/* Saw, square and a narrow pulse each repeat at the note's frequency. */
TT_TEST(each_wave_renders_at_note_pitch)
{
    static const uint8_t waves[] = { FM_WAVE_SAW, FM_WAVE_PULSE, FM_WAVE_SQUARE };
    static const uint8_t notes[] = { 33, 69, 93 };
    size_t w, n;
    for (w = 0; w < sizeof waves; w++) {
        for (n = 0; n < sizeof notes; n++) {
            double want = RATE / note_hz(notes[n]);
            osc_init();
            TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, waves[w]),
                        FM_OK);
            fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, 20);
            TT_CHECK_NEAR(held_period(notes[n]), want, want * 0.0006); /* 1 cent */
        }
    }
}

/*
 * Fraction of samples above zero over whole cycles. The pulse has no DC
 * (it sits at +2(1 - w) for a fraction w of the cycle and -2w for the
 * rest), so this is its duty cycle.
 */
static double duty_cycle(const int16_t *stereo, size_t frames)
{
    size_t i, high = 0;
    for (i = 0; i < frames; i++)
        if (stereo[2 * i] > 0) high++;
    return (double)high / (double)frames;
}

TT_TEST(pulse_width_sets_duty_cycle)
{
    static const int32_t widths[] = { 10, 25, 50, 75, 90 };
    size_t k;
    for (k = 0; k < sizeof widths / sizeof widths[0]; k++) {
        osc_init();
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
        TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT,
                                        widths[k]), FM_OK);
        /* 110 Hz is 400.9 samples a cycle; 100 cycles from 0.1 s */
        fm_engine_note_on(&eng, 45, 100);
        fm_engine_render(&eng, buf, 4410 + 40091);
        TT_CHECK_NEAR(duty_cycle(buf + 2 * 4410, 40091), widths[k] / 100.0,
                      0.005);
    }
}

/*
 * Fraction of samples within 10% of the peak level: a two-level wave (a
 * square) sits there almost all the time, a saw only a tenth of it.
 */
static double near_peak_fraction(const int16_t *stereo, size_t frames)
{
    int peak = peak_abs(stereo, 0, frames);
    size_t i, near = 0;
    for (i = 0; i < frames; i++)
        if (abs(stereo[2 * i]) * 10 >= peak * 9) near++;
    return (double)near / (double)frames;
}

/* The square stays a 50% two-level wave whatever the pulse width says. */
TT_TEST(square_ignores_pulse_width)
{
    static const int32_t widths[] = { 10, 90 };
    size_t k;
    for (k = 0; k < sizeof widths / sizeof widths[0]; k++) {
        osc_init();
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SQUARE);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, widths[k]);
        fm_engine_note_on(&eng, 45, 100);
        fm_engine_render(&eng, buf, 4410 + 40091);
        TT_CHECK_NEAR(duty_cycle(buf + 2 * 4410, 40091), 0.5, 0.005);
        TT_CHECK(near_peak_fraction(buf + 2 * 4410, 40091) > 0.95);
    }
}

/* Duty cycle of a pair's osc1 pulse at `pct`, note 45, over 100 cycles. */
static double pair_duty(int32_t pct)
{
    static fm_osc_pair pair;
    size_t i, high = 0;
    fm_osc_pair_reset(&pair, 1);
    fm_osc_pair_set_wave(&pair, 0, FM_WAVE_PULSE);
    fm_osc_pair_set_width(&pair, 0, pct);
    fm_osc_pair_set_pitch(&pair, 45 << 16, RATE);
    for (i = 0; i < 40091; i++)
        if (fm_osc_pair_next(&pair) > 0) high++;
    return (double)high / 40091;
}

/* Widths outside 1..99% play as the nearest end of that range. */
TT_TEST(pulse_width_outside_its_range_is_clamped)
{
    TT_CHECK_NEAR(pair_duty(100), pair_duty(99), 0.001);
    TT_CHECK_NEAR(pair_duty(1000), pair_duty(99), 0.001);
    TT_CHECK_NEAR(pair_duty(0), pair_duty(1), 0.001);
    TT_CHECK_NEAR(pair_duty(-5), pair_duty(1), 0.001);
    TT_CHECK_NEAR(pair_duty(99), 0.99, 0.005);
}

/* Peak of a held note 45 on osc1 with the given wave and width. */
static int held_peak(int32_t wave, int32_t width)
{
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, wave);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, width);
    fm_engine_note_on(&eng, 45, 100);
    fm_engine_render(&eng, buf, 4410 + 8820);
    return peak_abs(buf, 4410, 4410 + 8820);
}

/* Switching saw to pulse or square never jumps in level: same peak. */
TT_TEST(pulse_peaks_at_the_saw_level_at_every_width)
{
    static const int32_t widths[] = { 1, 10, 25, 50, 75, 90, 99 };
    double saw = held_peak(FM_WAVE_SAW, 50);
    size_t k;
    TT_CHECK(saw > 1000);
    TT_CHECK_NEAR(held_peak(FM_WAVE_SQUARE, 50) / saw, 1.0, 0.03);
    for (k = 0; k < sizeof widths / sizeof widths[0]; k++)
        TT_CHECK_NEAR(held_peak(FM_WAVE_PULSE, widths[k]) / saw, 1.0, 0.03);
}

/* Octave shifts by factors of two; fine tune by 2^(cents / 1200). */
TT_TEST(octave_and_fine_tune_shift_pitch)
{
    static const struct { int32_t octave, cents; } cases[] = {
        { 1, 0 }, { -1, 0 }, { 2, 0 }, { -2, 0 },
        { 0, 50 }, { 0, -50 }, { 0, 100 }, { 0, -100 }, { 0, 7 }, { 1, -33 }
    };
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        double shift = cases[k].octave + cases[k].cents / 1200.0;
        double want = RATE / (note_hz(57) * pow(2.0, shift));
        osc_init();
        TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_OCTAVE,
                                        cases[k].octave), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_FINE_CENTS,
                                        cases[k].cents), FM_OK);
        TT_CHECK_NEAR(held_period(57), want, want * 0.0006); /* 1 cent */
    }
}

/* osc2's coarse tune moves it by whole semitones: +7 is a fifth up. */
TT_TEST(osc2_coarse_tune_shifts_by_semitones)
{
    double want = RATE / (note_hz(57) * pow(2.0, 7 / 12.0));
    osc_init();
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC2_COARSE_SEMIS, 7), FM_OK);
    TT_CHECK_NEAR(held_period(57), want, want * 0.0006); /* 1 cent */
}

/* Coarse tune back at 0 sounds exactly like coarse tune never touched. */
TT_TEST(osc2_coarse_zero_matches_no_coarse)
{
    static int16_t plain[2 * 4096];
    size_t i;
    int same = 1;
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 500);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, 1);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 37);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, plain, 4096);
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 500);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, 1);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_COARSE_SEMIS, 7);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 37);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_COARSE_SEMIS, 0);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4096);
    for (i = 0; i < 2 * 4096; i++)
        if (buf[i] != plain[i]) same = 0;
    TT_CHECK(same);
}

/*
 * Coarse tune changed on a held note retunes osc2 from there on, without
 * restarting its cycle: at sample 441 osc2 is 0.2 into a cycle, where a
 * restart would jump about 0.4 of full scale and a saw moves about 0.01.
 */
TT_TEST(osc2_coarse_change_retunes_a_held_note_without_a_restart)
{
    static int16_t head[2 * 441];
    double want = RATE / (note_hz(57) * pow(2.0, 7 / 12.0));
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 1000);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, head, 441);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_COARSE_SEMIS, 7);
    fm_engine_render(&eng, buf, RATE);
    TT_CHECK(abs(buf[0] - head[2 * 440]) < 1000);
    TT_CHECK_NEAR(period_from_crossings(buf, RATE, 0), want, want * 0.0006);
}

/*
 * fm_osc_pitch_inc against 2^(cents / 1200) in double precision, every
 * cent over +-2 octaves at low, middle and high notes: within 0.01 cent.
 * Today's worst, 0.005 cent, is the pitch table's rounding at note 0;
 * dropping the cubic term of the 2^x series makes it 0.056.
 */
TT_TEST(tuned_increment_matches_exact_pitch)
{
    static const uint8_t notes[] = { 24, 60, 96 };
    double worst = 0;
    size_t n;
    int32_t c;
    for (n = 0; n < sizeof notes; n++) {
        for (c = -2400; c <= 2400; c++) {
            double want = note_hz(notes[n]) * pow(2.0, c / 1200.0) / RATE *
                          4294967296.0;
            double got = fm_osc_pitch_inc(((int32_t)notes[n] << 16) +
                                          c * 65536 / 100, RATE);
            double err = fabs(1200.0 * log2(got / want));
            if (err > worst) worst = err;
        }
    }
    printf("    tuning: worst error %.4f cent\n", worst);
    TT_CHECK(worst < 0.01);
}

/*
 * Tuning keeps its interval past the ends of the note table: two octaves
 * up from note 110 is four times its frequency (not clamped at note 131),
 * two down from note 20 a quarter. Above Nyquist the cap still holds.
 */
TT_TEST(octave_shifts_keep_their_ratio_at_the_extremes)
{
    double hi = fm_osc_pitch_inc(110 << 16, RATE);
    double lo = fm_osc_pitch_inc(20 << 16, RATE);
    TT_CHECK_NEAR(fm_osc_pitch_inc((110 + 24) << 16, RATE) / hi, 4.0, 1e-5);
    TT_CHECK_NEAR(fm_osc_pitch_inc((20 - 24) * 65536, RATE) / lo, 0.25, 1e-5);
    TT_CHECK_NEAR((double)fm_osc_pitch_inc((3 - 26) * 65536, RATE) /
                      fm_osc_pitch_inc(3 << 16, RATE),
                  pow(2.0, -26 / 12.0), 1e-4);
    TT_CHECK_EQ(fm_osc_pitch_inc((127 + 26) << 16, RATE), FM_OSC_MAX_INC);
}

/* Retuning a held note takes effect at once, without a new note on. */
TT_TEST(retuning_a_held_note_takes_effect)
{
    double want = RATE / (note_hz(57) * 2);
    osc_init();
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 441);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_OCTAVE, 1);
    fm_engine_render(&eng, buf, RATE);
    TT_CHECK_NEAR(period_from_crossings(buf, RATE, 0), want, want * 0.0006);
}

typedef struct {
    int32_t wave, width, octave, cents;
} osc_setting;

/* Renders note 57 with both oscillators set and the given mix. */
static void render_mix(int16_t *out, size_t frames, osc_setting o1,
                       osc_setting o2, int32_t mix)
{
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, o1.wave);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, o1.width);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_OCTAVE, o1.octave);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_FINE_CENTS, o1.cents);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_WAVE, o2.wave);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_PULSE_WIDTH_PCT, o2.width);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, o2.octave);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, o2.cents);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, mix), FM_OK);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, out, (uint32_t)frames);
}

/*
 * Mix 0 is osc1 alone and 1000 osc2 alone, whatever the other is set to
 * (and osc2 sounds exactly as osc1 would with the same settings); 500
 * is the average of the two.
 */
TT_TEST(osc_mix_ends_play_one_oscillator_and_middle_plays_both)
{
#define MIX_FRAMES 8820
    static int16_t a[2 * MIX_FRAMES], b[2 * MIX_FRAMES], m[2 * MIX_FRAMES];
    static int16_t other[2 * MIX_FRAMES];
    const osc_setting sq = { FM_WAVE_SQUARE, 50, 0, 0 };
    const osc_setting saw_up = { FM_WAVE_SAW, 50, 1, 0 };
    const osc_setting pulse_detuned = { FM_WAVE_PULSE, 30, -1, 37 };
    const osc_setting pulse_up_detuned = { FM_WAVE_PULSE, 30, 1, 37 };
    size_t i;
    int max_dev = 0, max_diff = 0;

    render_mix(a, MIX_FRAMES, sq, saw_up, 0);
    TT_CHECK(peak_abs(a, 0, MIX_FRAMES) > 10000);
    render_mix(other, MIX_FRAMES, sq, pulse_detuned, 0);
    TT_CHECK(memcmp(a, other, sizeof a) == 0);

    render_mix(b, MIX_FRAMES, sq, saw_up, 1000);
    render_mix(other, MIX_FRAMES, saw_up, sq, 0);
    TT_CHECK(memcmp(b, other, sizeof b) == 0);
    render_mix(other, MIX_FRAMES, pulse_detuned, saw_up, 1000);
    TT_CHECK(memcmp(b, other, sizeof b) == 0);

    /* every osc2 setting (wave, width, octave, fine) acts like osc1's */
    render_mix(b, MIX_FRAMES, sq, pulse_up_detuned, 1000);
    render_mix(other, MIX_FRAMES, pulse_up_detuned, sq, 0);
    TT_CHECK(memcmp(b, other, sizeof b) == 0);
    render_mix(b, MIX_FRAMES, sq, saw_up, 1000);

    render_mix(m, MIX_FRAMES, sq, saw_up, 500);
    for (i = 0; i < 2 * MIX_FRAMES; i++) {
        int dev = abs(2 * m[i] - (a[i] + b[i]));
        int diff = abs(a[i] - b[i]);
        if (dev > max_dev) max_dev = dev;
        if (diff > max_diff) max_diff = diff;
    }
    TT_CHECK(max_diff > 10000); /* the two oscillators really differ */
    TT_CHECK(max_dev <= 4);     /* 2 LSB of rounding on each side */
#undef MIX_FRAMES
}

/*
 * Noise stays within full scale and does not repeat: over a 0.2 s window
 * no lag up to 4096 samples correlates with the signal by more than 0.1.
 * A pitched wave fails this at its period.
 */
TT_TEST(noise_is_bounded_and_not_periodic)
{
#define NOISE_FRAMES 8820
#define NOISE_LAGS 4096
    static double x[NOISE_FRAMES];
    double energy = 0, worst = 0, mean = 0;
    size_t i, lag;
    osc_init();
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, FM_WAVE_NOISE),
                FM_OK);
    fm_engine_note_on(&eng, 60, 100);
    fm_engine_render(&eng, buf, NOISE_FRAMES);
    TT_CHECK(peak_abs(buf, 0, NOISE_FRAMES) <= 16384 + 64);
    TT_CHECK(peak_abs(buf, 0, NOISE_FRAMES) > 8000);
    for (i = 0; i < NOISE_FRAMES; i++) mean += buf[2 * i];
    mean /= NOISE_FRAMES;
    TT_CHECK(fabs(mean) < 300); /* no DC */
    for (i = 0; i < NOISE_FRAMES; i++) {
        x[i] = buf[2 * i] - mean;
        energy += x[i] * x[i];
    }
    for (lag = 1; lag <= NOISE_LAGS; lag++) {
        double r = 0;
        for (i = 0; i + lag < NOISE_FRAMES; i++) r += x[i] * x[i + lag];
        r = fabs(r) / energy;
        if (r > worst) worst = r;
    }
    printf("    noise: worst autocorrelation %.3f\n", worst);
    TT_CHECK(worst < 0.1);
#undef NOISE_FRAMES
#undef NOISE_LAGS
}

/*
 * Pulse and square aliasing, measured exactly as
 * high_saw_note_has_low_aliasing measures the saw (C7 at 44.1 kHz,
 * 32768-point Blackman-Harris FFT, energy more than 6 bins from a
 * harmonic counts as aliasing) and held to the same ALIAS_LIMIT_DB
 * (-25 dB). Figures are in docs/fixed-point.md. At C7 a 1% pulse would
 * be 0.2 samples wide; the oscillator keeps every pulse at least two
 * samples wide, which is what holds the extremes under the limit.
 */
TT_TEST(high_pulse_and_square_notes_have_low_aliasing)
{
    static const struct { uint8_t wave; int32_t width; } cases[] = {
        { FM_WAVE_SQUARE, 50 }, { FM_WAVE_PULSE, 25 }, { FM_WAVE_PULSE, 10 },
        { FM_WAVE_PULSE, 90 }, { FM_WAVE_PULSE, 1 }, { FM_WAVE_PULSE, 99 }
    };
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        double db;
        osc_init();
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, cases[k].wave);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, cases[k].width);
        fm_engine_note_on(&eng, 96, 100);
        fm_engine_render(&eng, buf, 4800 + 32768);
        db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(96), RATE, 6);
        printf("    aliasing for %s %d%% at note 96: %.1f dB\n",
               cases[k].wave == FM_WAVE_SQUARE ? "square" : "pulse",
               (int)cases[k].width, db);
        TT_CHECK(db < ALIAS_LIMIT_DB);
    }
}

/*
 * Every wave at the extremes of tuning, width and rate stays bounded (no
 * clipping), does not divide by zero, and falls silent after release.
 */
TT_TEST(extreme_osc_settings_stay_bounded_and_release)
{
    static const uint32_t rates[] = { 8000, 44100, 192000 };
    static const uint8_t notes[] = { 0, 127 };
    static const int32_t tunes[][2] = { { -2, -100 }, { 2, 100 } };
    static const int32_t widths[] = { 1, 99 };
    static const int32_t mixes[] = { 0, 500, 1000 };
    size_t r, n, t, w, wv, m, i;
    for (r = 0; r < 3; r++)
    for (n = 0; n < 2; n++)
    for (t = 0; t < 2; t++)
    for (w = 0; w < 2; w++)
    for (wv = 0; wv < FM_WAVE_COUNT; wv++)
    for (m = 0; m < 3; m++) {
        int loud_after = 0;
        TT_CHECK_EQ(fm_engine_init(&eng, rates[r], 1), FM_OK);
        fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, (int32_t)wv);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_WAVE, (int32_t)(FM_WAVE_COUNT - 1 - wv));
        fm_engine_set_param(&eng, FM_PARAM_OSC1_OCTAVE, tunes[t][0]);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_FINE_CENTS, tunes[t][1]);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, tunes[1 - t][0]);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, tunes[1 - t][1]);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, widths[w]);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_PULSE_WIDTH_PCT, widths[1 - w]);
        fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, mixes[m]);
        fm_engine_note_on(&eng, notes[n], 127);
        fm_engine_render(&eng, buf, 2000);
        TT_CHECK(peak_abs(buf, 0, 2000) < 32700);
        fm_engine_note_off(&eng, notes[n]);
        fm_engine_render(&eng, buf, 2000);
        for (i = 2; i < 2 * 2000; i++)
            if (buf[i] != 0) loud_after = 1;
        TT_CHECK(!loud_after);
    }
}

/* Pitch bend moves osc2 as it moves osc1, including osc2's tuning. */
TT_TEST(pitch_bend_moves_both_oscillators)
{
    double want = RATE / (note_hz(57) * pow(2.0, (1200 + 200) / 1200.0));
    osc_init();
    fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, 1);
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 1000); /* osc2 alone */
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 441);
    fm_engine_pitch_bend(&eng, 16383); /* full up: +2 (8191/8192 of it) */
    fm_engine_render(&eng, buf, RATE);
    want *= pow(2.0, (200.0 / 8192) / 1200.0); /* 8191/8192 of 200 cents */
    TT_CHECK_NEAR(period_from_crossings(buf, RATE, 0), want, want * 0.0006);
}

/* Retuning a held, bent note keeps the bend: the new tuning adds to it. */
TT_TEST(retuning_a_bent_note_keeps_the_bend)
{
    double want = RATE / (note_hz(57) * pow(2.0, (100.0 + 30) / 1200.0));
    osc_init();
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_pitch_bend(&eng, 12288); /* +1 of the default 2 semitones */
    fm_engine_render(&eng, buf, 441);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_FINE_CENTS, 30);
    fm_engine_render(&eng, buf, RATE);
    TT_CHECK_NEAR(period_from_crossings(buf, RATE, 0), want, want * 0.0006);
}

/*
 * Two voices playing noise make different noise: summed, they are louder
 * than one by about sqrt(2) (3 dB), where identical streams would double.
 */
TT_TEST(each_voice_makes_its_own_noise)
{
    double one, two;
    double power[2] = { 0, 0 };
    size_t i, k;
    for (k = 0; k < 2; k++) { /* one note, then two started together */
        fm_engine_init(&eng, RATE, 2);
        fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, FM_WAVE_NOISE);
        fm_engine_note_on(&eng, 60, 100);
        if (k) fm_engine_note_on(&eng, 64, 100);
        fm_engine_render(&eng, buf, 8192);
        for (i = 0; i < 8192; i++) power[k] += (double)buf[2 * i] * buf[2 * i];
    }
    one = power[0];
    two = power[1];
    TT_CHECK(one > 0);
    TT_CHECK_NEAR(two / one, 2.0, 0.2); /* power: x2 uncorrelated, x4 same */
}

/*
 * Unison detune goes through the voice's pitch, so osc2 follows it like
 * osc1: with osc2 alone (mix 1000) two voices 100 cents apart sound at
 * -50 and +50 cents, each as loud as a solo note, and nothing is left at
 * the undetuned pitch. (A solo note of osc2 equals one of osc1: same saw.)
 */
TT_TEST(unison_detune_moves_both_oscillators)
{
    double ref = solo_amp(2, 93);
    int sign;
    init_unison(2, 100);
    fm_engine_set_param(&poly, FM_PARAM_OSC_MIX, 1000);
    fm_engine_note_on(&poly, 93, 100);
    fm_engine_render(&poly, pbuf, 2205 + DFT_N);
    for (sign = -1; sign <= 1; sign += 2) {
        double hz = note_hz(93) * pow(2.0, sign * 50 / 1200.0);
        TT_CHECK_NEAR(tone_amp(pbuf + 2 * 2205, DFT_N, 0, hz, PRATE) / ref,
                      1.0, 0.15);
    }
    TT_CHECK(note_amp(pbuf, 2205, 93) / ref < 0.3);
}

/*
 * Unison voices start together, so they need distinct noise: two of them
 * sum to about twice the power of one, where a shared stream would give
 * four times.
 */
TT_TEST(unison_voices_make_distinct_noise)
{
    double power[2] = { 0, 0 };
    size_t i, k;
    for (k = 0; k < 2; k++) {
        init_unison((uint8_t)(k + 1), 0);
        fm_engine_set_param(&poly, FM_PARAM_OSC1_WAVE, FM_WAVE_NOISE);
        fm_engine_note_on(&poly, 60, 100);
        fm_engine_render(&poly, pbuf, 2205 + 8192);
        for (i = 2205; i < 2205 + 8192; i++)
            power[k] += (double)pbuf[2 * i] * pbuf[2 * i];
    }
    TT_CHECK(power[0] > 0);
    /* per-voice gain is 1 / voice_count: two voices sum to 2/4 = 0.5x
     * the power of one if uncorrelated, 4/4 = 1x if identical */
    TT_CHECK_NEAR(power[1] / power[0], 0.5, 0.06);
}

/* ---- Sync and cross-mod (#6) ---- */

/* osc2 alone (mix full), tuned an octave and 37 cents above osc1, a held note. */
static void slave_init(uint32_t rate, uint8_t wave2)
{
    fm_engine_init(&eng, rate, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_WAVE, wave2);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, 1);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 37);
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 1000);
}

/* Largest difference between the output and itself `lag` frames later (channel 0). */
static int max_diff_at_lag(const int16_t *stereo, size_t from, size_t frames, size_t lag)
{
    size_t i;
    int worst = 0;
    for (i = from; i < from + frames; i++) {
        int d = stereo[2 * i] - stereo[2 * (i + lag)];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

/*
 * At 44 kHz, A4 (440 Hz) lasts exactly 100 samples. With sync on, osc2's
 * phase restarts with every cycle of osc1, so osc2 alone, though tuned
 * an octave and 37 cents up, repeats every 100 samples; without sync it
 * doesn't.
 */
TT_TEST(sync_restarts_osc2_on_every_cycle_of_osc1)
{
    int synced, free_running;
    slave_init(44000, FM_WAVE_SAW);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, 8000);
    free_running = max_diff_at_lag(buf, 4000, 2000, 100);
    slave_init(44000, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC_SYNC, 1);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, 8000);
    synced = max_diff_at_lag(buf, 4000, 2000, 100);
    printf("    sync: largest change over one osc1 cycle %d, free-running %d\n", synced, free_running);
    TT_CHECK(free_running > 5000);
    TT_CHECK(synced < 200);
}

/*
 * Synced saw, square and pulse on osc2, measured like the other waves
 * (32768-point Blackman-Harris FFT, 6-bin guard) against osc1's pitch,
 * the synced sound's fundamental, and held to ALIAS_LIMIT_DB. Note 84
 * (C6) with osc2 11 semitones up puts osc2 near 2 kHz, reset every 0.96 ms
 * most of the way through its cycle: a near full-size jump each time.
 */
TT_TEST(synced_osc2_has_low_aliasing)
{
    static const struct { uint8_t wave; int32_t width; } cases[] = {
        { FM_WAVE_SAW, 50 }, { FM_WAVE_SQUARE, 50 }, { FM_WAVE_PULSE, 25 }
    };
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        double db;
        slave_init(RATE, cases[k].wave);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, -100); /* osc2 0.89 into its cycle at each reset */
        fm_engine_set_param(&eng, FM_PARAM_OSC2_PULSE_WIDTH_PCT, cases[k].width);
        fm_engine_set_param(&eng, FM_PARAM_OSC_SYNC, 1);
        fm_engine_note_on(&eng, 84, 100);
        fm_engine_render(&eng, buf, 4800 + 32768);
        db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(84), RATE, 6);
        printf("    aliasing for synced wave %d at note 84: %.1f dB\n", (int)cases[k].wave, db);
        TT_CHECK(db < ALIAS_LIMIT_DB);
    }
}

/*
 * osc2 an octave less 2 cents up (a ratio just under 2) meets each
 * restart about 0.998 into its cycle, within one increment of its own wrap:
 * the polyBLEP it would put on that wrap must not add to the restart's.
 */
TT_TEST(synced_osc2_just_short_of_its_wrap_has_low_aliasing)
{
    double db;
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, -2);
    fm_engine_set_param(&eng, FM_PARAM_OSC_SYNC, 1);
    fm_engine_note_on(&eng, 84, 100);
    fm_engine_render(&eng, buf, 4800 + 32768);
    db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(84), RATE, 6);
    printf("    aliasing for synced saw an octave less 2 cents up at note 84: %.1f dB\n", db);
    TT_CHECK(db < ALIAS_LIMIT_DB);
}

/*
 * osc2 an octave and 2 cents up (a ratio just over 2) wraps on its own just
 * before each restart, inside the same sample: that wrap keeps its polyBLEP
 * on both sides.
 */
TT_TEST(synced_osc2_just_past_its_wrap_has_low_aliasing)
{
    double db;
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 2);
    fm_engine_set_param(&eng, FM_PARAM_OSC_SYNC, 1);
    fm_engine_note_on(&eng, 84, 100);
    fm_engine_render(&eng, buf, 4800 + 32768);
    db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(84), RATE, 6);
    printf("    aliasing for synced saw an octave and 2 cents up at note 84: %.1f dB\n", db);
    TT_CHECK(db < ALIAS_LIMIT_DB);
}

/* Cross-mod set to 0 sounds exactly like cross-mod never touched. */
TT_TEST(xmod_zero_matches_no_xmod)
{
    static int16_t plain[2 * 4096];
    size_t i;
    int same = 1;
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, plain, 4096);
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC_XMOD, 700);
    fm_engine_set_param(&eng, FM_PARAM_OSC_XMOD, 0);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4096);
    for (i = 0; i < 2 * 4096; i++)
        if (buf[i] != plain[i]) same = 0;
    TT_CHECK(same);
}

/*
 * osc1 at 220 Hz modulating osc2's frequency (osc2 an octave up, 440 Hz,
 * heard alone) adds sidebands at 440 +- 220 Hz: 660 Hz, which a 440 Hz
 * saw doesn't have.
 */
TT_TEST(xmod_adds_sidebands)
{
    double without, with;
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 0);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4410 + 16384);
    without = tone_amp(buf + 2 * 4410, 16384, 0, 660.0, RATE);
    slave_init(RATE, FM_WAVE_SAW);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_FINE_CENTS, 0);
    fm_engine_set_param(&eng, FM_PARAM_OSC_XMOD, 300);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4410 + 16384);
    with = tone_amp(buf + 2 * 4410, 16384, 0, 660.0, RATE);
    printf("    660 Hz sideband: %.1f without cross-mod, %.1f with\n", without, with);
    TT_CHECK(without < 20.0);
    TT_CHECK(with > 500.0);
}

/* Full cross-mod and sync on every wave, at the extremes of pitch and rate: no clipping, silent after release. */
TT_TEST(max_xmod_and_sync_stay_in_range)
{
    static const uint32_t rates[] = { 8000, 44100, 192000 };
    static const uint8_t notes[] = { 0, 60, 127 };
    size_t r, n, w1, w2, sy, i;
    for (r = 0; r < 3; r++)
    for (n = 0; n < 3; n++)
    for (w1 = 0; w1 < FM_WAVE_COUNT; w1++)
    for (w2 = 0; w2 < FM_WAVE_COUNT; w2++)
    for (sy = 0; sy < 2; sy++) {
        int loud_after = 0;
        TT_CHECK_EQ(fm_engine_init(&eng, rates[r], 1), FM_OK);
        fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
        fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, (int32_t)w1);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_WAVE, (int32_t)w2);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_OCTAVE, 2);
        fm_engine_set_param(&eng, FM_PARAM_OSC2_PULSE_WIDTH_PCT, 1);
        fm_engine_set_param(&eng, FM_PARAM_OSC_MIX, 1000);
        fm_engine_set_param(&eng, FM_PARAM_OSC_SYNC, (int32_t)sy);
        fm_engine_set_param(&eng, FM_PARAM_OSC_XMOD, 1000);
        fm_engine_note_on(&eng, notes[n], 127);
        fm_engine_render(&eng, buf, 2000);
        if (peak_abs(buf, 0, 2000) > 16384 + 64)
            printf("    over at rate %u note %u waves %u/%u sync %u: %d\n", (unsigned)rates[r],
                   (unsigned)notes[n], (unsigned)w1, (unsigned)w2, (unsigned)sy, peak_abs(buf, 0, 2000));
        TT_CHECK(peak_abs(buf, 0, 2000) <= 16384 + 64); /* one voice at full level, like the noise test */
        fm_engine_note_off(&eng, notes[n]);
        fm_engine_render(&eng, buf, 2000);
        for (i = 2; i < 2 * 2000; i++)
            if (buf[i] != 0) loud_after = 1;
        TT_CHECK(!loud_after);
    }
}

static void run_osc_tests(void)
{
    TT_RUN(each_wave_renders_at_note_pitch);
    TT_RUN(pulse_width_sets_duty_cycle);
    TT_RUN(square_ignores_pulse_width);
    TT_RUN(pulse_width_outside_its_range_is_clamped);
    TT_RUN(pulse_peaks_at_the_saw_level_at_every_width);
    TT_RUN(octave_and_fine_tune_shift_pitch);
    TT_RUN(osc2_coarse_tune_shifts_by_semitones);
    TT_RUN(osc2_coarse_zero_matches_no_coarse);
    TT_RUN(osc2_coarse_change_retunes_a_held_note_without_a_restart);
    TT_RUN(tuned_increment_matches_exact_pitch);
    TT_RUN(octave_shifts_keep_their_ratio_at_the_extremes);
    TT_RUN(retuning_a_held_note_takes_effect);
    TT_RUN(pitch_bend_moves_both_oscillators);
    TT_RUN(retuning_a_bent_note_keeps_the_bend);
    TT_RUN(each_voice_makes_its_own_noise);
    TT_RUN(unison_detune_moves_both_oscillators);
    TT_RUN(unison_voices_make_distinct_noise);
    TT_RUN(osc_mix_ends_play_one_oscillator_and_middle_plays_both);
    TT_RUN(noise_is_bounded_and_not_periodic);
    TT_RUN(high_pulse_and_square_notes_have_low_aliasing);
    TT_RUN(extreme_osc_settings_stay_bounded_and_release);
    TT_RUN(sync_restarts_osc2_on_every_cycle_of_osc1);
    TT_RUN(synced_osc2_has_low_aliasing);
    TT_RUN(synced_osc2_just_short_of_its_wrap_has_low_aliasing);
    TT_RUN(synced_osc2_just_past_its_wrap_has_low_aliasing);
    TT_RUN(xmod_zero_matches_no_xmod);
    TT_RUN(xmod_adds_sidebands);
    TT_RUN(max_xmod_and_sync_stay_in_range);
}
