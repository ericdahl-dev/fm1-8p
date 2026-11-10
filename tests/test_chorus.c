/* Chorus tests, through the chorus module API and the engine API. */
#include "../engine/chorus.h"
#include "../engine/fm1_8p.h"

#define CH_RATE 44100u

static fm_engine ch_eng;
static int16_t ch_buf[CH_RATE * 2 * 4];

/* FNV-1a over a buffer of samples. */
static uint32_t ch_hash(const int16_t *s, size_t count)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < count * sizeof *s; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * Two overlapping notes and a release, 20000 frames. The golden hash was
 * taken from the engine before the chorus existed (main at 47680c8, with polyphony).
 */
#define CH_GOLDEN_HASH 0x8E24F4ADu

static uint32_t ch_render_golden_phrase(int set_mode, int32_t mode)
{
    memset(ch_buf, 0, 2 * 20000 * sizeof ch_buf[0]);
    fm_engine_init(&ch_eng, CH_RATE, 8);
    if (set_mode) fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, mode);
    fm_engine_set_param(&ch_eng, FM_PARAM_AMP_ATTACK_MS, 3);
    fm_engine_set_param(&ch_eng, FM_PARAM_AMP_RELEASE_MS, 40);
    fm_engine_note_on(&ch_eng, 57, 100);
    fm_engine_render(&ch_eng, ch_buf, 8000);
    fm_engine_note_on(&ch_eng, 64, 90);
    fm_engine_render(&ch_eng, ch_buf + 2 * 8000, 6000);
    fm_engine_note_off(&ch_eng, 64);
    fm_engine_render(&ch_eng, ch_buf + 2 * 14000, 6000);
    return ch_hash(ch_buf, 2 * 20000);
}

TT_TEST(chorus_off_engine_output_is_bit_exact_with_pre_chorus_engine)
{
    TT_CHECK_EQ(ch_render_golden_phrase(0, 0), CH_GOLDEN_HASH);
    TT_CHECK_EQ(fm_engine_get_param(&ch_eng, FM_PARAM_CHORUS_MODE), 0);
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_CHORUS_MODE)->def, 0);
    TT_CHECK_EQ(ch_render_golden_phrase(1, FM_CHORUS_OFF), CH_GOLDEN_HASH);
    /* back to off after a mode was on */
    fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, 2);
    fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, 0);
    TT_CHECK_EQ(fm_engine_get_param(&ch_eng, FM_PARAM_CHORUS_MODE), 0);
}

/* RMS of left - right relative to RMS of left, over frames [from, to). */
static double ch_side_ratio(const int16_t *stereo, size_t from, size_t to)
{
    double side = 0, left = 0;
    size_t i;
    for (i = from; i < to; i++) {
        double l = stereo[2 * i], r = stereo[2 * i + 1];
        side += (l - r) * (l - r);
        left += l * l;
    }
    return left > 0 ? sqrt(side / left) : 0;
}

TT_TEST(chorus_on_makes_left_and_right_differ_for_a_mono_voice)
{
    int32_t mode;
    TT_CHECK_EQ(FM_CHORUS_MODE_COUNT, 4);
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_CHORUS_MODE)->max,
                FM_CHORUS_MODE_COUNT - 1);
    for (mode = 1; mode < FM_CHORUS_MODE_COUNT; mode++) {
        double ratio;
        TT_CHECK_EQ(fm_engine_init(&ch_eng, CH_RATE, 1), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, mode),
                    FM_OK);
        fm_engine_note_on(&ch_eng, 57, 100);
        fm_engine_render(&ch_eng, ch_buf, CH_RATE * 2);
        ratio = ch_side_ratio(ch_buf, CH_RATE / 10, CH_RATE * 2);
        printf("    chorus mode %d: side/left RMS %.3f\n", (int)mode, ratio);
        TT_CHECK(ratio > 0.1);
    }
}

