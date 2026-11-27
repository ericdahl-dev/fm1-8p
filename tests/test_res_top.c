/*
 * The top of the cutoff range with resonance (#168), against PG-8X 2.0.13.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * PG-8X figures: PG-8X 2.0.13 under Wine, SELFNOISE 0 (param 56), HPF 1 (ours: highpass step 0), chorus off,
 * DCO-1 saw alone, a C3 held from 0.2 s, rendered at the rate named. A C3 has a harmonic every 131 Hz, so a
 * resonant peak is read at its top within 0.4 dB. The harmonics are read over 0.7-2.7 s at the render's
 * measured f0 (130.72 Hz: PG-8X plays a few cents flat), each as its level minus the same harmonic's at VCF
 * FREQ 127, RES 0: the filter's response alone, the source divided out. The rows here are that response read
 * at the engine's own harmonics (k x 130.81 Hz, linear between PG-8X's), its mean over harmonics 1 to 6 (the
 * passband: the level cut resonance makes) and the peak's height over that, to 20 kHz
 * (fm1-8p-ref/pg8x/match/168/map.py and pins.py, 2026-11-27). Ours is read the same way, re the open filter.
 *
 * What PG-8X does up there: its passband depends on the resonance alone, at every cutoff (RES 64 -4.6 dB, RES
 * 127 -20.0, at VCF FREQ 127 too: it never becomes a wire with resonance); its peak grows 5 dB from FREQ 100
 * to 122 and then leaves the band upward; it reads the same in Hz at 44.1 and 48 kHz (within 0.3 dB rms); it is
 * linear (the same at MIX 127, 64, 32 and 16) and does not self-oscillate.
 */

#define RT_DFT_N 16384
#define RT_SKIP 4800
#define RT_HMAX 152                 /* C3's harmonics to 19.9 kHz */
static int16_t rt_buf[2 * (RT_SKIP + RT_DFT_N)];
static fm_engine rt_eng, rt_eng2;

/* The harmonics a row pins: 1.0, 3.1, 5.2, 7.3, 9.4, 11.5, 13.6, 15.7, 17.8 and 19.9 kHz at C3. */
#define RT_PINS 10
static const int rt_k[RT_PINS] = { 8, 24, 40, 56, 72, 88, 104, 120, 136, 152 };

