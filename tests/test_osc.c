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

static void run_osc_tests(void)
{
    TT_RUN(each_wave_renders_at_note_pitch);
    TT_RUN(pulse_width_sets_duty_cycle);
    TT_RUN(octave_and_fine_tune_shift_pitch);
    TT_RUN(retuning_a_held_note_takes_effect);
    TT_RUN(osc_mix_ends_play_one_oscillator_and_middle_plays_both);
    TT_RUN(noise_is_bounded_and_not_periodic);
}
