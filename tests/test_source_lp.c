/*
 * The source lowpass (#299 option A): PG-8X 2.0.13's fixed top-band roll-off, which every source (saw, square,
 * synced DCO-2, noise) carries and ours lacked.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */

#define SLP_RATE 48000u
#define SLP_DFT_N 32768
static int16_t slp_buf[2 * (4800 + SLP_DFT_N)];
static fm_engine slp_eng;

#include "../engine/output.h"

/* h[k] re h1, dB, of a held note on DCO-1 alone with `wave`, filter open, highpass off, chorus off. */
static void slp_partials(uint8_t wave, uint8_t note, const int *k, int count, double *db)
{
    double h1;
    int i;
    fm_engine_init(&slp_eng, SLP_RATE, 4);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&slp_eng, FM_PARAM_OSC1_WAVE, wave);
    fm_engine_note_on(&slp_eng, note, 100);
    fm_engine_render(&slp_eng, slp_buf, 4800 + SLP_DFT_N);
    h1 = tone_amp(slp_buf + 2 * 4800, SLP_DFT_N, 0, note_hz(note), SLP_RATE);
    for (i = 0; i < count; i++)
        db[i] = 20.0 * log10(tone_amp(slp_buf + 2 * 4800, SLP_DFT_N, 0, k[i] * note_hz(note), SLP_RATE) / h1);
}

/*
 * A held C6 saw's top band reads as PG-8X's within 1 dB. PG-8X 2.0.13 (SELFNOISE 0, VCF FREQ 127, RES 0, HPF 1,
 * chorus off) at its measured f0, 1045.25 Hz, 48 kHz: h5 -14.92, h9 -22.16, h13 -28.53, h17 -34.93 dB re h1
 * (fm1-8p-ref/pg8x/match/299a/top_probe.py). Without the lowpass ours read -14.31, -20.19, -24.62, -28.72.
 */
TT_TEST(source_lowpass_c6_saw_top_band_reads_as_pg8x)
{
    static const int k[] = { 5, 9, 13, 17 };
    static const double pg[] = { -14.92, -22.16, -28.53, -34.93 };
    double db[4];
    int i;
    slp_partials(FM_WAVE_SAW, 84, k, 4, db);
    for (i = 0; i < 4; i++) {
        printf("    C6 saw h%d: %+.2f dB re h1 (PG-8X %+.2f)\n", k[i], db[i], pg[i]);
        TT_CHECK(fabs(db[i] - pg[i]) < 1.0);
    }
}

/*
 * Bypass-safe: a new engine plays silence as silence, and all sound off (CC 120) is silent from the next
 * sample, with no last sample of the note left in the lowpass to leak out (chorus off: nothing else holds one).
 */
TT_TEST(source_lowpass_holds_nothing_at_init_or_after_all_sound_off)
{
    size_t i;
    int loud = 0;
    TT_CHECK_EQ(fm_engine_init(&slp_eng, SLP_RATE, 4), FM_OK);
    fm_engine_render(&slp_eng, slp_buf, 256);
    for (i = 0; i < 2 * 256; i++)
        if (slp_buf[i] != 0) loud = 1;
    TT_CHECK(!loud);
    fm_engine_note_on(&slp_eng, 84, 127);
    fm_engine_render(&slp_eng, slp_buf, 4801); /* ends mid-cycle: the lowpass holds a loud sample */
    TT_CHECK(peak_abs(slp_buf, 2000, 4801) > 500);
    fm_engine_controller(&slp_eng, 120, 0);
    fm_engine_render(&slp_eng, slp_buf, 256);
    for (i = 0; i < 2 * 256; i++)
        if (slp_buf[i] != 0) loud = 1;
    TT_CHECK(!loud);
}

/* RMS of the source lowpass's response to a sine of `hz` at `rate`, re the sine's, dB (after 64 samples). */
static double slp_gain_db(double hz, double rate)
{
    int32_t last = 0;
    double in2 = 0, out2 = 0;
    int i;
    for (i = 0; i < 64 + 48000; i++) {
        int32_t x = (int32_t)(8388608.0 * sin(2.0 * AN_PI * hz * i / rate)); /* 0.5, Q8.24 */
        int32_t y = fm_out_source_lp(x, &last);
        if (i < 64) continue;
        in2 += (double)x * x;
        out2 += (double)y * y;
    }
    return 10.0 * log10(out2 / in2);
}

