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

/*
 * At resonance 0 the top opens one way, as PG-8X's does: from VCF FREQ 108 to 127 each harmonic of PG-8X's saw
 * rises with every step and none goes over its open level (at 44.1 kHz h37, 19.4 kHz, reads -26.2 dB at 108,
 * -16.4 at 112, -8.5 at 116, -3.5 at 120, -1.0 at 124 and 0 at 127). So here, at the FM-1's rate: up the
 * cutoff from 12100 (FREQ 108) to open in 100-cent steps, no harmonic falls by more than 0.3 dB a step or
 * reads more than 0.3 dB over the open filter. (Before #151 the sections ran on to 0.425 of the rate, and h37
 * read +14.6 dB over open at 13752, FREQ 120.)
 */
TT_TEST(vcf_top_opens_one_way_at_resonance_0)
{
    double prev[VT_HARMONICS], db[VT_HARMONICS], over = -1e9, fall = -1e9;
    int32_t c, over_at = 0, fall_at = 0;
    int i;
    for (c = 12100; c <= FM_FILTER_OPEN_CENTS; c += 100) {
        vt_response(44100, c, 0, db);
        for (i = 0; i < VT_HARMONICS; i++) {
            if (db[i] > over) over = db[i], over_at = c;
            if (c > 12100 && prev[i] - db[i] > fall) fall = prev[i] - db[i], fall_at = c;
            prev[i] = db[i];
        }
    }
    printf("    resonance 0, 12100 to open: most over the open filter %+.2f dB (at %d), largest fall a step %.2f dB "
           "(at %d)\n", over, (int)over_at, fall, (int)fall_at);
    TT_CHECK(over < 0.3);
    TT_CHECK(fall < 0.3);
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

static void run_vcf_top_tests(void)
{
    TT_RUN(vcf_top_opens_one_way_at_resonance_0);
    TT_RUN(vcf_top_reads_as_pg8x_at_44k1);
    TT_RUN(vcf_top_reads_as_pg8x_at_48k);
}