static void rt_setup(fm_engine *e, uint32_t rate, int32_t cutoff, int32_t resonance)
{
    fm_engine_init(e, rate, 4);
    fm_engine_set_param(e, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(e, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(e, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(e, FM_PARAM_OSC1_WAVE, FM_WAVE_SAW);
    fm_engine_set_param(e, FM_PARAM_FILTER_RESONANCE, resonance);
    fm_engine_set_param(e, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff);
}

/* The level of each of a held C3 saw's harmonics 1..RT_HMAX through the filter, dB (amp[k - 1]). */
static void rt_levels(uint32_t rate, int32_t cutoff, int32_t resonance, double *db)
{
    int k;
    rt_setup(&rt_eng, rate, cutoff, resonance);
    fm_engine_note_on(&rt_eng, 48, 100);
    fm_engine_render(&rt_eng, rt_buf, RT_SKIP + RT_DFT_N);
    for (k = 1; k <= RT_HMAX; k++)
        db[k - 1] = 20.0 * log10(tone_amp(rt_buf + 2 * RT_SKIP, RT_DFT_N, 0, k * note_hz(48), rate) + 1e-9);
}

/* The filter's response at every harmonic, dB: rt_levels re the open filter's at `rate` (kept from call to
 * call). Returns the passband (harmonics 1..6) and, in *peak, the tallest harmonic over it. */
static double rt_response(uint32_t rate, int32_t cutoff, int32_t resonance, double *db, double *peak)
{
    static double open[RT_HMAX];
    static uint32_t open_rate;
    double pb = 0, top = -1e9;
    int k;
    if (open_rate != rate) {
        rt_levels(rate, FM_FILTER_OPEN_CENTS, 0, open);
        open_rate = rate;
    }
    rt_levels(rate, cutoff, resonance, db);
    for (k = 0; k < RT_HMAX; k++) {
        db[k] -= open[k];
        if (k < 6) pb += db[k] / 6;
        if (db[k] > top) top = db[k];
    }
    if (peak) *peak = top - pb;
    return pb;
}

/* A PG-8X row: VCF FREQ, VCF RES, the engine's resonance and cutoff for them, PG-8X's passband, its peak over
 * the passband, and its response at rt_k, dB. */
typedef struct { int freq, res; int32_t permille, cut; double pb, peak, db[RT_PINS]; } rt_row;

/* Ours against the rows at `rate`: the worst passband, peak and pinned-harmonic error, dB. Prints each row. */
static void rt_against(uint32_t rate, const rt_row *row, int count, double *wpb, double *wpeak, double *wh)
{
    static double db[RT_HMAX];
    int r, i;
    *wpb = *wpeak = *wh = 0;
    for (r = 0; r < count; r++) {
        double peak, pb = rt_response(rate, row[r].cut, row[r].permille, db, &peak), w = 0;
        for (i = 0; i < RT_PINS; i++) w = fmax(w, fabs(db[rt_k[i] - 1] - row[r].db[i]));
        printf("    %u Hz, FREQ %d RES %d: passband %+.2f (%+.2f), peak %.2f (%.2f), worst harmonic off %.2f dB\n",
               (unsigned)rate, row[r].freq, row[r].res, pb, row[r].pb, peak, row[r].peak, w);
        *wpb = fmax(*wpb, fabs(pb - row[r].pb));
        *wpeak = fmax(*wpeak, fabs(peak - row[r].peak));
        *wh = fmax(*wh, w);
    }
}

/* PG-8X at 44.1 kHz (the header's method). */
static const rt_row rt_pg44[] = {
    { 112, 16, 290, 12649, -2.95, 2.17, { -2.89, -2.28, -1.33, -0.78, -1.60, -4.13, -7.66, -11.38, -14.89, -18.08 } },
    { 116, 16, 290, 13199, -2.96, 2.53, { -2.92, -2.54, -1.87, -1.09, -0.52, -0.54, -1.40, -3.05, -5.22, -7.61 } },
    { 120, 16, 290, 13752, -2.97, 3.31, { -2.94, -2.69, -2.23, -1.60, -0.91, -0.26, 0.19, 0.34, 0.10, -0.55 } },
    { 122, 16, 290, 14030, -2.97, 3.95, { -2.95, -2.74, -2.34, -1.79, -1.15, -0.48, 0.14, 0.63, 0.92, 0.97 } },
    { 124, 16, 290, 14309, -2.97, 4.47, { -2.95, -2.77, -2.43, -1.94, -1.36, -0.72, -0.07, 0.54, 1.07, 1.48 } },
    { 127, 16, 290, 14400, -2.97, 4.38, { -2.95, -2.80, -2.51, -2.10, -1.59, -1.02, -0.42, 0.20, 0.80, 1.38 } },
    { 112, 64, 400, 12649, -4.56, 4.82, { -4.47, -3.52, -1.82, -0.10, -0.04, -2.74, -7.06, -11.53, -15.59, -19.16 } },
    { 116, 64, 400, 13199, -4.57, 5.39, { -4.52, -3.94, -2.85, -1.40, 0.03, 0.81, 0.26, -1.69, -4.47, -7.51 } },
    { 120, 64, 400, 13752, -4.58, 6.44, { -4.54, -4.18, -3.47, -2.46, -1.26, -0.00, 1.09, 1.77, 1.78, 1.05 } },
    { 122, 64, 400, 14030, -4.58, 7.22, { -4.55, -4.25, -3.66, -2.83, -1.80, -0.66, 0.50, 1.54, 2.30, 2.63 } },
    { 124, 64, 400, 14309, -4.58, 7.37, { -4.56, -4.30, -3.81, -3.10, -2.23, -1.22, -0.15, 0.92, 1.92, 2.75 } },
    { 127, 64, 400, 14400, -4.58, 6.50, { -4.56, -4.36, -3.96, -3.39, -2.68, -1.86, -0.96, -0.02, 0.93, 1.86 } },
    { 112, 96, 795, 12649, -11.38, 17.90, { -11.23, -9.60, -6.04, 0.04, 6.40, 0.86, -8.60, -15.82, -21.31, -25.68 } },
    { 116, 96, 795, 13199, -11.40, 18.83, { -11.31, -10.34, -8.33, -5.08, -0.35, 5.37, 6.98, 1.11, -5.76, -11.39 } },
    { 120, 96, 795, 13752, -11.41, 20.27, { -11.35, -10.75, -9.52, -7.63, -5.01, -1.57, 2.70, 7.09, 8.79, 5.74 } },
    { 122, 96, 795, 14030, -11.41, 21.15, { -11.37, -10.88, -9.89, -8.40, -6.37, -3.77, -0.55, 3.27, 7.24, 9.70 } },
    { 124, 96, 795, 14309, -11.42, 18.54, { -11.38, -10.97, -10.16, -8.96, -7.35, -5.33, -2.88, 0.03, 3.37, 6.93 } },
    { 127, 96, 795, 14400, -11.42, 13.12, { -11.39, -11.07, -10.45, -9.53, -8.34, -6.87, -5.16, -3.18, -0.94, 1.56 } },
    { 112, 127, 935, 12649, -19.93, 34.16, { -19.76, -17.93, -13.71, -5.18, 13.18, -2.61, -16.25, -24.41, -30.23, -34.74 } },
    { 116, 127, 935, 13199, -19.95, 35.20, { -19.85, -18.76, -16.43, -12.47, -5.85, 6.43, 12.36, -3.94, -13.69, -20.19 } },
    { 120, 127, 935, 13752, -19.96, 36.69, { -19.90, -19.21, -17.81, -15.59, -12.37, -7.75, -0.79, 10.49, 15.21, 2.48 } },
    { 122, 127, 935, 14030, -19.96, 37.63, { -19.91, -19.36, -18.23, -16.50, -14.07, -10.79, -6.32, 0.04, 9.72, 17.61 } },
    { 124, 127, 935, 14309, -19.97, 28.27, { -19.92, -19.47, -18.55, -17.15, -15.26, -12.79, -9.64, -5.55, -0.05, 7.78 } },
    { 127, 127, 935, 14400, -19.97, 16.79, { -19.94, -19.58, -18.88, -17.83, -16.44, -14.71, -12.61, -10.10, -7.09, -3.41 } },
};
#define RT_PG44_N ((int)(sizeof rt_pg44 / sizeof rt_pg44[0]))

/* PG-8X at 48 kHz. */
static const rt_row rt_pg48[] = {
    { 112, 16, 290, 12649, -2.95, 2.18, { -2.89, -2.26, -1.31, -0.77, -1.69, -4.37, -8.03, -11.85, -15.43, -18.64 } },
    { 116, 16, 290, 13199, -2.96, 2.51, { -2.92, -2.54, -1.85, -1.07, -0.52, -0.61, -1.59, -3.39, -5.70, -8.19 } },
    { 120, 16, 290, 13752, -2.97, 3.22, { -2.94, -2.69, -2.22, -1.59, -0.89, -0.26, 0.16, 0.24, -0.11, -0.89 } },
    { 122, 16, 290, 14030, -2.97, 3.82, { -2.95, -2.74, -2.34, -1.79, -1.15, -0.48, 0.13, 0.60, 0.84, 0.80 } },
    { 124, 16, 290, 14309, -2.97, 4.40, { -2.95, -2.77, -2.43, -1.95, -1.36, -0.72, -0.07, 0.54, 1.05, 1.44 } },
    { 127, 16, 290, 14400, -2.97, 4.38, { -2.95, -2.81, -2.52, -2.11, -1.61, -1.03, -0.42, 0.21, 0.83, 1.42 } },
    { 112, 64, 400, 12649, -4.56, 4.80, { -4.47, -3.51, -1.79, -0.09, -0.12, -2.99, -7.45, -12.01, -16.11, -19.69 } },
    { 116, 64, 400, 13199, -4.57, 5.32, { -4.52, -3.94, -2.84, -1.38, 0.04, 0.75, 0.05, -2.07, -4.99, -8.11 } },
    { 120, 64, 400, 13752, -4.58, 6.30, { -4.54, -4.18, -3.47, -2.46, -1.25, 0.00, 1.08, 1.68, 1.56, 0.67 } },
    { 122, 64, 400, 14030, -4.58, 7.04, { -4.55, -4.25, -3.67, -2.83, -1.80, -0.65, 0.51, 1.52, 2.23, 2.46 } },
    { 124, 64, 400, 14309, -4.58, 7.30, { -4.56, -4.31, -3.82, -3.12, -2.24, -1.23, -0.14, 0.94, 1.92, 2.72 } },
    { 127, 64, 400, 14400, -4.58, 6.50, { -4.56, -4.36, -3.97, -3.41, -2.70, -1.88, -0.97, -0.01, 0.97, 1.92 } },
    { 112, 96, 795, 12649, -11.38, 17.80, { -11.23, -9.60, -6.04, 0.07, 6.34, 0.51, -9.00, -16.22, -21.71, -26.09 } },
    { 116, 96, 795, 13199, -11.40, 18.68, { -11.31, -10.35, -8.34, -5.08, -0.33, 5.38, 6.70, 0.60, -6.31, -11.92 } },
    { 120, 96, 795, 13752, -11.41, 20.03, { -11.35, -10.76, -9.54, -7.65, -5.03, -1.56, 2.74, 7.09, 8.49, 5.11 } },
    { 122, 96, 795, 14030, -11.41, 20.91, { -11.37, -10.88, -9.91, -8.43, -6.40, -3.79, -0.54, 3.33, 7.28, 9.49 } },
    { 124, 96, 795, 14309, -11.42, 18.45, { -11.38, -10.98, -10.19, -8.99, -7.39, -5.37, -2.89, 0.06, 3.46, 7.04 } },
    { 127, 96, 795, 14400, -11.42, 13.06, { -11.39, -11.08, -10.47, -9.57, -8.39, -6.93, -5.21, -3.20, -0.92, 1.66 } },
    { 112, 127, 935, 12649, -19.93, 34.06, { -19.76, -17.93, -13.73, -5.19, 13.18, -2.96, -16.56, -24.72, -30.55, -35.08 } },
    { 116, 127, 935, 13199, -19.95, 35.00, { -19.85, -18.77, -16.46, -12.51, -5.87, 6.52, 11.88, -4.42, -14.11, -20.59 } },
    { 120, 127, 935, 13752, -19.96, 36.42, { -19.90, -19.22, -17.84, -15.64, -12.43, -7.79, -0.76, 10.67, 14.61, 1.76 } },
    { 122, 127, 935, 14030, -19.96, 37.35, { -19.91, -19.37, -18.26, -16.55, -14.14, -10.85, -6.35, 0.10, 9.97, 17.22 } },
    { 124, 127, 935, 14309, -19.97, 28.00, { -19.92, -19.48, -18.58, -17.20, -15.33, -12.87, -9.70, -5.57, 0.04, 8.08 } },
    { 127, 127, 935, 14400, -19.97, 16.61, { -19.94, -19.59, -18.91, -17.88, -16.52, -14.80, -12.70, -10.18, -7.11, -3.34 } },
};
#define RT_PG48_N ((int)(sizeof rt_pg48 / sizeof rt_pg48[0]))

/*
 * The level cut resonance makes holds all the way up: PG-8X's passband is the resonance's alone at every
 * cutoff (RES 64: -4.56..-4.58 dB from VCF FREQ 112 to 127; RES 127: -19.93..-19.97). Here, at the FM-1's
 * rate, the passband at cutoffs 13752, 14030, 14309 and open (FREQ 120, 122, 124 and 125 up) is within 0.5 dB
 * of the passband at 12649 (FREQ 112), at resonance 400 and 935. (Before #168 the output faded to the input
 * over the top 600 cents: the passband rose 5.0 dB by open at resonance 400 and 21.9 at 935, most of it
 * between FREQ 120 and 123: the level jump of #168.)
 */
TT_TEST(res_top_level_cut_holds_to_open)
{
    static const int32_t res[] = { 400, 935 }, cut[] = { 13752, 14030, 14309, FM_FILTER_OPEN_CENTS };
    static double db[RT_HMAX];
    size_t r, c;
    for (r = 0; r < sizeof res / sizeof res[0]; r++) {
        double ref = rt_response(44100, 12649, res[r], db, NULL), worst = 0;
        for (c = 0; c < sizeof cut / sizeof cut[0]; c++)
            worst = fmax(worst, fabs(rt_response(44100, cut[c], res[r], db, NULL) - ref));
        printf("    resonance %d: passband %+.2f dB at 12649, moves at most %.2f dB up to open\n", (int)res[r], ref, worst);
        TT_CHECK(worst < 0.5);
    }
}

/*
 * At the FM-1's rate the top reads as PG-8X's with resonance: VCF FREQ 112 to 127 at VCF RES 16, 64, 96 and
 * 127 (resonance 290, 400, 795, 935), PG-8X rendered at 44.1 kHz. The passband within 0.5 dB, the peak's
 * height within 2 dB, and every pinned harmonic (1 to 19.9 kHz) within 3.5 dB. The open cutoff (14400, VCF
 * FREQ 124.65 and everything over it) stands for PG-8X's FREQ 127, where PG-8X's own sum of cutoff sources
 * stops, so FREQ 125 and 126 play FREQ 127's response.
 * (Before #168: the peak 51.3 dB at FREQ 120, RES 127, against PG-8X's 36.7: section 2's zeros at the origin
 * lift the peak by 1 + t^2, 18.7 dB there; the passband 1.9 dB low from FREQ 112 to 120 and 20 dB high at 124;
 * no peak at all from 124.)
 */
TT_TEST(res_top_reads_as_pg8x_at_44k1)
{
    double pb, peak, h;
    rt_against(44100, rt_pg44, RT_PG44_N, &pb, &peak, &h);
    printf("    44.1 kHz: worst passband %.2f dB, worst peak height %.2f, worst pinned harmonic %.2f\n", pb, peak, h);
    TT_CHECK(pb < 0.5);
    TT_CHECK(peak < 2.0);
    TT_CHECK(h < 3.5);
}

/* The same at 48 kHz, against PG-8X rendered at 48 kHz: its own table set. */
TT_TEST(res_top_reads_as_pg8x_at_48k)
{
    double pb, peak, h;
    rt_against(48000, rt_pg48, RT_PG48_N, &pb, &peak, &h);
    printf("    48 kHz: worst passband %.2f dB, worst peak height %.2f, worst pinned harmonic %.2f\n", pb, peak, h);
    TT_CHECK(pb < 0.5);
    TT_CHECK(peak < 2.0);
    TT_CHECK(h < 3.5);
}

/*
 * Between the two fitted rates the top reads as PG-8X's too: PG-8X's response is the same in Hz at 44.1 and 48
 * kHz (within 0.3 dB rms), so at 46 kHz ours is held against the 44.1 kHz rows of VCF RES 64 and 127, with the
 * two table sets read linearly between by the rate. The peak's height within 2.5 dB, the passband within 0.5.
 */
TT_TEST(res_top_reads_as_pg8x_between_the_rates)
{
    double pb, peak, h, pb2, peak2, h2;
    rt_against(46000, rt_pg44 + 6, 6, &pb, &peak, &h);
    rt_against(46000, rt_pg44 + 18, 6, &pb2, &peak2, &h2);
    printf("    46 kHz: worst passband %.2f dB, worst peak height %.2f, worst pinned harmonic %.2f\n", fmax(pb, pb2),
           fmax(peak, peak2), fmax(h, h2));
    TT_CHECK(fmax(pb, pb2) < 0.5);
    TT_CHECK(fmax(peak, peak2) < 2.5);
    TT_CHECK(fmax(h, h2) < 4.5);
}

/*
 * No notch above the peak. At VCF FREQ 114 PG-8X's response falls smoothly past its peak (RES 32: -5.85 dB at
 * 15.7 kHz, -10.17 at 17.8, -14.05 at 19.9). Before #168 section 1's zeros, a complex pair on their way from
 * the origin to Nyquist, crossed the top band there (at 16.6 kHz with a Q of 1.2): ours read 27.3 dB under
 * PG-8X's at 20 kHz for RES 32 and 26.9 for RES 80. Now they are a real double zero, which has no notch
 * wherever it sits: no harmonic from 13.6 kHz up is more than 3 dB under PG-8X's.
 */
TT_TEST(res_top_has_no_notch_above_the_peak)
{
    static const rt_row row[] = {
        { 114, 32, 500, 12923, -5.93, 7.59, { -5.84, -4.95, -3.20, -0.83, 1.27, 1.31, -1.56, -5.85, -10.17, -14.05 } },
        { 114, 64, 400, 12923, -4.57, 5.06, { -4.49, -3.76, -2.39, -0.73, 0.42, -0.08, -2.54, -6.08, -9.78, -13.25 } },
        { 114, 80, 575, 12923, -7.15, 9.94, { -7.05, -6.04, -4.01, -1.08, 1.93, 2.54, -0.83, -5.90, -10.75, -14.95 } },
    };
    static double db[RT_HMAX];
    size_t r;
    int i;
    for (r = 0; r < sizeof row / sizeof row[0]; r++) {
        double under = 0;
        rt_response(44100, row[r].cut, row[r].permille, db, NULL);
        for (i = 6; i < RT_PINS; i++) under = fmax(under, row[r].db[i] - db[rt_k[i] - 1]);
        printf("    FREQ 114 RES %d: most under PG-8X's from 13.6 kHz up %.2f dB\n", row[r].res, under);
        TT_CHECK(under < 3.0);
    }
}

/*
 * A little resonance gets the top too, from resonance 112 (VCF RES 5): PG-8X keeps its level cut and a small
 * rise at the very top there as well (RES 8 at FREQ 127: passband -1.40 dB, +0.72 at 19.9 kHz; RES 6 at FREQ
 * 120: -1.03, -2.55). Before #168 ours was a wire at FREQ 127 (0.0 dB) and read 10.7 dB under at 20 kHz for
 * RES 6, FREQ 120. Each pinned harmonic within 1 dB.
 */
TT_TEST(res_top_holds_at_low_resonance)
{
    static const rt_row row[] = {
        { 127, 8, 180, 14400, -1.40, 2.14, { -1.40, -1.31, -1.16, -0.94, -0.68, -0.39, -0.10, 0.19, 0.47, 0.72 } },
        { 120, 6, 135, 13752, -1.03, 0.22, { -1.03, -0.98, -0.90, -0.83, -0.81, -0.88, -1.08, -1.42, -1.92, -2.55 } },
    };
    double pb, peak, h;
    rt_against(44100, row, 2, &pb, &peak, &h);
    TT_CHECK(pb < 0.5);
    TT_CHECK(h < 1.0);
}

/* The engine's output level per 2048-frame stretch as the cutoff rises from 12100 to open at 1700 cents a
 * second (set every 64 frames) and then holds, a C3 saw at `resonance`: the largest rise between neighbors, dB. */
static double rt_sweep_rise(int32_t resonance)
{
    enum { BLOCK = 64, FRAME = 2048, FRAMES = 36 };
    static int16_t out[2 * BLOCK];
    double prev = 0, rise = 0;
    int f, b, i;
    long n = 0;
    rt_setup(&rt_eng, 44100, 12100, resonance);
    fm_engine_note_on(&rt_eng, 48, 100);
    for (f = 0; f < FRAMES; f++) {
        double s = 0, db;
        for (b = 0; b < FRAME / BLOCK; b++, n += BLOCK) {
            long c = 12100 + (n > 8820 ? (n - 8820) * 1700 / 44100 : 0);
            fm_engine_set_param(&rt_eng, FM_PARAM_FILTER_CUTOFF_CENTS, c > FM_FILTER_OPEN_CENTS ? FM_FILTER_OPEN_CENTS : (int32_t)c);
            fm_engine_render(&rt_eng, out, BLOCK);
            for (i = 0; i < BLOCK; i++) s += (double)out[2 * i] * out[2 * i];
        }
        db = 10.0 * log10(s / FRAME + 1e-9);
        if (f > 4 && db - prev > rise) rise = db - prev;
        prev = db;
    }
    return rise;
}

/*
 * A sweep through the top has no level jump. PG-8X's level through an envelope sweep to the top holds (RES
 * 64) or glides down as the peak leaves the band (RES 127: -41.9 to -43.3 dBFS over 0.7 s), at most 0.04 dB
 * up between 23 ms frames at RES 64. Here the cutoff rises from 12100 to open at 1700 cents a second: the
 * level never rises more than 0.5 dB between 46 ms stretches. (Before #168: 1.1 dB at resonance 400 and 2.2
 * at 935 in one stretch, 4.5 and 14 dB in all, as the fade let the input through.)
 */
TT_TEST(res_top_sweep_has_no_level_jump)
{
    static const int32_t res[] = { 400, 935 };
    size_t r;
    for (r = 0; r < sizeof res / sizeof res[0]; r++) {
        double rise = rt_sweep_rise(res[r]);
        printf("    resonance %d: largest rise between 46 ms stretches %.2f dB\n", (int)res[r], rise);
        TT_CHECK(rise < 0.5);
    }
}

/*
 * The peak at the top is no taller than PG-8X's, which is the headroom: PG-8X's largest gain at any harmonic to
 * 20 kHz, RES 127, is +17.7 dB re the input (FREQ 122). Here, resonance 935, cutoffs 13200 to open: under +20
 * dB. (Before #168: +30.2 dB at FREQ 121, into the filter's hold and the output's soft clip.)
 */
TT_TEST(res_top_gain_is_bounded)
{
    static double db[RT_HMAX];
    double top = -1e9;
    int32_t c;
    int k;
    for (c = 13200; c <= FM_FILTER_OPEN_CENTS; c += 150) {
        rt_response(44100, c, 935, db, NULL);
        for (k = 0; k < RT_HMAX; k++) top = fmax(top, db[k]);
    }
    printf("    resonance 935, cutoffs 13200..14400: the largest gain at a harmonic %+.2f dB\n", top);
    TT_CHECK(top < 20.0);
}

/*
 * Past VCF RES 127 (resonance 935, the most a JX-8P patch reaches; the RES knob and a CC go on to 1000) the
 * top's damping falls on as the table's own does, k x k(resonance) / k(935), so the peak grows on as it does
 * lower down: at resonance 1000 it stands at least 44 dB over the passband at cutoffs 12649 and 13752 (935:
 * 34.6 and 37.1), and the largest gain stays under +25 dB re the input.
 */
TT_TEST(res_top_goes_on_past_res_127)
{
    static const int32_t cut[] = { 12649, 13752 };
    static double db[RT_HMAX];
    size_t c;
    int k;
    for (c = 0; c < sizeof cut / sizeof cut[0]; c++) {
        double peak, pb = rt_response(44100, cut[c], 1000, db, &peak), top = -1e9;
        for (k = 0; k < RT_HMAX; k++) top = fmax(top, db[k]);
        printf("    resonance 1000, cutoff %d: passband %+.2f dB, peak %.2f dB over it, largest gain %+.2f\n", (int)cut[c], pb,
               peak, top);
        TT_CHECK(peak > 44.0);
        TT_CHECK(top < 25.0);
    }
}

/* RMS of n frames of the engine's left channel, dB. */
static double rt_rms_db(fm_engine *e, int16_t *buf, uint32_t n)
{
    double s = 0;
    uint32_t i;
    fm_engine_render(e, buf, n);
    for (i = 0; i < n; i++) s += (double)buf[2 * i] * buf[2 * i];
    return 10.0 * log10(s / n + 1e-9);
}

/*
 * A resonance change on a held note at an open cutoff is heard: with resonance the open cutoff is not a wire,
 * so the voice has to be told the filter now runs (fm_voice_set_resonance marks it). The default patch (open
 * filter), a held C3: resonance 0 -> 400 takes the level down by PG-8X's cut (4.6 dB; within 1 dB once it has
 * settled), and back to 0 brings it back within 0.2 dB. (With the filter changed but the voice not told, the
 * voice's fast path went on skipping the filter: no change at all, and the FM_CHECK_BUSY build asserted.)
 */
TT_TEST(res_top_resonance_change_at_open_is_heard)
{
    double a, b, c;
    rt_setup(&rt_eng, 44100, FM_FILTER_OPEN_CENTS, 0);
    fm_engine_note_on(&rt_eng, 48, 100);
    rt_rms_db(&rt_eng, rt_buf, 4410);
    a = rt_rms_db(&rt_eng, rt_buf, 13230);
    fm_engine_set_param(&rt_eng, FM_PARAM_FILTER_RESONANCE, 400);
    rt_rms_db(&rt_eng, rt_buf, 4410);
    b = rt_rms_db(&rt_eng, rt_buf, 13230);
    fm_engine_set_param(&rt_eng, FM_PARAM_FILTER_RESONANCE, 0);
    rt_rms_db(&rt_eng, rt_buf, 4410);
    c = rt_rms_db(&rt_eng, rt_buf, 13230);
    printf("    level at resonance 0, then 400, then 0 again: %.2f, %.2f, %.2f dB\n", a, b, c);
    TT_CHECK(fabs((a - b) - 4.6) < 1.0);
    TT_CHECK(fabs(a - c) < 0.2);
}

/*
 * A resonance turned into the handover on a held note arrives: the voice keeps its control points coming until
 * the handover is there (FM_BUSY_CUTOFF), with nothing else moving the cutoff. Two engines, a held C3 at an
 * open cutoff: one has resonance 112 from the start, the other is turned 91 -> 112 at 0.1 s (31 control
 * points to go: 11 ms). From 0.2 s on they are the same within 4 LSB. (With the control points not kept
 * coming, or the step not made, the second stays where it was: hundreds of LSB apart.)
 */
TT_TEST(res_top_turned_resonance_arrives)
{
    static int16_t a[2 * 4410], b[2 * 4410];
    int i, worst = 0;
    rt_setup(&rt_eng, 44100, FM_FILTER_OPEN_CENTS, 112);
    rt_setup(&rt_eng2, 44100, FM_FILTER_OPEN_CENTS, 91);
    fm_engine_note_on(&rt_eng, 48, 100);
    fm_engine_note_on(&rt_eng2, 48, 100);
    fm_engine_render(&rt_eng, a, 4410);
    fm_engine_render(&rt_eng2, b, 4410);
    fm_engine_set_param(&rt_eng2, FM_PARAM_FILTER_RESONANCE, 112);
    fm_engine_render(&rt_eng, a, 4410);
    fm_engine_render(&rt_eng2, b, 4410);
    fm_engine_render(&rt_eng, a, 4410);
    fm_engine_render(&rt_eng2, b, 4410);
    for (i = 0; i < 2 * 4410; i++) worst = abs(a[i] - b[i]) > worst ? abs(a[i] - b[i]) : worst;
    printf("    0.2 to 0.3 s: the largest difference %d LSB (of a peak of %d)\n", worst, peak_abs(a, 0, 4410));
    TT_CHECK(peak_abs(a, 0, 4410) > 1000);
    TT_CHECK(worst <= 4);
}

/*
 * The same between notes, on a voice that isn't sounding: the resonance is set at once, the handover with it,
 * and the voice is told whether the filter now runs. One voice, the default patch (open): a C3 played and let
 * go, resonance 0 -> 400 while nothing sounds, the C3 again: 4.6 dB down, within 1 dB. (Not told, the voice
 * would play the second note through its fast path, as a wire.)
 */
TT_TEST(res_top_resonance_set_between_notes_is_heard)
{
    double a, b;
    fm_engine_init(&rt_eng, 44100, 1);
    fm_engine_set_param(&rt_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&rt_eng, FM_PARAM_AMP_RELEASE_MS, 1);
    fm_engine_note_on(&rt_eng, 48, 100);
    rt_rms_db(&rt_eng, rt_buf, 4410);
    a = rt_rms_db(&rt_eng, rt_buf, 13230);
    fm_engine_note_off(&rt_eng, 48);
    rt_rms_db(&rt_eng, rt_buf, 13230);
    TT_CHECK(peak_abs(rt_buf, 8820, 13230) == 0);       /* the voice is silent */
    fm_engine_set_param(&rt_eng, FM_PARAM_FILTER_RESONANCE, 400);
    fm_engine_note_on(&rt_eng, 48, 100);
    rt_rms_db(&rt_eng, rt_buf, 4410);
    b = rt_rms_db(&rt_eng, rt_buf, 13230);
    printf("    the note at resonance 0, then again after resonance 400 was set in the silence: %.2f, %.2f dB\n", a, b);
    TT_CHECK(fabs((a - b) - 4.6) < 1.0);
}

/*
 * A handover left on its way when its voice fell silent is where it goes before the next note sounds: the
 * note does not start in the middle of it. One voice, twice: the first with resonance 112 from the start; the
 * second with resonance 91, turned to 112 on the sounding note 16 samples before it is let go (a control point
 * of the 31 it has to go). The amp envelope's release is 0, so the voice is silent at once and the handover
 * stops where it was: the next note, 0.1 s on, is the same in both within 4 LSB (the filter's states are not
 * cleared between notes, and the two got here by different roads). Started where the handover was left, it
 * is 1199 LSB apart (of a peak of 21182).
 */
TT_TEST(res_top_handover_is_there_when_a_silent_voice_starts)
{
    static int16_t a[2 * 4410], b[2 * 4410];
    fm_engine *e[2] = { &rt_eng, &rt_eng2 };
    int16_t *out[2] = { a, b };
    int i, worst = 0;
    for (i = 0; i < 2; i++) {
        fm_engine_init(e[i], 44100, 1);
        fm_engine_set_param(e[i], FM_PARAM_AMP_SUSTAIN, 1000);
        fm_engine_set_param(e[i], FM_PARAM_AMP_RELEASE_MS, 0);
        fm_engine_set_param(e[i], FM_PARAM_FILTER_RESONANCE, i == 0 ? 112 : 91);
        fm_engine_note_on(e[i], 48, 100);
        fm_engine_render(e[i], out[i], 4416 - 16);
        if (i == 1) fm_engine_set_param(e[i], FM_PARAM_FILTER_RESONANCE, 112);
        fm_engine_render(e[i], out[i], 16);
        fm_engine_note_off(e[i], 48);
        fm_engine_render(e[i], out[i], 4410);
        TT_CHECK(peak_abs(out[i], 2205, 4410) == 0);
        fm_engine_note_on(e[i], 55, 100);
        fm_engine_render(e[i], out[i], 4410);
    }
    TT_CHECK(peak_abs(a, 0, 4410) > 1000);
    for (i = 0; i < 2 * 4410; i++) worst = abs(a[i] - b[i]) > worst ? abs(a[i] - b[i]) : worst;
    printf("    the next note, the two ways: the largest difference %d LSB (of a peak of %d)\n", worst, peak_abs(a, 0, 4410));
    TT_CHECK(worst <= 4);
}

/* The step a resonance change r1 -> r2 makes at once on a held C3 at `cutoff`: two engines alike until one's
 * resonance changes; the largest difference at the first frame after it over 12 times in the saw's cycle, as a
 * share of the output's peak. */
static double rt_res_step(int32_t cutoff, int32_t r1, int32_t r2)
{
    enum { BLOCK = 64 };
    static int16_t a[2 * BLOCK], b[2 * BLOCK];
    double worst = 0, peak = 0;
    int q, n, i;
    for (q = 0; q < 12; q++) {
        int n0 = 13248 + BLOCK * q;
        rt_setup(&rt_eng, 44100, cutoff, r1);
        rt_setup(&rt_eng2, 44100, cutoff, r1);
        fm_engine_note_on(&rt_eng, 48, 100);
        fm_engine_note_on(&rt_eng2, 48, 100);
        for (n = 0; n <= n0; n += BLOCK) {
            if (n == n0) fm_engine_set_param(&rt_eng, FM_PARAM_FILTER_RESONANCE, r2);
            fm_engine_render(&rt_eng, a, BLOCK);
            fm_engine_render(&rt_eng2, b, BLOCK);
            for (i = 0; i < BLOCK; i++) peak = fmax(peak, fabs((double)b[2 * i]));
        }
        worst = fmax(worst, fabs((double)a[0] - b[0]));
    }
    return worst / peak;
}

/*
 * Turning RES on a held note makes no click of the top's own. From resonance 90 to 112 the top hands over
 * from the low-resonance one (#151) to this: at an open cutoff that is from a wire to PG-8X's level cut and
 * shelf, 22 per mille for all of it, and a RES knob step is 10. So on a sounding voice the handover's weight
 * moves no faster than 1/32 a control tick (16 samples: 12 ms for all of it), and the sections take their new
 * coefficients as fast as the old top is faded out, so what the weight moves is only the fade.
 * A knob step into, inside or out of the handover, at cutoffs open, 14100 and 13475: the output's first frame
 * after it is within 2 % of the output's peak of where it would have been. (The low-resonance top's own
 * worst knob step, before #168: 3.7 %, at 14100 from resonance 100 to 90. Without the limit: 4.8 % at an open
 * cutoff from 90 to 100, 8.6 % at 14100 from 100 to 90.) A move clear across the handover in one go is not
 * such a step: it goes at once (res_top_move_clear_across_the_handover_goes_at_once).
 */
TT_TEST(res_top_knob_step_makes_no_click)
{
    static const int32_t cut[] = { FM_FILTER_OPEN_CENTS, 14100, 13475 };
    static const int32_t step[][2] = { { 90, 100 }, { 100, 110 }, { 110, 120 }, { 120, 110 }, { 110, 100 }, { 100, 90 } };
    size_t c, s;
    for (c = 0; c < sizeof cut / sizeof cut[0]; c++) {
        double worst = 0;
        for (s = 0; s < sizeof step / sizeof step[0]; s++)
            worst = fmax(worst, rt_res_step(cut[c], step[s][0], step[s][1]));
        printf("    cutoff %d: the largest step a RES change through the handover makes, %.2f %% of the peak\n", (int)cut[c],
               100 * worst);
        TT_CHECK(worst < 0.02);
    }
}

/* A C3 saw at peak 1.0 (Q8.24), its phase kept in *ph. */
static int32_t rt_saw(double *ph)
{
    *ph += 130.8128 / 44100.0;
    if (*ph >= 1.0) *ph -= 1.0;
    return (int32_t)((2.0 * *ph - 1.0) * 16777216.0);
}

/*
 * The handover itself, at the filter, at an open cutoff. Resonance turned 0 -> 100 (into the handover) leaves
 * the wire a wire until the next control point, then takes FM_FILTER_UR_STEP a control point (16 samples); 100
 * -> 120 (out of it) takes it the rest of the way: 32 or 33 steps for all of it, each moving the output's next
 * sample by less than 0.8 % of the input's peak. The same back to a wire on 120 -> 100 -> 0. (Set, not turned,
 * it is there at once: fm_filter_set_resonance, for a voice that isn't sounding.)
 */
TT_TEST(res_top_handover_moves_a_step_a_control_point)
{
    static const int16_t via[2][2] = { { 100, 120 }, { 100, 0 } };
    static fm_filter f, g;
    int32_t rate = fm_filter_rate_cents(44100);
    double ph = 0.123, worst = 0;
    int dir, leg, n, steps;
    fm_filter_reset(&f);
    fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS, rate);
    for (n = 0; n < 4410; n++) fm_filter_run(&f, rt_saw(&ph));
    for (dir = 0; dir < 2; dir++) {
        steps = 0;
        for (leg = 0; leg < 2; leg++) {
            fm_filter_turn_resonance(&f, via[dir][leg]);
            TT_CHECK(fm_filter_handover_moving(&f));
            if (!leg) TT_CHECK(fm_filter_is_wire(&f) == !dir);  /* nothing yet: the coefficients' own change only */
            for (; fm_filter_handover_moving(&f) && steps < 1000; steps++) {
                int32_t x;
                g = f;
                TT_CHECK(fm_filter_handover_step(&f) & 1);
                x = rt_saw(&ph);
                worst = fmax(worst, fabs((double)fm_filter_run(&f, x) - (double)fm_filter_run(&g, x)) / 16777216.0);
                for (n = 1; n < 16; n++) fm_filter_run(&f, rt_saw(&ph));
            }
        }
        printf("    resonance %s at an open cutoff: %d control points\n", dir ? "120 -> 100 -> 0" : "0 -> 100 -> 120", steps);
        TT_CHECK(steps == 32 || steps == 33);            /* (33: the stop at 100 ends a step short) */
        TT_CHECK(fm_filter_handover_step(&f) == 0);
        TT_CHECK(fm_filter_is_wire(&f) == dir);
        for (n = 0; n < 4410; n++) fm_filter_run(&f, rt_saw(&ph));
    }
    printf("    the largest step of the output at a control point, %.2f %% of the input's peak\n", 100 * worst);
    TT_CHECK(worst < 0.008);
    fm_filter_reset(&f);
    fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS, rate);
    fm_filter_set_resonance(&f, 120);
    TT_CHECK(!fm_filter_handover_moving(&f));
    TT_CHECK(!fm_filter_is_wire(&f));
}

/*
 * A move clear across the handover in one go (from resonance 90 or under to 112 or over, or back) takes the
 * handover along at once, as it takes every other coefficient. A jump that size steps the output whatever the
 * handover does (0 -> 400 at cutoff 13475: 53 % of the peak on the low-resonance top alone, before #168), and a
 * patch whose resonance glides in over it is not held 32 control points between the two tops while its
 * cutoff moves: there every control point works out both tops, which cost the CPU gate's F rows 2.6 points of
 * a block for those 12 ms. A move that lands inside the handover or starts there is not clear across.
 */
TT_TEST(res_top_move_clear_across_the_handover_goes_at_once)
{
    static const int16_t move[][3] = { { 0, 400, 1 }, { 90, 112, 1 }, { 400, 0, 1 }, { 112, 90, 1 },
                                       { 90, 111, 0 }, { 91, 400, 0 }, { 400, 91, 0 }, { 111, 0, 0 } };
    static fm_filter f;
    size_t m;
    for (m = 0; m < sizeof move / sizeof move[0]; m++) {
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, move[m][0]);
        fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS, fm_filter_rate_cents(44100));
        fm_filter_turn_resonance(&f, move[m][1]);
        TT_CHECK(fm_filter_handover_moving(&f) == !move[m][2]);
    }
}