/*
 * Measures a mode's delay sweep through the module API. The input is a
 * slow ramp (a long sawtooth), so each wet tap is the ramp value from
 * `delay` samples ago, and linear interpolation of a ramp is exact:
 * delay = now - position of the tapped value on the ramp. Only rounding
 * remains (the halved wet signal and the Q0.15 fraction), well under
 * 0.01 sample; without interpolation the error would be up to 0.5 sample.
 *
 * With `ideal` (rate Hz, center ms, sweep ms) it also reports the largest
 * difference between the left delay and that ideal triangle, which starts
 * at center - sweep.
 */
#define CH_RAMP_PERIOD 4096
#define CH_RAMP_STEP 8192 /* Q8.24 per sample: the ramp spans +-1.0 */

typedef struct {
    double rate_hz, center_ms, depth_ms, mirror_err_ms;
    double track_err_samples;
} ch_sweep;

/* Ideal triangle, -1 at phase 0, +1 at half a cycle. */
static double ch_ideal_triangle(double cycles)
{
    double p = cycles - floor(cycles);
    return p < 0.5 ? 4 * p - 1 : 3 - 4 * p;
}

static ch_sweep ch_measure_sweep(uint8_t mode, uint32_t rate,
                                 const double *ideal)
{
    double track = 0;
    static fm_chorus ch;
    const uint32_t frames = rate * 8;
    double lo = 1e9, hi = -1e9, sum = 0, mirror = 0, mean;
    double first = -1, last = -1, prev = 0;
    int crossings = 0, have_prev = 0, pass;
    size_t valid = 0;
    uint32_t n;
    ch_sweep out;
    fm_chorus_init(&ch, rate);
    fm_chorus_set_mode(&ch, mode);
    /* pass 0 finds the mean delay, pass 1 counts LFO cycles around it */
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            fm_chorus_init(&ch, rate);
            fm_chorus_set_mode(&ch, mode);
        }
        mean = valid ? sum / (double)valid : 0;
        for (n = 0; n < frames; n++) {
            int32_t pos = (int32_t)(n % CH_RAMP_PERIOD);
            int32_t x = (pos - CH_RAMP_PERIOD / 2) * CH_RAMP_STEP;
            int32_t l, r;
            double wet_l, wet_r, d_l, d_r;
            fm_chorus_process(&ch, x, &l, &r);
            /* skip the ramp's wrap until the taps are past it */
            if (pos < FM_CHORUS_DELAY_LEN + 8 || n < CH_RAMP_PERIOD) continue;
            wet_l = 2.0 * l - x;
            wet_r = 2.0 * r - x;
            d_l = pos - (wet_l / CH_RAMP_STEP + CH_RAMP_PERIOD / 2);
            d_r = pos - (wet_r / CH_RAMP_STEP + CH_RAMP_PERIOD / 2);
            if (pass == 0) {
                if (d_l < lo) lo = d_l;
                if (d_l > hi) hi = d_l;
                sum += d_l;
                valid++;
            } else {
                double err = fabs((d_l + d_r) / 2 - (lo + hi) / 2);
                if (err > mirror) mirror = err;
                if (ideal) {
                    double want = (ideal[1] + ideal[2] *
                        ch_ideal_triangle(ideal[0] * n / rate)) *
                        rate / 1000.0;
                    if (fabs(d_l - want) > track) track = fabs(d_l - want);
                }
                if (have_prev && prev < mean && d_l >= mean) {
                    if (first < 0) first = n;
                    last = n;
                    crossings++;
                }
                prev = d_l;
                have_prev = 1;
            }
        }
    }
    out.center_ms = (lo + hi) / 2 * 1000.0 / rate;
    out.depth_ms = (hi - lo) / 2 * 1000.0 / rate;
    out.rate_hz = crossings > 1
        ? (crossings - 1) * (double)rate / (last - first) : 0;
    out.mirror_err_ms = mirror * 1000.0 / rate;
    out.track_err_samples = track;
    return out;
}

