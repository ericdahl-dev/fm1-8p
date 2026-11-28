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
static void slp_partials(uint32_t rate, uint8_t wave, uint8_t note, const int *k, int count, double *db)
{
    double h1;
    int i;
    fm_engine_init(&slp_eng, rate, 4);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&slp_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&slp_eng, FM_PARAM_OSC1_WAVE, wave);
    fm_engine_note_on(&slp_eng, note, 100);
    fm_engine_render(&slp_eng, slp_buf, 4800 + SLP_DFT_N);
    h1 = tone_amp(slp_buf + 2 * 4800, SLP_DFT_N, 0, note_hz(note), rate);
    for (i = 0; i < count; i++)
        db[i] = 20.0 * log10(tone_amp(slp_buf + 2 * 4800, SLP_DFT_N, 0, k[i] * note_hz(note), rate) / h1);
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
    slp_partials(SLP_RATE, FM_WAVE_SAW, 84, k, 4, db);
    for (i = 0; i < 4; i++) {
        printf("    C6 saw h%d: %+.2f dB re h1 (PG-8X %+.2f)\n", k[i], db[i], pg[i]);
        TT_CHECK(fabs(db[i] - pg[i]) < 1.0);
    }
}

/*
 * The same at 44.1 kHz, the FM-1's rate, against PG-8X rendered at 44.1 kHz (f0 1045.33 Hz): h5 -14.98,
 * h9 -22.32, h13 -28.84, h17 -35.45 dB re h1, within 0.5 dB. Without the lowpass ours read -14.37, -20.39,
 * -25.07, -29.52; with one weight for both rates (0.66) h17 read 1.44 dB dark here.
 */