/* One hash over a1, a2, a3, t, z3, zt, fade and the level at `res`'s resonances, cutoffs `from`..open by 7. */
static uint32_t rt_coef_hash(uint32_t sample_rate, const int16_t *res, size_t count, int32_t from)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(sample_rate), c;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < count; i++)
        for (c = from; c <= FM_FILTER_OPEN_CENTS; c += 7) {
            int32_t v[8];
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, res[i]);
            fm_filter_set_cutoff(&f, c, rate);
            v[0] = f.a1, v[1] = f.a2, v[2] = f.a3, v[3] = f.t, v[4] = f.z3, v[5] = f.zt, v[6] = f.fade, v[7] = f.a;
            h = fnv1a(h, v, sizeof v);
        }
    return h;
}

/*
 * Up to resonance 90 the top is the low-resonance one (#151), coefficient for coefficient: one hash a rate over
 * resonances 0, 45 and 90, cutoffs 11000..14400 by 7. The values are the base's (e99473b, the same loop on its
 * filter.c).
 */
TT_TEST(res_top_leaves_the_low_resonance_top_as_it_was)
{
    static const struct { uint32_t rate, hash; } row[] = { { 44100, 0xC2443F4Au }, { 48000, 0x4272F6B7u } };
    static const int16_t res[] = { 0, 45, 90 };
    size_t r;
    for (r = 0; r < sizeof row / sizeof row[0]; r++) {
        uint32_t h = rt_coef_hash(row[r].rate, res, 3, 11000);
        printf("    %u Hz: coefficient hash 0x%08Xu\n", (unsigned)row[r].rate, (unsigned)h);
        TT_CHECK_EQ(h, row[r].hash);
    }
}