TT_TEST(chorus_modes_differ_in_rate_and_depth)
{
    /* the mode table documented in engine/chorus.h */
    static const double spec[FM_CHORUS_MODE_COUNT][3] = {
        { 0, 0, 0 }, { 0.5, 5.0, 1.5 }, { 0.8, 5.0, 2.5 }, { 6.0, 3.0, 0.3 },
    };
    ch_sweep got[FM_CHORUS_MODE_COUNT];
    int a, b;
    for (a = 1; a < FM_CHORUS_MODE_COUNT; a++) {
        got[a] = ch_measure_sweep((uint8_t)a, CH_RATE, spec[a]);
        printf("    chorus mode %d: %.3f Hz, center %.3f ms, sweep +-%.3f ms,"
               " off ideal by %.4f samples\n", a, got[a].rate_hz,
               got[a].center_ms, got[a].depth_ms, got[a].track_err_samples);
        TT_CHECK_NEAR(got[a].rate_hz, spec[a][0], spec[a][0] * 0.02);
        TT_CHECK_NEAR(got[a].center_ms, spec[a][1], 0.05);
        TT_CHECK_NEAR(got[a].depth_ms, spec[a][2], 0.05);
        /* right sweeps opposite to left around the same center */
        TT_CHECK(got[a].mirror_err_ms < 0.005);
        /*
         * Every sample follows the ideal triangle, so interpolation works
         * (a whole-sample tap would be off by up to 0.5). The LFO increment
         * is rounded down, which drifts by about 0.02 sample over 8 s.
         */
        TT_CHECK(got[a].track_err_samples < 0.05);
    }
    for (a = 1; a < FM_CHORUS_MODE_COUNT; a++)
        for (b = a + 1; b < FM_CHORUS_MODE_COUNT; b++) {
            double rate = got[a].rate_hz / got[b].rate_hz;
            double depth = got[a].depth_ms / got[b].depth_ms;
            if (rate < 1) rate = 1 / rate;
            if (depth < 1) depth = 1 / depth;
            TT_CHECK(rate > 1.3 || depth > 1.3);
        }
}

/*
 * Worst case at the module: the sum of FM_MAX_VOICES full-scale voices,
 * +-8.0 in Q8.24, as a square wave (every period from 2 to 600 samples,
 * so some line up with the taps), as full-scale noise, and at the int32
 * extremes. An output must never exceed half the input peak plus 1.0
 * (half the delay line's +-2.0 clamp), so the chorus never adds level
 * beyond the dry input and nothing overflows.
 */
#define CH_FULL_SCALE_8_VOICES (FM_MAX_VOICES * (1 << 24))

static int ch_bounded(fm_chorus *ch, int32_t in, int32_t peak_in)
{
    int32_t l, r;
    int64_t limit = (int64_t)peak_in / 2 + (1 << 24) + 1;
    fm_chorus_process(ch, in, &l, &r);
    return llabs((long long)l) <= limit && llabs((long long)r) <= limit;
}

TT_TEST(chorus_output_stays_bounded_for_eight_full_scale_voices)
{
    static fm_chorus ch;
    const int32_t fs = CH_FULL_SCALE_8_VOICES;
    uint32_t seed = 12345, n;
    int mode, period, ok = 1;
    for (mode = 1; mode < FM_CHORUS_MODE_COUNT; mode++) {
        fm_chorus_init(&ch, CH_RATE);
        fm_chorus_set_mode(&ch, (uint8_t)mode);
        for (period = 2; period <= 600; period += 7)
            for (n = 0; n < 4u * (uint32_t)period + 1500u; n++)
                ok &= ch_bounded(&ch, (int32_t)(n % (uint32_t)period) <
                                 period / 2 ? fs : -fs, fs);
        for (n = 0; n < CH_RATE * 2; n++) {
            int32_t x;
            seed = seed * 1664525u + 1013904223u;
            x = (int32_t)(seed >> 4) - (1 << 27); /* -8.0 .. +8.0 */
            ok &= ch_bounded(&ch, x, fs);
        }
        for (n = 0; n < 4000; n++)
            ok &= ch_bounded(&ch, (n / 300) % 2 ? INT32_MAX : INT32_MIN,
                             INT32_MAX);
    }
    TT_CHECK(ok);
}

