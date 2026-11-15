/* The lowpass as PG-8X 2.0.13's: two equal resonant 2-pole sections (#162, the resonance's width). Its
 * response at engine/filter.c's public interface against PG-8X's own, as fitted to its renders
 * (~/fm1-8p-ref/lvl/res, tools: resfit / qtable2 in the PR): each row's f0, Q and level re its open
 * render, fitted within 0.01..0.66 dB of PG-8X's measured response. */
#include "../engine/filter.h"

#define SVF_RATE 48000u

static const struct { int16_t permille, cut; float f0, q, level_db; } PG_ROWS[] = {
    { 0, 6769, 369.9f, 0.696f, +0.01f }, /* F49 byte 0: fit 0.01 dB */
    { 290, 6769, 369.4f, 0.980f, -2.96f }, /* F49 byte 16: fit 0.01 dB */
    { 500, 6769, 369.0f, 1.382f, -5.94f }, /* F49 byte 32: fit 0.01 dB */
    { 395, 6769, 369.2f, 1.175f, -4.53f }, /* F49 byte 48: fit 0.01 dB */
    { 400, 6769, 369.2f, 1.181f, -4.58f }, /* F49 byte 64: fit 0.01 dB */
    { 575, 6769, 368.9f, 1.590f, -7.16f }, /* F49 byte 80: fit 0.01 dB */
    { 690, 6769, 368.7f, 2.004f, -9.18f }, /* F49 byte 88: fit 0.02 dB */
    { 756, 6769, 368.6f, 2.347f, -10.55f }, /* F49 byte 93: fit 0.02 dB */
    { 828, 6769, 368.5f, 2.947f, -12.55f }, /* F49 byte 100: fit 0.03 dB */
    { 860, 6769, 368.5f, 3.356f, -13.69f }, /* F49 byte 104: fit 0.04 dB */
    { 900, 6769, 368.4f, 4.327f, -15.94f }, /* F49 byte 112: fit 0.05 dB */
    { 925, 6769, 368.3f, 5.507f, -18.10f }, /* F49 byte 120: fit 0.08 dB */
    { 935, 6769, 368.3f, 6.708f, -19.91f }, /* F49 byte 127: fit 0.11 dB */
    { 0, 8717, 1140.6f, 0.682f, +0.09f }, /* F64 byte 0: fit 0.06 dB */
    { 290, 8717, 1134.1f, 0.965f, -2.88f }, /* F64 byte 16: fit 0.06 dB */
    { 500, 8717, 1129.4f, 1.367f, -5.88f }, /* F64 byte 32: fit 0.05 dB */
    { 395, 8717, 1131.4f, 1.159f, -4.46f }, /* F64 byte 48: fit 0.06 dB */
    { 400, 8717, 1131.4f, 1.165f, -4.50f }, /* F64 byte 64: fit 0.06 dB */
    { 575, 8717, 1128.0f, 1.575f, -7.10f }, /* F64 byte 80: fit 0.05 dB */
    { 690, 8717, 1126.1f, 1.990f, -9.13f }, /* F64 byte 88: fit 0.04 dB */
    { 756, 8717, 1125.0f, 2.334f, -10.51f }, /* F64 byte 93: fit 0.04 dB */
    { 828, 8717, 1123.8f, 2.936f, -12.51f }, /* F64 byte 100: fit 0.03 dB */
    { 860, 8717, 1123.2f, 3.347f, -13.66f }, /* F64 byte 104: fit 0.04 dB */
    { 900, 8717, 1122.3f, 4.322f, -15.92f }, /* F64 byte 112: fit 0.05 dB */
    { 925, 8717, 1121.7f, 5.508f, -18.08f }, /* F64 byte 120: fit 0.08 dB */
    { 935, 8717, 1121.2f, 6.715f, -19.89f }, /* F64 byte 127: fit 0.12 dB */
    { 0, 10844, 4113.2f, 0.582f, +0.87f }, /* F80 byte 0: fit 0.57 dB */
    { 290, 10844, 3961.4f, 0.842f, -2.03f }, /* F80 byte 16: fit 0.66 dB */
    { 500, 10844, 3854.0f, 1.239f, -5.16f }, /* F80 byte 32: fit 0.58 dB */
    { 395, 10844, 3898.8f, 1.033f, -3.67f }, /* F80 byte 48: fit 0.63 dB */
    { 400, 10844, 3897.3f, 1.039f, -3.72f }, /* F80 byte 64: fit 0.63 dB */
    { 575, 10844, 3822.5f, 1.448f, -6.45f }, /* F80 byte 80: fit 0.54 dB */
    { 690, 10844, 3782.0f, 1.866f, -8.58f }, /* F80 byte 88: fit 0.47 dB */
    { 756, 10844, 3760.9f, 2.214f, -10.02f }, /* F80 byte 93: fit 0.42 dB */
    { 828, 10844, 3737.4f, 2.823f, -12.10f }, /* F80 byte 100: fit 0.36 dB */
    { 860, 10844, 3726.8f, 3.239f, -13.29f }, /* F80 byte 104: fit 0.33 dB */
    { 900, 10844, 3710.7f, 4.227f, -15.61f }, /* F80 byte 112: fit 0.27 dB */
    { 925, 10844, 3699.7f, 5.428f, -17.82f }, /* F80 byte 120: fit 0.24 dB */
    { 935, 10844, 3692.9f, 6.649f, -19.66f }, /* F80 byte 127: fit 0.24 dB */
};

