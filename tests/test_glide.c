/*
 * Settings glide as PG-8X 2.0.13's do (#146): a change to a level, the
 * filter or an LFO depth moves there as a one-pole of about 16 ms
 * (measured mid-note through DawDreamer automation), and a patch set
 * right after init glides from PG-8X's power-on values (DCO-1 at full
 * mix, the filter open, no resonance), as its renders start. Through the
 * public interface; setting_glide is on for every JX-8P tone.
 */
#include "../engine/fm1_8p.h"

#define GL_RATE 48000u

static fm_engine gl_eng;
static int16_t gl_buf[GL_RATE * 2]; /* 1 s of stereo */

/* Peak over 1 ms from `ms` (a C6 period is under 1 ms). */
static int gl_peak_at(double ms)
{
    size_t a = (size_t)(ms * GL_RATE / 1000);
    return peak_abs(gl_buf, a, a + GL_RATE / 1000);
}

/* PG-8X with DCO-1's mix at 0, a note at the start of the render: DCO-1
 * still sounds, from its power-on level, and fades over the glide: from
 * 2 to 50 ms, tau 16 ms is 26.1 dB. Within 2 dB. */
TT_TEST(a_patch_set_after_init_glides_from_pg8xs_power_on_values)
{
    int first, later;
    TT_CHECK_EQ(fm_engine_init(&gl_eng, GL_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&gl_eng, FM_PARAM_SETTING_GLIDE, 1), FM_OK);
    fm_engine_set_param(&gl_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_ATTACK_MS, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&gl_eng, 84, 127);
    fm_engine_render(&gl_eng, gl_buf, GL_RATE / 10);
    first = gl_peak_at(2);
    later = gl_peak_at(50);
    printf("    DCO-1 off after init: %d counts at 2 ms, %d at 50 ms\n", first, later);
    TT_CHECK(first > 1000);
    TT_CHECK(later > 0 && fabs(20 * log10((double)first / later) - 26.1) < 2.0);
}

static fm_engine gl_off;
static int16_t gl_buf_off[GL_RATE * 2];

/* RMS difference of two stereo buffers over frames [a, b), and RMS of
 * the second. */
static double gl_rms(const int16_t *x, size_t a, size_t b)
{
    double s = 0;
    size_t i;
    for (i = 2 * a; i < 2 * b; i++) s += (double)x[i] * x[i];
    return sqrt(s / (double)(2 * (b - a)));
}

static double gl_rms_diff(const int16_t *x, const int16_t *y, size_t a, size_t b)
{
    double s = 0;
    size_t i;
    for (i = 2 * a; i < 2 * b; i++) s += ((double)x[i] - y[i]) * ((double)x[i] - y[i]);
    return sqrt(s / (double)(2 * (b - a)));
}

/* The widest pitch swing over frames [a, b), cents: each period from
 * the left channel's rising zero crossings (interpolated) against the
 * shortest. */
static double gl_swing_cents(const int16_t *x, size_t a, size_t b)
{
    double last = -1, lo = 1e9, hi = 0, t;
    size_t i;
    for (i = a + 1; i < b; i++) {
        int s0 = x[2 * (i - 1)], s1 = x[2 * i];
        if (s0 < 0 && s1 >= 0) {
            t = (double)(i - 1) + (double)-s0 / (double)(s1 - s0);
            if (last >= 0) {
                if (t - last < lo) lo = t - last;
                if (t - last > hi) hi = t - last;
            }
            last = t;
        }
    }
    return hi > 0 ? 1200 * log2(hi / lo) : 0;
}

typedef struct {
    const char *name;
    int32_t setup[4][2]; /* id, value; id 0 ends (0 is amp attack, never set here) */
    uint16_t id;
    int32_t from, to;
    int pitch; /* a depth of vibrato: the phase it moved stays moved, so compare the swing */
} gl_case;

/*
 * Each gliding setting, stepped mid-note: right after the step it plays
 * well away from where the same engine without the glide jumps to, and
 * 150 ms on (over 9 tau) it plays the same, within 1 % of its RMS.
 */
