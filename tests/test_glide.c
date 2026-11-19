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

/* A patch load sets every field, setting_glide (still 1) among them: a
 * change in the same load still glides. */
TT_TEST(setting_the_glide_on_again_keeps_a_change_gliding)
{
    int n;
    double early;
    fm_engine *e[2] = { &gl_eng, &gl_off };
    for (n = 0; n < 2; n++) {
        TT_CHECK_EQ(fm_engine_init(e[n], GL_RATE, 6), FM_OK);
        fm_engine_set_param(e[n], FM_PARAM_AMP_ATTACK_MS, 1);
        fm_engine_set_param(e[n], FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_note_on(e[n], 72, 127);
        fm_engine_render(e[n], n ? gl_buf_off : gl_buf, GL_RATE / 5);
        if (!n) fm_engine_set_param(e[n], FM_PARAM_SETTING_GLIDE, 1);
        fm_engine_set_param(e[n], FM_PARAM_FILTER_CUTOFF_CENTS, 8000);
        if (!n) fm_engine_set_param(e[n], FM_PARAM_SETTING_GLIDE, 1); /* the load sets it again */
        fm_engine_render(e[n], n ? gl_buf_off : gl_buf, GL_RATE / 10);
    }
    early = gl_rms_diff(gl_buf, gl_buf_off, 0, GL_RATE * 3 / 1000) / gl_rms(gl_buf_off, GL_RATE / 20, GL_RATE / 10);
    printf("    cutoff set with the glide set on again: off the jump by %.0f %% over 3 ms\n", 100 * early);
    TT_CHECK(early > 0.3);
}

/* A held note at VCA LEVEL `from`, the level set to `to` 300 ms in with the glide `glide` (on: as
 * PG-8X, #169), rendered to 600 ms into gl_buf. */
static void gl_level_turn(int glide, int32_t from, int32_t to)
{
    fm_engine_init(&gl_eng, GL_RATE, 6);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_LEVEL, from);
    fm_engine_set_param(&gl_eng, FM_PARAM_SETTING_GLIDE, glide);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_ATTACK_MS, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&gl_eng, 84, 127);
    fm_engine_render(&gl_eng, gl_buf, GL_RATE * 3 / 10);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_LEVEL, to);
    fm_engine_render(&gl_eng, gl_buf + 2 * (GL_RATE * 3 / 10), GL_RATE * 3 / 10);
}

/* VCA LEVEL turned while a note is held: the note follows it, as PG-8X's does (it read the level only
 * at note on, #169). From 1000 to 250, 12.0 dB down once it lands (within 0.3 dB). */
TT_TEST(a_held_note_follows_a_vca_level_change)
{
    double before, after;
    gl_level_turn(1, 1000, 250);
    before = gl_rms(gl_buf, GL_RATE / 10, GL_RATE * 3 / 10);
    after = gl_rms(gl_buf, GL_RATE / 2, GL_RATE * 6 / 10);
    printf("    VCA LEVEL 1000 -> 250 mid-note: %.2f dB\n", 20 * log10(after / before));
    TT_CHECK(fabs(20 * log10(after / before) + 12.04) < 0.3);
}

/* It glides there as PG-8X's settings do, a one-pole of 16 ms (PG-8X's VCA LEVEL measured 15.9 ms,
 * #146): from 1000 to 250, 0.526 of the start level 16 ms on (0.45 to 0.62: the glide moves in 2.7 ms
 * steps), and still near it 1 ms on. */
TT_TEST(a_vca_level_change_glides_over_16_ms)
{
    double start, at1, at16;
    gl_level_turn(1, 1000, 250);
    start = gl_peak_at(290);
    at1 = gl_peak_at(301);
    at16 = gl_peak_at(316);
    printf("    VCA LEVEL 1000 -> 250: %.3f of the start 1 ms on, %.3f 16 ms on\n", at1 / start, at16 / start);
    TT_CHECK(at1 / start > 0.85);
    TT_CHECK(at16 / start > 0.45 && at16 / start < 0.62);
}

/* With the glide off (the raw engine, Eric's decision on #219) a VCA LEVEL change acts at once: a
 * quarter of the level 1 ms on. */
TT_TEST(a_vca_level_change_without_the_glide_acts_at_once)
{
    double start, at1;
    gl_level_turn(0, 1000, 250);
    start = gl_peak_at(290);
    at1 = gl_peak_at(301);
    printf("    no glide, VCA LEVEL 1000 -> 250: %.3f of the start 1 ms on\n", at1 / start);
    TT_CHECK(at1 / start > 0.2 && at1 / start < 0.3);
}

/* A patch with VCA LEVEL under full, set after init, glides in from PG-8X's power-on level (full), as
 * its renders start: at 250, the note's first 2 ms are near full and it is down to 250 by 100 ms,
 * 11.2 dB apart for a 16 ms one-pole (within 2 dB). */
TT_TEST(a_patch_with_a_low_vca_level_glides_in_from_full)
{
    double first, later;
    fm_engine_init(&gl_eng, GL_RATE, 6);
    fm_engine_set_param(&gl_eng, FM_PARAM_SETTING_GLIDE, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_LEVEL, 250);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_ATTACK_MS, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&gl_eng, 84, 127);
    fm_engine_render(&gl_eng, gl_buf, GL_RATE / 5);
    first = gl_peak_at(2);
    later = gl_peak_at(100);
    printf("    VCA LEVEL 250 after init: %.1f dB from 2 to 100 ms\n", 20 * log10(first / later));
    TT_CHECK(fabs(20 * log10(first / later) - 11.2) < 2.0);
}