/*
 * Through the engine with every voice sounding: an eight-note chord
 * (octaves and fifths of A, all starting together so their peaks line
 * up) at full level, in every mode. The chorus must not raise the peak
 * over chorus off (one LSB allowed for rounding). The module-level test
 * above covers inputs far beyond what the mix can reach.
 */
TT_TEST(chorus_engine_peak_does_not_rise_with_a_full_chord)
{
    static const uint8_t chord[FM_MAX_VOICES] = { 33, 45, 52, 57, 64, 69,
                                                  76, 81 };
    int mode, peak_off = 0;
    size_t k;
    for (mode = 0; mode < FM_CHORUS_MODE_COUNT; mode++) {
        int peak;
        TT_CHECK_EQ(fm_engine_init(&ch_eng, CH_RATE, FM_MAX_VOICES), FM_OK);
        fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, mode);
        fm_engine_set_param(&ch_eng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&ch_eng, FM_PARAM_AMP_SUSTAIN, 1000);
        for (k = 0; k < FM_MAX_VOICES; k++)
            fm_engine_note_on(&ch_eng, chord[k], 127);
        TT_CHECK_EQ(fm_engine_active_voices(&ch_eng), FM_MAX_VOICES);
        fm_engine_render(&ch_eng, ch_buf, CH_RATE * 2);
        peak = peak_abs(ch_buf, 0, CH_RATE * 2);
        if (mode == 0) peak_off = peak;
        printf("    chorus mode %d, %d voices: peak %d\n", mode,
               FM_MAX_VOICES, peak);
        TT_CHECK(peak > 6000);
        TT_CHECK(peak <= peak_off + 1);
    }
}

/*
 * Every mode at the edge sample rates: no division by zero (the desktop
 * traps on one), the taps stay inside the delay line (the deep mode
 * needs more than the line holds at 192 kHz, so it is scaled to fit),
 * and the stereo spread survives. An out-of-range mode means off.
 */
TT_TEST(chorus_handles_extreme_rates_and_unknown_modes)
{
    static const uint32_t rates[] = { 1, 8000, 96000, 192000 };
    static fm_chorus ch;
    size_t r;
    int mode;
    int32_t l, r_out;
    ch_sweep sw;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (mode = 1; mode < FM_CHORUS_MODE_COUNT; mode++) {
            TT_CHECK_EQ(fm_engine_init(&ch_eng, rates[r], 1), FM_OK);
            fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, mode);
            fm_engine_set_param(&ch_eng, FM_PARAM_AMP_ATTACK_MS, 0);
            fm_engine_note_on(&ch_eng, 45, 100);
            fm_engine_render(&ch_eng, ch_buf, CH_RATE);
            TT_CHECK(peak_abs(ch_buf, 0, CH_RATE) <= 16384 + 64);
            if (rates[r] >= 8000)
                TT_CHECK(ch_side_ratio(ch_buf, CH_RATE / 2, CH_RATE) > 0.05);
        }
    }
    /* deep at 192 kHz: scaled to the line, same rate and shape */
    sw = ch_measure_sweep(FM_CHORUS_DEEP, 192000, NULL);
    TT_CHECK((sw.center_ms + sw.depth_ms) * 192.0 <= FM_CHORUS_DELAY_LEN - 2);
    TT_CHECK((sw.center_ms + sw.depth_ms) * 192.0 > FM_CHORUS_DELAY_LEN - 4);
    TT_CHECK_NEAR(sw.center_ms / sw.depth_ms, 2.0, 0.01);
    TT_CHECK_NEAR(sw.rate_hz, 0.8, 0.016);
    fm_chorus_init(&ch, CH_RATE);
    fm_chorus_set_mode(&ch, FM_CHORUS_MODE_COUNT);
    fm_chorus_process(&ch, 12345, &l, &r_out);
    TT_CHECK(l == 12345 && r_out == 12345);
    fm_chorus_set_mode(&ch, 255);
    fm_chorus_process(&ch, -777, &l, &r_out);
    TT_CHECK(l == -777 && r_out == -777);
    /* a chorus with no sample rate stays off rather than divide by 0 */
    fm_chorus_init(&ch, 0);
    fm_chorus_set_mode(&ch, FM_CHORUS_DEEP);
    fm_chorus_process(&ch, 999, &l, &r_out);
    TT_CHECK(l == 999 && r_out == 999);
}