/*
 * Rates outside 44.1 to 48 kHz keep the top they had with resonance too (the tables are fitted at 44.1 and 48
 * kHz, and PG-8X's top is the same in Hz, not in shares of the rate): one hash a rate over resonances 180, 400
 * and 935, cutoffs 11000..14400 by 7, the base's values (e99473b).
 */
TT_TEST(res_top_is_as_before_outside_the_fitted_rates)
{
    static const struct { uint32_t rate, hash; } row[] = {
        { 22050, 0xE6F65E10u }, { 32000, 0x69DD4405u }, { 88200, 0x263D2511u }, { 96000, 0x82C7B44Cu },
    };
    static const int16_t res[] = { 180, 400, 935 };
    size_t r;
    for (r = 0; r < sizeof row / sizeof row[0]; r++) {
        uint32_t h = rt_coef_hash(row[r].rate, res, 3, 11000);
        printf("    %u Hz: coefficient hash 0x%08Xu\n", (unsigned)row[r].rate, (unsigned)h);
        TT_CHECK_EQ(h, row[r].hash);
    }
}

/*
 * No coefficient steps where it is heard, with the cutoff or with the resonance: every cent of cutoff
 * 11400..14400 and every per mille of resonance 0..1000, at 44.1, 46 and 48 kHz. The sections' coefficients (t,
 * z3, the level) are weighed by the fade, the sections' share of the output: at an open cutoff inside the
 * handover the sections are the resonant top's outright while the fade is still near 0, so there they move fast
 * and are not heard. The largest steps are where the top is squeezed: the last 91 cents of cutoff, which stand
 * for PG-8X's VCF FREQ 124 to 127, and the handover's 22 per mille (the fade 1/22 a per mille). A table read off
 * by a knot, a weight that jumps or a rate's set taken at the other rate steps far past these.
 */