/* the filter's steady gain for a sine at hz, dB: the peak over the second half of 40 periods */
static double svf_gain_db(fm_filter *f, double hz, uint32_t rate)
{
    double amp = 0.25 * 16777216.0, peak = 0;
    uint32_t i, n = (uint32_t)(rate / hz * 40.0) + 8000u;
    fm_filter_clear(f);
    for (i = 0; i < n; i++) {
        int32_t y = fm_filter_run(f, (int32_t)lround(amp * sin(2 * AN_PI * hz * i / rate)));
        if (i > n / 2 && fabs((double)y) > peak) peak = fabs((double)y);
    }
    return 20 * log10(peak / amp);
}

/* PG-8X's two equal sections at f0 and Q, at its level, dB */
static double svf_pg_db(double hz, double f0, double q, double level_db)
{
    double w = hz / f0, re = 1 - w * w, im = w / q;
    return level_db - 10 * log10(re * re + im * im) * 2;
}

/* a response's peak (dB), where it is (Hz) and its -3 dB width (semitones), on a 1/48-octave grid
 * from f0 / 8 to 4 f0; `ours` the filter's, else PG-8X's fitted sections */
typedef struct { double height, at, width, pass; } svf_peak;
static svf_peak svf_measure(fm_filter *f, const void *rowp, int ours)
{
    const struct { int16_t permille, cut; float f0, q, level_db; } *row = rowp;
    double g[241], hz[241], lo = 0, hi = 0;
    svf_peak p = { -1e9, 0, 0, 0 };
    int k, m = 0;
    for (k = 0; k <= 240; k++) {
        hz[k] = row->f0 * pow(2.0, (k - 144) / 48.0);
        g[k] = ours ? svf_gain_db(f, hz[k], SVF_RATE) : svf_pg_db(hz[k], row->f0, row->q, row->level_db);
        if (g[k] > p.height) p.height = g[k], p.at = hz[k], m = k;
    }
    for (k = m; k > 0 && g[k] > p.height - 3; k--) {}
    lo = k == m ? hz[k] : hz[k] * pow(hz[k + 1] / hz[k], (p.height - 3 - g[k]) / (g[k + 1] - g[k]));
    for (k = m; k < 240 && g[k] > p.height - 3; k++) {}
    hi = k == m ? hz[k] : hz[k - 1] * pow(hz[k] / hz[k - 1], (g[k - 1] - (p.height - 3)) / (g[k - 1] - g[k]));
    p.width = 12 * log2(hi / lo);
    p.pass = g[0];
    return p;
}

/* (a)-(d) Every row, F49, F64 and F80, 13 resonances each, against PG-8X's fitted sections: (a) the
 * peak's -3 dB width within 0.5 semitone and its height within 1 dB, where it stands 3 dB over the
 * passband; (c) the passband (f0 / 8)
 * within 0.5 dB; (b) a resonance's height alike at the three cutoffs within 1 dB; (d) at F49 the
 * peak where PG-8X's is, within 15 cents, for every resonance with a peak (690 up). */
TT_TEST(svf_lowpass_is_pg8x_two_sections)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    double ww = 0, wh = 0, wp = 0, wd = 0, wb = 0, height[3][13];
    size_t r;
    for (r = 0; r < sizeof PG_ROWS / sizeof PG_ROWS[0]; r++) {
        svf_peak us, pg;
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, PG_ROWS[r].permille);
        fm_filter_set_cutoff(&f, PG_ROWS[r].cut, rate);
        us = svf_measure(&f, &PG_ROWS[r], 1);
        pg = svf_measure(&f, &PG_ROWS[r], 0);
        if (pg.height - pg.pass >= 6.0) {       /* (a): where there's a peak to measure */
            double dw = fabs(us.width - pg.width) - fmax(0.0, 0.1 * pg.width - 0.5);  /* (0.5 st or 10 %) */
            if (dw > ww) ww = dw;
            if (fabs(us.height - pg.height) > wh) wh = fabs(us.height - pg.height);
        }
        if (fabs(us.pass - pg.pass) > wp) wp = fabs(us.pass - pg.pass);
        if (r < 13 && PG_ROWS[r].permille >= 690 && fabs(1200 * log2(us.at / pg.at)) > wd)
            wd = fabs(1200 * log2(us.at / pg.at));
        height[r / 13][r % 13] = us.height - us.pass;
        if (fabs(us.pass - pg.pass) > 0.5)
            printf("      F%s res %d: passband %+.2f (PG %+.2f)\n", r < 13 ? "49" : r < 26 ? "64" : "80",
                   PG_ROWS[r].permille, us.pass, pg.pass);
        if (pg.height - pg.pass >= 6.0 && (fabs(us.width - pg.width) > fmax(0.5, 0.1 * pg.width) ||
                                            fabs(us.height - pg.height) > 1.0))
            printf("      F%s res %d: width %.2f st (PG %.2f), height %+.2f (PG %+.2f)\n",
                   r < 13 ? "49" : r < 26 ? "64" : "80", PG_ROWS[r].permille, us.width, pg.width, us.height, pg.height);
    }
    for (r = 6; r < 13; r++) {                  /* (b): the resonant ones, 690 up, peak over passband */
        double a = height[0][r], b = height[1][r], c = height[2][r];
        double d = fmax(fabs(a - b), fmax(fabs(a - c), fabs(b - c)));
        wb = d > wb ? d : wb;
    }
    printf("    against PG-8X's sections, 39 rows: width within %.2f st, height %.2f dB, passband %.2f dB; "
           "the peak's place at F49 %.1f cents; a height across the cutoffs %.2f dB\n", ww, wh, wp, wd, wb);
    TT_CHECK(ww < 0.5 && wh < 1.0);
    TT_CHECK(wp < 0.5);
    TT_CHECK(wd < 15.0);
    TT_CHECK(wb < 1.0);
}

static void run_svf_tests(void)
{
    TT_RUN(svf_lowpass_is_pg8x_two_sections);
}