/*
 * The chorus lives in fm_engine, which may sit on a task stack: its
 * delay line is fixed (2048 bytes of int16 samples, as documented in
 * engine/chorus.h) and the whole struct stays within 2.1 KiB.
 */
TT_TEST(chorus_memory_is_fixed_and_small)
{
    static fm_chorus ch;
    TT_CHECK_EQ(sizeof ch.line, 2048);
    TT_CHECK_EQ(FM_CHORUS_DELAY_LEN & (FM_CHORUS_DELAY_LEN - 1), 0);
    TT_CHECK(sizeof(fm_chorus) <= 2048 + 64);
}

/*
 * The delay line holds +-2.0 at most: a hot input (+-4.0, two full-scale
 * voices at once) comes back from the taps clamped to about +-2.0 with
 * its sign kept, not wrapped around.
 */
TT_TEST(chorus_delay_line_clamps_hot_input_to_two)
{
    static fm_chorus ch;
    static const int32_t inputs[] = { 4 << 24, -(4 << 24) };
    size_t k;
    uint32_t n;
    for (k = 0; k < 2; k++) {
        const int32_t x = inputs[k];
        int32_t l = 0, r = 0;
        double wet_l, wet_r, want = x > 0 ? 2.0 : -2.0;
        fm_chorus_init(&ch, CH_RATE);
        fm_chorus_set_mode(&ch, FM_CHORUS_DEEP);
        for (n = 0; n < FM_CHORUS_DELAY_LEN + 100; n++)
            fm_chorus_process(&ch, x, &l, &r);
        /* each output is half the input plus half the tap */
        wet_l = (2.0 * l - x) / (1 << 24);
        wet_r = (2.0 * r - x) / (1 << 24);
        printf("    chorus input %+.1f: wet %+.4f / %+.4f\n",
               (double)x / (1 << 24), wet_l, wet_r);
        TT_CHECK_NEAR(wet_l, want, 0.001);
        TT_CHECK_NEAR(wet_r, want, 0.001);
    }
}

/*
 * The chorus stops writing its delay line while off, so turning it back
 * on must start from silence, not replay what was there before.
 */
TT_TEST(chorus_turned_on_again_starts_from_a_silent_line)
{
    static fm_chorus ch;
    uint32_t n;
    int32_t l, r;
    int silent = 1;
    fm_chorus_init(&ch, CH_RATE);
    fm_chorus_set_mode(&ch, FM_CHORUS_SLOW);
    for (n = 0; n < 3000; n++)
        fm_chorus_process(&ch, (n / 50) % 2 ? (1 << 24) : -(1 << 24), &l, &r);
    fm_chorus_set_mode(&ch, FM_CHORUS_OFF);
    for (n = 0; n < 500; n++) fm_chorus_process(&ch, 0, &l, &r);
    fm_chorus_set_mode(&ch, FM_CHORUS_DEEP);
    for (n = 0; n < FM_CHORUS_DELAY_LEN; n++) {
        fm_chorus_process(&ch, 0, &l, &r);
        if (l != 0 || r != 0) silent = 0;
    }
    TT_CHECK(silent);
}

static void run_chorus_tests(void)
{
    TT_RUN(chorus_off_engine_output_is_bit_exact_with_pre_chorus_engine);
    TT_RUN(chorus_on_makes_left_and_right_differ_for_a_mono_voice);
    TT_RUN(chorus_modes_differ_in_rate_and_depth);
    TT_RUN(chorus_output_stays_bounded_for_eight_full_scale_voices);
    TT_RUN(chorus_engine_peak_does_not_rise_with_a_full_chord);
    TT_RUN(chorus_handles_extreme_rates_and_unknown_modes);
    TT_RUN(chorus_memory_is_fixed_and_small);
    TT_RUN(chorus_delay_line_clamps_hot_input_to_two);
    TT_RUN(chorus_turned_on_again_starts_from_a_silent_line);
}