TT_TEST(each_setting_glides_to_a_mid_note_change)
{
    static const gl_case cases[] = {
        { "osc2 level", { { FM_PARAM_OSC2_LEVEL, 0 } }, FM_PARAM_OSC2_LEVEL, 0, 1000, 0 },
        { "osc1 level", { { 0 } }, FM_PARAM_OSC1_LEVEL, 1000, 300, 0 },
        { "cutoff", { { 0 } }, FM_PARAM_FILTER_CUTOFF_CENTS, 14728, 8000, 0 },
        { "resonance", { { FM_PARAM_FILTER_CUTOFF_CENTS, 9000 } }, FM_PARAM_FILTER_RESONANCE, 0, 700, 0 },
        { "key track", { { FM_PARAM_FILTER_CUTOFF_CENTS, 7000 } }, FM_PARAM_FILTER_KEY_TRACK, 0, 1000, 0 },
        { "env amount", { { FM_PARAM_FILTER_CUTOFF_CENTS, 6000 }, { FM_PARAM_FILTER_ENV_ATTACK_MS, 1 },
                          { FM_PARAM_FILTER_ENV_SUSTAIN, 1000 } }, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0, 3600, 0 },
        { "osc1 LFO depth", { { FM_PARAM_LFO_RATE_CENTIHZ, 600 } }, FM_PARAM_LFO_PITCH_CENTS, 0, 100, 1 },
        { "osc2 LFO depth", { { FM_PARAM_LFO_RATE_CENTIHZ, 600 }, { FM_PARAM_OSC2_LEVEL, 1000 },
                              { FM_PARAM_OSC1_LEVEL, 0 } }, FM_PARAM_LFO_PITCH_OSC2_CENTS, 0, 100, 1 },
    };
    size_t c;
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const gl_case *k = &cases[c];
        int n;
        double early, late, rms;
        fm_engine *e[2] = { &gl_eng, &gl_off };
        for (n = 0; n < 2; n++) {
            int j;
            TT_CHECK_EQ(fm_engine_init(e[n], GL_RATE, 6), FM_OK);
            fm_engine_set_param(e[n], FM_PARAM_AMP_ATTACK_MS, 1);
            fm_engine_set_param(e[n], FM_PARAM_AMP_SUSTAIN, 1000);
            for (j = 0; j < 4 && k->setup[j][0]; j++)
                fm_engine_set_param(e[n], (uint16_t)k->setup[j][0], k->setup[j][1]);
            fm_engine_set_param(e[n], k->id, k->from);
            fm_engine_note_on(e[n], 72, 127); /* C5: key track pivots at C4 */
            fm_engine_render(e[n], n ? gl_buf_off : gl_buf, GL_RATE / 5);
            if (!n) fm_engine_set_param(e[n], FM_PARAM_SETTING_GLIDE, 1); /* on after a render: no start-up glide */
            fm_engine_set_param(e[n], k->id, k->to);
            fm_engine_render(e[n], n ? gl_buf_off : gl_buf, GL_RATE / 2);
        }
        rms = gl_rms(gl_buf_off, GL_RATE * 15 / 100, GL_RATE / 5);
        early = gl_rms_diff(gl_buf, gl_buf_off, 0, GL_RATE * 3 / 1000) / rms;
        if (k->pitch) {
            double on = gl_swing_cents(gl_buf, GL_RATE * 15 / 100, GL_RATE / 2);
            double off = gl_swing_cents(gl_buf_off, GL_RATE * 15 / 100, GL_RATE / 2);
            late = fabs(on - off) / off;
            printf("    %s: off the jump by %.0f %% over the first 3 ms; swing 150..500 ms %.1f against %.1f cents\n",
                   k->name, 100 * early, on, off);
            TT_CHECK(late < 0.05);
        } else {
            late = gl_rms_diff(gl_buf, gl_buf_off, GL_RATE * 15 / 100, GL_RATE / 5) / rms;
            printf("    %s: off the jump by %.0f %% over the first 3 ms, %.2f %% 150 ms on\n",
                   k->name, 100 * early, 100 * late);
            TT_CHECK(late < 0.01);
        }
        TT_CHECK(early > 0.3);
    }
}

/* The start-up glide and a mid-note one, played in odd blocks, are the
 * same samples as in one block. */
TT_TEST(glides_do_not_depend_on_block_size)
{
    int n;
    for (n = 0; n < 2; n++) {
        int16_t *out = n ? gl_buf_off : gl_buf;
        uint32_t done = 0, b = 1;
        TT_CHECK_EQ(fm_engine_init(&gl_eng, GL_RATE, 6), FM_OK);
        fm_engine_set_param(&gl_eng, FM_PARAM_SETTING_GLIDE, 1);
        fm_engine_set_param(&gl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 7000);
        fm_engine_set_param(&gl_eng, FM_PARAM_FILTER_RESONANCE, 500);
        fm_engine_note_on(&gl_eng, 60, 127);
        while (done < GL_RATE / 2) {
            uint32_t m = n ? b : GL_RATE / 4;
            if (m > GL_RATE / 2 - done) m = GL_RATE / 2 - done;
            if (done == GL_RATE / 4) fm_engine_set_param(&gl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 11000);
            if (done < GL_RATE / 4 && done + m > GL_RATE / 4) m = GL_RATE / 4 - done;
            fm_engine_render(&gl_eng, out + 2 * done, m);
            done += m;
            b = b % 97 + 13;
        }
    }
    TT_CHECK(memcmp(gl_buf, gl_buf_off, sizeof gl_buf / 2) == 0);
}

static void run_glide_tests(void)
{
    TT_RUN(glides_do_not_depend_on_block_size);
    TT_RUN(each_setting_glides_to_a_mid_note_change);
    TT_RUN(a_patch_set_after_init_glides_from_pg8xs_power_on_values);
}
