/*
 * The top of the cutoff range at resonance 0 (#151): how the filter opens to the wire, against PG-8X 2.0.13.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * PG-8X figures: PG-8X 2.0.13 under Wine, SELFNOISE 0 (param 56), RES 0, HPF 1 (ours: highpass step 0), chorus
 * off, DCO-1 saw alone, a C5 held from 0.2 s, rendered at the rate named. Harmonics are read over 0.7-2.7 s at
 * the render's measured f0 (522.67 Hz at 44.1 kHz, 522.80 at 48 kHz: PG-8X plays a few cents flat), each re
 * h1, and then re the same harmonic at VCF FREQ 127: the filter's response alone, with the source divided out
 * (fm1-8p-ref/pg8x/match/151/refit.py, 2026-11-26). Ours is read the same way, re the open filter.
 */

#define VT_DFT_N 32768
static int16_t vt_buf[2 * (4800 + VT_DFT_N)];
static fm_engine vt_eng;

/* The harmonics read: 4.2, 7.8, 12.0, 16.2 and 19.4 kHz at C5. */
#define VT_HARMONICS 5
static const int vt_k[VT_HARMONICS] = { 8, 15, 23, 31, 37 };

/* h[vt_k] re h1, dB, of a held C5 saw through the filter at `cutoff` cents and `resonance` per mille. */
static void vt_partials(uint32_t rate, int32_t cutoff, int32_t resonance, double *db)
{
    double h1;
    int i;
    fm_engine_init(&vt_eng, rate, 4);
    fm_engine_set_param(&vt_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&vt_eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&vt_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&vt_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SAW);
    fm_engine_set_param(&vt_eng, FM_PARAM_FILTER_RESONANCE, resonance);
    fm_engine_set_param(&vt_eng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff);
    fm_engine_note_on(&vt_eng, 72, 100);
    fm_engine_render(&vt_eng, vt_buf, 4800 + VT_DFT_N);
    h1 = tone_amp(vt_buf + 2 * 4800, VT_DFT_N, 0, note_hz(72), rate);
    for (i = 0; i < VT_HARMONICS; i++)
        db[i] = 20.0 * log10(tone_amp(vt_buf + 2 * 4800, VT_DFT_N, 0, vt_k[i] * note_hz(72), rate) / h1);
}

/* The filter's response at the harmonics: vt_partials re the open filter's. */
static void vt_response(uint32_t rate, int32_t cutoff, int32_t resonance, double *db)
{
    double open[VT_HARMONICS];
    int i;
    vt_partials(rate, FM_FILTER_OPEN_CENTS, 0, open);
    vt_partials(rate, cutoff, resonance, db);
    for (i = 0; i < VT_HARMONICS; i++) db[i] -= open[i];
}

/* Up the cutoff from 12100 to open by `step` cents at `rate`, resonance 0: the most any harmonic read (vt_k
 * and h34) is over the open filter, and the most one falls from a step to the next, dB. */