TT_TEST(res_top_steps_no_coefficient)
{
    static const uint32_t rates[] = { 44100, 46000, 48000 };
    static fm_filter f;
    static int32_t prev[3001][4];
    size_t r;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        int32_t rate = fm_filter_rate_cents(rates[r]), res, c;
        double wc[4] = { 0, 0, 0, 0 }, wr[4] = { 0, 0, 0, 0 };
        static const double unit[4] = { 134217728.0, 1073741824.0, 1073741824.0, 1073741824.0 };
        int i;
        for (res = 0; res <= 1000; res++) {
            int32_t last[4] = { 0, 0, 0, 0 };
            for (c = 11400; c <= FM_FILTER_OPEN_CENTS; c++) {
                int32_t v[4];
                fm_filter_reset(&f);
                fm_filter_set_resonance(&f, res);
                fm_filter_set_cutoff(&f, c, rate);
                v[0] = f.t, v[1] = f.z3, v[2] = f.a, v[3] = f.fade;
                for (i = 0; i < 4; i++) {
                    double heard = i == 3 ? 1.0 : v[3] / 1073741824.0;
                    if (c > 11400) wc[i] = fmax(wc[i], heard * fabs((double)v[i] - last[i]) / unit[i]);
                    if (res) wr[i] = fmax(wr[i], heard * fabs((double)v[i] - prev[c - 11400][i]) / unit[i]);
                }
                for (i = 0; i < 4; i++) last[i] = prev[c - 11400][i] = v[i];
            }
        }
        printf("    %u Hz: largest step a cent of cutoff: t %.4f, z3 %.4f, level %.5f, fade %.4f; a per mille of "
               "resonance: t %.4f, z3 %.4f, level %.5f, fade %.4f\n", (unsigned)rates[r], wc[0], wc[1], wc[2], wc[3],
               wr[0], wr[1], wr[2], wr[3]);
        TT_CHECK(wc[0] < 0.02 && wc[1] < 0.015 && wc[2] < 0.0005 && wc[3] < 0.006);
        TT_CHECK(wr[0] < 0.15 && wr[1] < 0.09 && wr[2] < 0.008 && wr[3] < 0.05);
    }
}

