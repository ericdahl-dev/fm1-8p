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
 * delay = now - position of the tapped value on the ramp.
 */
#define CH_RAMP_PERIOD 4096
#define CH_RAMP_STEP 8192 /* Q8.24 per sample: the ramp spans +-1.0 */

typedef struct {
    double rate_hz, center_ms, depth_ms, mirror_err_ms;
} ch_sweep;

static ch_sweep ch_measure_sweep(uint8_t mode)
{
    static fm_chorus ch;
    const uint32_t frames = CH_RATE * 8;
    double lo = 1e9, hi = -1e9, sum = 0, mirror = 0, mean;
    double first = -1, last = -1, prev = 0;
    int crossings = 0, have_prev = 0, pass;
    size_t valid = 0;
    uint32_t n;
    ch_sweep out;
    fm_chorus_init(&ch, CH_RATE);
    fm_chorus_set_mode(&ch, mode);
    /* pass 0 finds the mean delay, pass 1 counts LFO cycles around it */
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            fm_chorus_init(&ch, CH_RATE);
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
            if (pos < 512 || n < CH_RAMP_PERIOD) continue;
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
                double err = fabs((d_l + d_r) / 2 - mean);
                if (err > mirror) mirror = err;
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
    out.center_ms = (lo + hi) / 2 * 1000.0 / CH_RATE;
    out.depth_ms = (hi - lo) / 2 * 1000.0 / CH_RATE;
    out.rate_hz = crossings > 1
        ? (crossings - 1) * (double)CH_RATE / (last - first) : 0;
    out.mirror_err_ms = mirror * 1000.0 / CH_RATE;
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
        got[a] = ch_measure_sweep((uint8_t)a);
        printf("    chorus mode %d: %.3f Hz, center %.3f ms, sweep +-%.3f ms\n",
               a, got[a].rate_hz, got[a].center_ms, got[a].depth_ms);
        TT_CHECK_NEAR(got[a].rate_hz, spec[a][0], spec[a][0] * 0.02);
        TT_CHECK_NEAR(got[a].center_ms, spec[a][1], 0.05);
        TT_CHECK_NEAR(got[a].depth_ms, spec[a][2], 0.05);
        /* right sweeps opposite to left around the same center */
        TT_CHECK(got[a].mirror_err_ms < 0.05);
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

static void run_chorus_tests(void)
{
    TT_RUN(chorus_off_engine_output_is_bit_exact_with_pre_chorus_engine);
    TT_RUN(chorus_on_makes_left_and_right_differ_for_a_mono_voice);
    TT_RUN(chorus_modes_differ_in_rate_and_depth);
}