static void vt_opening(uint32_t rate, int32_t step, double *over, double *fall)
{
    static const int k[] = { 8, 15, 23, 31, 34, 37 };
    enum { N = sizeof k / sizeof k[0] };
    double open[N], prev[N], h1;
    int32_t c = FM_FILTER_OPEN_CENTS;   /* the first pass: the open filter */
    int i, first = 1;
    *over = *fall = -1e9;
    while (c <= FM_FILTER_OPEN_CENTS) {
        fm_engine_init(&vt_eng, rate, 4);
        fm_engine_set_param(&vt_eng, FM_PARAM_AMP_ATTACK_MS, 0);
        fm_engine_set_param(&vt_eng, FM_PARAM_AMP_DECAY_MS, 0);
        fm_engine_set_param(&vt_eng, FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_set_param(&vt_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SAW);
        fm_engine_set_param(&vt_eng, FM_PARAM_FILTER_CUTOFF_CENTS, c);
        fm_engine_note_on(&vt_eng, 72, 100);
        fm_engine_render(&vt_eng, vt_buf, 4800 + VT_DFT_N);
        h1 = tone_amp(vt_buf + 2 * 4800, VT_DFT_N, 0, note_hz(72), rate);
        for (i = 0; i < N; i++) {
            double db = 20.0 * log10(tone_amp(vt_buf + 2 * 4800, VT_DFT_N, 0, k[i] * note_hz(72), rate) / h1);
            if (first) {
                open[i] = db;
                continue;
            }
            db -= open[i];
            if (db > *over) *over = db;
            if (c > 12100 && prev[i] - db > *fall) *fall = prev[i] - db;
            prev[i] = db;
        }
        c = first ? 12100 : c + step;
        first = 0;
    }
}

/*
 * At resonance 0 the top opens as PG-8X's does: from VCF FREQ 108 to 127 each harmonic of PG-8X's saw rises
 * with every step and none goes over its open level (at 44.1 kHz h37, 19.4 kHz, reads -26.2 dB at 108, -16.4 at
 * 112, -8.5 at 116, -3.5 at 120, -1.0 at 124 and 0 at 127). Here, at the FM-1's rate, up the cutoff from 12100
 * (FREQ 108) to open in 20-cent steps: no harmonic reads more than 0.3 dB over the open filter, and none falls
 * by more than 1.5 dB from a step to the next. (Before #151 the sections ran on to 0.425 of the rate: h37 read
 * +14.6 dB over open at 13752, FREQ 120, and fell 9.8 dB in 100 cents at 12500.)
 *
 * Ours is not strictly one way: near 12560 (FREQ 111.4), where section 1's zeros leave, the harmonics around
 * 17.5 kHz dip about 1.2 dB as the cutoff rises 110 cents (h34: 0.35 dB in one 20-cent step), near -18 dB.
 */
TT_TEST(vcf_top_opens_one_way_at_resonance_0)
{
    double over, fall;
    vt_opening(44100, 20, &over, &fall);
    printf("    44.1 kHz, resonance 0, 12100 to open by 20 cents: most over the open filter %+.2f dB, largest fall "
           "a step %.2f dB\n", over, fall);
    TT_CHECK(over < 0.3);
    TT_CHECK(fall < 1.5);
}

/*
 * The top is fitted at 44.1 and 48 kHz, and only rates from 44.1 to 48 kHz get it: its fade and damping are in
 * cents of cutoff and its f0 stop is a share of the rate, so at a higher rate the sections never reach the
 * stop and the long fade to the input cuts a notch (192 kHz: -50.5 dB at 15.4 kHz at cutoff 12450, and a 33 dB
 * fall as the cutoff rises 260 cents; 96 kHz: 3.7 dB). Other rates keep the top they had: at 96 and 192 kHz no
 * harmonic falls by more than 1 dB over a 50-cent step.
 */
TT_TEST(vcf_top_other_rates_keep_the_top_they_had)
{
    static const uint32_t rate[] = { 96000, 192000 };
    size_t r;
    for (r = 0; r < sizeof rate / sizeof rate[0]; r++) {
        double over, fall;
        vt_opening(rate[r], 50, &over, &fall);
        printf("    %u Hz, resonance 0, 12100 to open by 50 cents: most over the open filter %+.2f dB, largest fall "
               "a step %.2f dB\n", (unsigned)rate[r], over, fall);
        TT_CHECK(fall < 1.0);
    }
}

/* PG-8X's response at the harmonics (the header's method), by VCF FREQ and the engine's cutoff for it */
typedef struct { int freq; int32_t cut; double db[VT_HARMONICS]; } vt_row;

/* Worst |ours - PG-8X| over `rows` at `rate`, dB; prints each row. */
static double vt_worst(uint32_t rate, const vt_row *rows, int count)
{
    double worst = 0;
    int r, i;
    for (r = 0; r < count; r++) {
        double db[VT_HARMONICS];
        vt_response(rate, rows[r].cut, 0, db);
        printf("    %u Hz, VCF FREQ %d (%d cents):", (unsigned)rate, rows[r].freq, (int)rows[r].cut);
        for (i = 0; i < VT_HARMONICS; i++) {
            printf(" h%d %+.2f (%+.2f)", vt_k[i], db[i], rows[r].db[i]);
            worst = fmax(worst, fabs(db[i] - rows[r].db[i]));
        }
        printf("\n");
    }
    return worst;
}

/*
 * At the FM-1's rate a saw at VCF FREQ 114 to 124, resonance 0, reads as PG-8X's within 1.5 dB at every
 * harmonic read, 4 to 19.4 kHz. PG-8X rendered at 44.1 kHz. (Before #151 ours read +11.4 dB at h37 for FREQ
 * 118 and +14.6 for 120, and -5.2 for 114. With the sections held at their top f0 and faded to the input, but
 * without the added damping, h31 read 2.3 dB under at 114 and 116.)
 *
 * FREQ 110 and 112, where the sections are still moving up to that f0, are within 4 dB: there the fade's share
 * of the input (-21.5 dB at 110) is already as loud as PG-8X's top harmonic through its filter (-21.2).
 * (Before #151: -9.8 dB at h37 for 112.)
 */
TT_TEST(vcf_top_reads_as_pg8x_at_44k1)
{
    static const vt_row low[] = {
        { 110, 12375, { -0.93, -4.34, -10.66, -17.00, -21.15 } },
        { 112, 12649, { -0.67, -2.96, -7.55, -12.76, -16.43 } },
    };
    static const vt_row pg[] = {
        { 114, 12923, { -0.49, -2.01, -5.17, -9.13, -12.15 } },
        { 116, 13199, { -0.36, -1.38, -3.45, -6.22, -8.50 } },
        { 118, 13475, { -0.26, -0.94, -2.26, -4.05, -5.60 } },
        { 120, 13752, { -0.18, -0.63, -1.44, -2.52, -3.46 } },
        { 122, 14030, { -0.12, -0.40, -0.87, -1.47, -1.98 } },
        { 124, 14309, { -0.06, -0.21, -0.46, -0.74, -0.97 } },
    };
    double wl = vt_worst(44100, low, (int)(sizeof low / sizeof low[0]));
    double worst = vt_worst(44100, pg, (int)(sizeof pg / sizeof pg[0]));
    printf("    worst %.2f dB at FREQ 110 and 112, %.2f dB from 114\n", wl, worst);
    TT_CHECK(wl < 4.0);
    TT_CHECK(worst < 1.5);
}

/*
 * The same at 48 kHz, against PG-8X rendered at 48 kHz: the top is fitted at both rates with one set of
 * constants. (Before #151: +10.3 dB at h31 for FREQ 120, -10.4 at h37 for 112.)
 */
TT_TEST(vcf_top_reads_as_pg8x_at_48k)
{
    static const vt_row low[] = {
        { 110, 12375, { -0.90, -4.40, -10.95, -17.49, -21.72 } },
        { 112, 12649, { -0.64, -2.98, -7.79, -13.23, -17.01 } },
    };
    static const vt_row pg[] = {
        { 114, 12923, { -0.47, -2.02, -5.34, -9.54, -12.70 } },
        { 116, 13199, { -0.34, -1.37, -3.56, -6.54, -8.98 } },
        { 118, 13475, { -0.24, -0.93, -2.32, -4.28, -5.98 } },
        { 120, 13752, { -0.17, -0.62, -1.47, -2.66, -3.73 } },
        { 122, 14030, { -0.11, -0.38, -0.88, -1.55, -2.14 } },
        { 124, 14309, { -0.06, -0.21, -0.46, -0.77, -1.05 } },
    };
    double wl = vt_worst(48000, low, (int)(sizeof low / sizeof low[0]));
    double worst = vt_worst(48000, pg, (int)(sizeof pg / sizeof pg[0]));
    printf("    worst %.2f dB at FREQ 110 and 112, %.2f dB from 114\n", wl, worst);
    TT_CHECK(wl < 4.0);
    TT_CHECK(worst < 1.5);
}

/*
 * A little resonance keeps the top harmonic. At resonance 290 and 340 per mille (VCF RES 16 and 20) and VCF
 * FREQ 114, PG-8X's h37 reads -8.94 and -8.33 dB (44.1 kHz, re the open RES 0 filter). The sections' own
 * shortfall there is 11 to 12 dB (the resonant top, #168). The long fade to the input must not add to it: a
 * few per cent of the input against the sections' output, out of phase past f0, took h37 to 17.2 and 23.8 dB
 * under PG-8X when the fade was handed over as slowly as the f0 stop. So the fade is resonance 0's alone
 * (gone by about resonance 100), and no harmonic read here is more than 13 dB under PG-8X's, or 6 over.
 */
TT_TEST(vcf_top_low_resonance_keeps_its_top_harmonic)
{
    static const struct { int32_t res; double pg[VT_HARMONICS]; } row[] = {
        { 290, { +0.91, +2.23, +0.81, -4.47, -8.94 } },
        { 340, { +1.18, +3.23, +2.41, -3.41, -8.33 } },
    };
    size_t r;
    int i;
    for (r = 0; r < sizeof row / sizeof row[0]; r++) {
        double db[VT_HARMONICS], under = 0, over = 0;
        vt_response(44100, 12923, row[r].res, db);
        printf("    resonance %d, VCF FREQ 114:", (int)row[r].res);
        for (i = 0; i < VT_HARMONICS; i++) {
            printf(" h%d %+.2f (%+.2f)", vt_k[i], db[i], row[r].pg[i]);
            under = fmax(under, row[r].pg[i] - db[i]);
            over = fmax(over, db[i] - row[r].pg[i]);
        }
        printf("\n    most under PG-8X %.2f dB, most over %.2f\n", under, over);
        TT_CHECK(under < 13.0);
        TT_CHECK(over < 6.0);
    }
}

static void run_vcf_top_tests(void)
{
    TT_RUN(vcf_top_opens_one_way_at_resonance_0);
    TT_RUN(vcf_top_other_rates_keep_the_top_they_had);
    TT_RUN(vcf_top_reads_as_pg8x_at_44k1);
    TT_RUN(vcf_top_reads_as_pg8x_at_48k);
    TT_RUN(vcf_top_low_resonance_keeps_its_top_harmonic);
}