/* A full-range cutoff jump mid-note glides in steps no larger than PG-8X's (#219: its glide steps
 * about every 4 ms, 22 % of the way a step at 16 ms; ours every 2.7 ms, 15 %): read every 16 samples
 * (the voice filter's control points), no move over 20 % of the jump, and there by 100 ms. */
TT_TEST(a_full_range_cutoff_jump_glides_in_small_steps)
{
    const fm_vfilter *vf;
    int32_t from, to, last, now, big = 0;
    uint32_t k;
    fm_engine_init(&gl_eng, GL_RATE, 6);
    fm_engine_set_param(&gl_eng, FM_PARAM_SETTING_GLIDE, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 0);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&gl_eng, 60, 127);
    fm_engine_render(&gl_eng, gl_buf, GL_RATE / 2); /* landed on 0 */
    vf = fm_voice_filter(&gl_eng.voices[0]);
    from = last = fm_vfilter_cutoff(vf);
    fm_engine_set_param(&gl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, FM_FILTER_OPEN_CENTS);
    for (k = 0; k < GL_RATE / 10 / 16; k++) {
        fm_engine_render(&gl_eng, gl_buf, 16);
        now = fm_vfilter_cutoff(vf);
        if (now - last > big) big = now - last;
        last = now;
    }
    to = last;
    printf("    cutoff %ld -> %ld cents: the largest step %ld (%.1f %% of the jump)\n", (long)from, (long)to,
           (long)big, 100.0 * big / (to - from));
    TT_CHECK(to - from > 10000);
    TT_CHECK(big * 5 <= to - from);
}

/* Samples from a note's key up until its voice is free, at VCA LEVEL `level`, the release 100 ms. */
static uint32_t gl_frees_after(int32_t level)
{
    uint32_t n = 0;
    fm_engine_init(&gl_eng, GL_RATE, 6);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_LEVEL, level);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_ATTACK_MS, 1);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_RELEASE_MS, 100);
    fm_engine_note_on(&gl_eng, 60, 127);
    fm_engine_render(&gl_eng, gl_buf, GL_RATE / 10);
    fm_engine_note_off(&gl_eng, 60);
    while (fm_voice_active(&gl_eng.voices[0]) && n < 2 * GL_RATE) {
        fm_engine_render(&gl_eng, gl_buf, 1);
        n++;
    }
    return n;
}

/* A voice is free once its note's output is 80 dB down, whatever the VCA LEVEL (as when the level was
 * in the note's peak, before #169 made it the output's gain): a release (tau 100 ms) at level 279 frees
 * the voice ln(1000 / 279) = 1.277 tau sooner than at full, so a quiet tone's inaudible tail holds no
 * voice (within 3 ms). */
TT_TEST(a_quiet_vca_level_frees_its_voice_when_its_output_is_80_db_down)
{
    uint32_t full = gl_frees_after(1000), quiet = gl_frees_after(279);
    printf("    voice free after key up: %.1f ms at level 1000, %.1f ms at 279\n", 1000.0 * full / GL_RATE,
           1000.0 * quiet / GL_RATE);
    TT_CHECK(fabs(1000.0 * (full - (double)quiet) / GL_RATE - 127.7) < 3.0);
}

/* The VCA LEVEL gliding down to 0 on a held note (each step sets the amp envelope's floor): no divide by
 * 0 (the level is taken as 1 there), silence, and the voice frees soon after its key comes up. */
TT_TEST(a_vca_level_gliding_to_zero_keeps_its_floor_safe)
{
    uint32_t n = 0;
    gl_level_turn(1, 1000, 0);
    TT_CHECK_EQ(gl_peak_at(590), 0);
    fm_engine_set_param(&gl_eng, FM_PARAM_AMP_RELEASE_MS, 100);
    fm_engine_note_off(&gl_eng, 84);
    while (fm_voice_active(&gl_eng.voices[0]) && n < GL_RATE) {
        fm_engine_render(&gl_eng, gl_buf, 1);
        n++;
    }
    printf("    level 0: the voice free %.1f ms after key up\n", 1000.0 * n / GL_RATE);
    TT_CHECK(n < GL_RATE / 4); /* 80 dB under a level of 1/1000: 20 dB, 2.3 tau */
}

static void run_glide_tests(void)
{
    TT_RUN(setting_the_glide_on_again_keeps_a_change_gliding);
    TT_RUN(glides_do_not_depend_on_block_size);
    TT_RUN(each_setting_glides_to_a_mid_note_change);
    TT_RUN(a_patch_set_after_init_glides_from_pg8xs_power_on_values);
    TT_RUN(a_held_note_follows_a_vca_level_change);
    TT_RUN(a_vca_level_change_glides_over_16_ms);
    TT_RUN(a_vca_level_change_without_the_glide_acts_at_once);
    TT_RUN(a_patch_with_a_low_vca_level_glides_in_from_full);
    TT_RUN(a_full_range_cutoff_jump_glides_in_small_steps);
    TT_RUN(a_quiet_vca_level_frees_its_voice_when_its_output_is_80_db_down);
    TT_RUN(a_vca_level_gliding_to_zero_keeps_its_floor_safe);
}