/*
 * The response, fitted (output.h): within 1.4 dB of the analog 1-pole at 10650 Hz from 1 to 20 kHz at 48 kHz
 * (measured: 1.00 dB the brighter at 10 kHz, 1.33 dB the darker at 20 kHz), and within 0.6 dB of the mean of
 * PG-8X minus ours (fm1-8p-ref/pg8x/match/299a: saw and square at C4, C5, C6, D6, C7 and D7, 48 kHz) in the
 * bands it was fitted on: -0.58 dB at 4-6 kHz, -1.83 at 8-10, -3.59 at 12-14, -4.64 at 14-16 and -7.02 at
 * 18-20 kHz, read at each band's center.
 */
TT_TEST(source_lowpass_response_matches_the_fit)
{
    static const double hz[] = { 1000, 2000, 5000, 8000, 10000, 12000, 15000, 18000, 20000 };
    static const struct { double hz, db; } pg[] = {
        { 5000, -0.58 }, { 9000, -1.83 }, { 13000, -3.59 }, { 15000, -4.64 }, { 19000, -7.02 },
    };
    size_t i;
    for (i = 0; i < sizeof hz / sizeof hz[0]; i++) {
        double db = slp_gain_db(hz[i], 48000.0), analog = -10.0 * log10(1.0 + pow(hz[i] / 10650.0, 2));
        printf("    %5.0f Hz: %+.2f dB (analog 1-pole at 10650 Hz %+.2f)\n", hz[i], db, analog);
        TT_CHECK(fabs(db - analog) < 1.4);
    }
    for (i = 0; i < sizeof pg / sizeof pg[0]; i++) {
        double db = slp_gain_db(pg[i].hz, 48000.0);
        printf("    %5.0f Hz: %+.2f dB (PG-8X minus ours %+.2f)\n", pg[i].hz, db, pg[i].db);
        TT_CHECK(fabs(db - pg[i].db) < 0.6);
    }
}

/* DC passes at unity, exactly, at every level up to the rails (one sample after a step). */
TT_TEST(source_lowpass_dc_gain_is_unity)
{
    static const int32_t level[] = { 1, -1, 12345, -98765, 1 << 24, -(1 << 24), INT32_MAX, INT32_MIN };
    size_t i;
    for (i = 0; i < sizeof level / sizeof level[0]; i++) {
        int32_t last = 0;
        int n;
        (void)fm_out_source_lp(level[i], &last);
        for (n = 0; n < 4; n++) TT_CHECK_EQ(fm_out_source_lp(level[i], &last), level[i]);
    }
}

/*
 * No added clipping: each output lies between the two inputs it weighs, so it is never louder than the input
 * (no overshoot for the soft clip to bend) and never wraps, for any int32 (full-scale steps, the rails, noise).
 */
TT_TEST(source_lowpass_adds_no_peak_and_never_wraps)
{
    int32_t last = 0, prev = 0;
    uint32_t r = 0x2545F491u;
    int i, bad = 0;
    for (i = 0; i < 200000; i++) {
        int32_t x, y, lo, hi;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        x = i < 1000 ? ((i & 1) ? INT32_MAX : INT32_MIN) : i < 2000 ? ((i & 2) ? (1 << 26) : -(1 << 26)) : (int32_t)r;
        y = fm_out_source_lp(x, &last);
        lo = x < prev ? x : prev;
        hi = x < prev ? prev : x;
        if (y < lo || y > hi) bad++;
        prev = x;
    }
    TT_CHECK_EQ(bad, 0);
}

static void run_source_lp_tests(void)
{
    TT_RUN(source_lowpass_response_matches_the_fit);
    TT_RUN(source_lowpass_dc_gain_is_unity);
    TT_RUN(source_lowpass_adds_no_peak_and_never_wraps);
    TT_RUN(source_lowpass_holds_nothing_at_init_or_after_all_sound_off);
    TT_RUN(source_lowpass_c6_saw_top_band_reads_as_pg8x);
}