TT_TEST(source_lowpass_c6_saw_top_band_reads_as_pg8x_at_44k1)
{
    static const int k[] = { 5, 9, 13, 17 };
    static const double pg[] = { -14.98, -22.32, -28.84, -35.45 };
    double db[4];
    int i;
    slp_partials(44100, FM_WAVE_SAW, 84, k, 4, db);
    for (i = 0; i < 4; i++) {
        printf("    44.1 kHz C6 saw h%d: %+.2f dB re h1 (PG-8X %+.2f)\n", k[i], db[i], pg[i]);
        TT_CHECK(fabs(db[i] - pg[i]) < 0.5);
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
    TT_CHECK(!loud);    /* and a new init of an engine that was playing starts from silence too */
    fm_engine_note_on(&slp_eng, 84, 127);
    fm_engine_render(&slp_eng, slp_buf, 4801);
    TT_CHECK(peak_abs(slp_buf, 2000, 4801) > 500);
    TT_CHECK_EQ(fm_engine_init(&slp_eng, SLP_RATE, 4), FM_OK);
    fm_engine_render(&slp_eng, slp_buf, 256);
    for (i = 0; i < 2 * 256; i++)
        if (slp_buf[i] != 0) loud = 1;
    TT_CHECK(!loud);
}

/* RMS of the source lowpass's response to a sine of `hz` at `rate`, re the sine's, dB (after 64 samples). */
static double slp_gain_db(double hz, double rate)
{
    int32_t last = 0, a = fm_out_source_lp_weight((uint32_t)rate);
    double in2 = 0, out2 = 0;
    int i;
    for (i = 0; i < 64 + 48000; i++) {
        int32_t x = (int32_t)(8388608.0 * sin(2.0 * AN_PI * hz * i / rate)); /* 0.5, Q8.24 */
        int32_t y = fm_out_source_lp(x, a, &last);
        if (i < 64) continue;
        in2 += (double)x * x;
        out2 += (double)y * y;
    }
    return 10.0 * log10(out2 / in2);
}

/*
 * The response at each fitted rate is within 0.6 dB of the mean of PG-8X minus ours in the bands it was fitted
 * on, read at each band's center (fm1-8p-ref/pg8x/match/299a: saw and square at C4, C5, C6, D6, C7 and D7,
 * PG-8X rendered at that rate). And it stays near the analog 1-pole that fits PG-8X minus ours best (10650
 * Hz): within 1.1 dB from 1 to 18 kHz at both rates.
 */
TT_TEST(source_lowpass_response_matches_the_fit_at_each_rate)
{
    static const double hz[] = { 1000, 2000, 5000, 8000, 10000, 12000, 15000, 18000 };
    static const struct { double rate; struct { double hz, db; } band[5]; } pg[] = {
        { 44100, { { 5000, -0.57 }, { 9000, -1.78 }, { 13000, -3.46 }, { 15000, -4.44 }, { 19000, -7.27 } } },
        { 48000, { { 5000, -0.58 }, { 9000, -1.83 }, { 13000, -3.59 }, { 15000, -4.64 }, { 19000, -7.02 } } },
    };
    size_t r, i;
    for (r = 0; r < sizeof pg / sizeof pg[0]; r++) {
        for (i = 0; i < 5; i++) {
            double db = slp_gain_db(pg[r].band[i].hz, pg[r].rate);
            printf("    %.0f Hz rate, %5.0f Hz: %+.2f dB (PG-8X minus ours %+.2f)\n", pg[r].rate, pg[r].band[i].hz, db,
                   pg[r].band[i].db);
            TT_CHECK(fabs(db - pg[r].band[i].db) < 0.6);
        }
        for (i = 0; i < sizeof hz / sizeof hz[0]; i++) {
            double db = slp_gain_db(hz[i], pg[r].rate), analog = -10.0 * log10(1.0 + pow(hz[i] / 10650.0, 2));
            printf("    %.0f Hz rate, %5.0f Hz: %+.2f dB (analog 1-pole at 10650 Hz %+.2f)\n", pg[r].rate, hz[i], db, analog);
            TT_CHECK(fabs(db - analog) < 1.1);
        }
    }
}

/*
 * The weight by rate: the fitted values at 44.1 and 48 kHz, a wire at 32 kHz and below, the two taps' most
 * (0.5) at 88.2 kHz and above, and between them it only falls as the rate rises, with no jump at a fitted point.
 */
TT_TEST(source_lowpass_weight_follows_the_sample_rate)
{
    uint32_t hz;
    int32_t prev = 32768;
    TT_CHECK_EQ(fm_out_source_lp_weight(44100), 22774); /* 0.6950 */
    TT_CHECK_EQ(fm_out_source_lp_weight(48000), 21381); /* 0.6525 */
    TT_CHECK_EQ(fm_out_source_lp_weight(8000), 32768);
    TT_CHECK_EQ(fm_out_source_lp_weight(32000), 32768);
    TT_CHECK_EQ(fm_out_source_lp_weight(88200), 16384);
    TT_CHECK_EQ(fm_out_source_lp_weight(192000), 16384);
    for (hz = 8000; hz <= 192000; hz++) {
        int32_t a = fm_out_source_lp_weight(hz);
        TT_CHECK(a <= prev && prev - a <= 2);
        prev = a;
    }
}

/* DC passes at unity, exactly, at every level up to the lowpass's range (one sample after a step). */
TT_TEST(source_lowpass_dc_gain_is_unity)
{
    static const int32_t level[] = { 1, -1, 12345, -98765, 1 << 24, -(1 << 24), FM_OUT_SOURCE_LP_MAX,
                                     -FM_OUT_SOURCE_LP_MAX };
    size_t i;
    for (i = 0; i < sizeof level / sizeof level[0]; i++) {
        int32_t last = 0;
        int n;
        (void)fm_out_source_lp(level[i], FM_OUT_SOURCE_LP_A_44K1, &last);
        for (n = 0; n < 4; n++) TT_CHECK_EQ(fm_out_source_lp(level[i], FM_OUT_SOURCE_LP_A_44K1, &last), level[i]);
    }
}

/*
 * No added clipping: through fm_out_mix_block, the engine's path, each output lies between the two scaled
 * inputs it weighs, so it is never louder than the scaled sum (no overshoot for the soft clip to bend) and
 * never wraps: for any int32 sum (rail-to-rail steps, loud steps, noise) at the largest gain the block takes
 * (0.5, FM_OUT_MIX_GAIN_MAX: the scaled sum then spans the lowpass's whole range, +-2^30; the engine's
 * largest gain is 0.36), at both fitted weights. CI runs this under UBSan.
 */
TT_TEST(source_lowpass_adds_no_peak_and_never_wraps)
{
    static int32_t x[4096], m[4096];
    int32_t last = 0, prev = 0;
    uint32_t r = 0x2545F491u;
    int blk, i, bad = 0;
    for (blk = 0; blk < 64; blk++) {
        int32_t a = blk & 1 ? FM_OUT_SOURCE_LP_A_48K : FM_OUT_SOURCE_LP_A_44K1;
        for (i = 0; i < 4096; i++) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            x[i] = blk < 2 ? ((i & 1) ? INT32_MAX : INT32_MIN) : blk < 4 ? ((i & 2) ? (1 << 26) : -(1 << 26)) : (int32_t)r;
            m[i] = (int32_t)(((int64_t)x[i] * FM_OUT_MIX_GAIN_MAX) >> 30);
        }
        fm_out_mix_block(x, 4096, FM_OUT_MIX_GAIN_MAX, a, &last);
        for (i = 0; i < 4096; i++) {
            int32_t lo = m[i] < prev ? m[i] : prev, hi = m[i] < prev ? prev : m[i];
            if (x[i] < lo || x[i] > hi) bad++;
            prev = m[i];
        }
    }
    TT_CHECK_EQ(bad, 0);
}

static void run_source_lp_tests(void)
{
    TT_RUN(source_lowpass_response_matches_the_fit_at_each_rate);
    TT_RUN(source_lowpass_weight_follows_the_sample_rate);
    TT_RUN(source_lowpass_dc_gain_is_unity);
    TT_RUN(source_lowpass_adds_no_peak_and_never_wraps);
    TT_RUN(source_lowpass_holds_nothing_at_init_or_after_all_sound_off);
    TT_RUN(source_lowpass_c6_saw_top_band_reads_as_pg8x);
    TT_RUN(source_lowpass_c6_saw_top_band_reads_as_pg8x_at_44k1);
}