static void run_res_top_tests(void)
{
    TT_RUN(res_top_level_cut_holds_to_open);
    TT_RUN(res_top_reads_as_pg8x_at_44k1);
    TT_RUN(res_top_reads_as_pg8x_at_48k);
    TT_RUN(res_top_reads_as_pg8x_between_the_rates);
    TT_RUN(res_top_has_no_notch_above_the_peak);
    TT_RUN(res_top_holds_at_low_resonance);
    TT_RUN(res_top_sweep_has_no_level_jump);
    TT_RUN(res_top_gain_is_bounded);
    TT_RUN(res_top_goes_on_past_res_127);
    TT_RUN(res_top_resonance_change_at_open_is_heard);
    TT_RUN(res_top_turned_resonance_arrives);
    TT_RUN(res_top_resonance_set_between_notes_is_heard);
    TT_RUN(res_top_handover_is_there_when_a_silent_voice_starts);
    TT_RUN(res_top_knob_step_makes_no_click);
    TT_RUN(res_top_handover_moves_a_step_a_control_point);
    TT_RUN(res_top_move_clear_across_the_handover_goes_at_once);
    TT_RUN(res_top_leaves_the_low_resonance_top_as_it_was);
    TT_RUN(res_top_is_as_before_outside_the_fitted_rates);
    TT_RUN(res_top_steps_no_coefficient);
}
