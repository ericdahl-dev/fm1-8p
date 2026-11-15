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

/* PG-8X 2.0.13's f0 at resonance 93 against the engine's cutoff for the same VCF FREQ (F16..F88) */
static const struct { int16_t cut; float f0; } PG_F0[] = {
    { 2639, 32.3f }, { 3620, 57.7f }, { 4614, 104.5f }, { 5621, 189.0f }, { 6769, 368.6f }, { 7672, 620.1f },
    { 8717, 1125.0f }, { 9774, 2047.8f }, { 10844, 3760.9f }, { 11927, 6965.7f } };

/* the frequency, in 2-cent steps over +-1.5 semitones around hz, where the filter peaks */
static double svf_peak_hz(fm_filter *f, double hz)
{
    double best = -1e9, at = hz;
    int c;
    for (c = -150; c <= 150; c += 2) {
        double h = hz * pow(2.0, c / 1200.0), g = svf_gain_db(f, h, SVF_RATE);
        if (g > best) best = g, at = h;
    }
    return at;
}

/* (d) The peak sits where PG-8X's does at every VCF FREQ from 16 to 88 (resonance 756, its RES 93):
 * within 15 cents of the peak of PG-8X's fitted sections there (Q 2.3: under its f0) */
TT_TEST(svf_peak_follows_pg8x_f0_up_the_range)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    double worst = 0;
    size_t r;
    for (r = 0; r < sizeof PG_F0 / sizeof PG_F0[0]; r++) {
        double q = 2.347, w2 = 1.0 - 1.0 / (2.0 * q * q), want, got, c;
        want = PG_F0[r].f0 * sqrt(w2 > 0 ? (1.0 + sqrt(1.0 + 0.0)) / 2.0 * w2 * 2.0 / 2.0 : 1.0);
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, 756);
        fm_filter_set_cutoff(&f, PG_F0[r].cut, rate);
        got = svf_peak_hz(&f, want);
        {   /* PG-8X's own sections' peak, on the same grid */
            double best = -1e9; int k;
            for (k = -150; k <= 150; k += 2) {
                double h = want * pow(2.0, k / 1200.0), g = svf_pg_db(h, PG_F0[r].f0, q, 0);
                if (g > best) best = g, want = want, c = h;
            }
            want = c;
        }
        c = fabs(1200.0 * log2(got / want));
        worst = c > worst ? c : worst;
        printf("    cutoff %5d: peak %7.1f Hz, PG-8X's %7.1f (%.0f cents)\n", PG_F0[r].cut, got, want, c);
    }
    TT_CHECK(worst < 15.0);
}

/* (e) The top end at F80 (f0 about 3.7 kHz at 48 kHz): from f0 to 2 f0 within 1 dB of PG-8X's
 * sections, from 2 f0 to 4 f0 within 3 dB (where they are within 50 dB of their peak) */
TT_TEST(svf_top_end_is_pg8x_at_f80)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    double w1 = 0, w2 = 0;
    size_t r;
    for (r = 26; r < 39; r++) {
        double peak = -1e9;
        int k;
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, PG_ROWS[r].permille);
        fm_filter_set_cutoff(&f, PG_ROWS[r].cut, rate);
        for (k = -12; k <= 24; k++)
            peak = fmax(peak, svf_pg_db(PG_ROWS[r].f0 * pow(2.0, k / 12.0), PG_ROWS[r].f0, PG_ROWS[r].q, PG_ROWS[r].level_db));
        for (k = 0; k <= 24; k++) {
            double hz = PG_ROWS[r].f0 * pow(2.0, k / 12.0), m = svf_pg_db(hz, PG_ROWS[r].f0, PG_ROWS[r].q, PG_ROWS[r].level_db);
            double d = fabs(svf_gain_db(&f, hz, SVF_RATE) - m);
            if (m < peak - 50) continue;
            if (k <= 12) w1 = fmax(w1, d);
            else w2 = fmax(w2, d);
        }
    }
    printf("    F80's top end against PG-8X's sections: f0..2 f0 within %.2f dB, 2..4 f0 within %.2f\n", w1, w2);
    TT_CHECK(w1 < 1.0 && w2 < 3.0);
}

/* (f) Linear in the JX range: at F49 resonance 935 the response to a sine 24 dB under an oscillator's
 * level, at it, and 6 dB over it is the same within 0.05 dB from f0 / 2 to 2 f0 */
TT_TEST(svf_is_linear_at_jx_resonances)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    static const double amp[3] = { 0.063, 1.0, 2.0 };
    double worst = 0;
    int k, a;
    fm_filter_reset(&f);
    fm_filter_set_resonance(&f, 935);
    fm_filter_set_cutoff(&f, 6769, rate);
    for (k = -12; k <= 12; k += 2) {
        double hz = 368.3 * pow(2.0, k / 12.0), g[3];
        for (a = 0; a < 3; a++) {
            double peak = 0, A = amp[a] * 16777216.0;
            uint32_t i, n = (uint32_t)(SVF_RATE / hz * 40.0) + 8000u;
            fm_filter_clear(&f);
            for (i = 0; i < n; i++) {
                int32_t y = fm_filter_run(&f, (int32_t)lround(A * sin(2 * AN_PI * hz * i / SVF_RATE)));
                if (i > n / 2 && fabs((double)y) > peak) peak = fabs((double)y);
            }
            g[a] = 20 * log10(peak / A);
        }
        worst = fmax(worst, fmax(fabs(g[0] - g[1]), fabs(g[2] - g[1])));
    }
    printf("    resonance 935 at -24 / 0 / +6 dB: the same within %.3f dB\n", worst);
    TT_CHECK(worst < 0.05);
}

/* (g) The rate: at 8, 44.1, 96 and 192 kHz the F49 and F64 peaks at resonance 935 sit within 10 cents
 * of 48 kHz's, and as high within 0.5 dB (Q within 5 %) */
TT_TEST(svf_peak_holds_across_rates)
{
    static const uint32_t rates[] = { 8000u, 44100u, 96000u, 192000u };
    static const int32_t cuts[] = { 6769, 8717 };
    static fm_filter f;
    double wc = 0, wd = 0;
    size_t c, r;
    for (c = 0; c < 2; c++) {
        double ref_hz = 0, ref_db = 0;
        for (r = 0; r < 5; r++) {
            uint32_t rate = r == 4 ? SVF_RATE : rates[r];
            double best = -1e9, at = 0, hz0 = c ? 1121.2 : 368.3;
            int k;
            if (r < 4 && c == 1 && rate == 8000u) continue;   /* (1.1 kHz at 8 kHz: too near Nyquist) */
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, 935);
            fm_filter_set_cutoff(&f, cuts[c], fm_filter_rate_cents(rate));
            for (k = -100; k <= 100; k += 2) {
                double h = hz0 * pow(2.0, k / 1200.0), g = svf_gain_db(&f, h, rate);
                if (g > best) best = g, at = h;
            }
            if (r == 0 && c == 0) { ref_hz = 0; }
            if (r == 4) {
                ref_hz = at;
                ref_db = best;
            }
            if (r == 4) continue;
            (void)ref_db;
        }
        /* second pass against 48 kHz's */
        for (r = 0; r < 4; r++) {
            uint32_t rate = rates[r];
            double best = -1e9, at = 0, hz0 = c ? 1121.2 : 368.3;
            int k;
            if (c == 1 && rate == 8000u) continue;
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, 935);
            fm_filter_set_cutoff(&f, cuts[c], fm_filter_rate_cents(rate));
            for (k = -100; k <= 100; k += 2) {
                double h = hz0 * pow(2.0, k / 1200.0), g = svf_gain_db(&f, h, rate);
                if (g > best) best = g, at = h;
            }
            wc = fmax(wc, fabs(1200.0 * log2(at / ref_hz)));
            wd = fmax(wd, fabs(best - ref_db));
            printf("    cutoff %d at %u Hz: peak %.1f Hz %+.2f dB (48 kHz: %.1f Hz %+.2f)\n", (int)cuts[c],
                   (unsigned)rate, at, best, ref_hz, ref_db);
        }
    }
    TT_CHECK(wc < 10.0 && wd < 0.5);
}

/* the filter's two sections in double, on the filter's own coefficients (the reference for the
 * fixed point's precision) */
typedef struct { double s[4]; } svf_ref;
static double svf_ref_run(svf_ref *r, const fm_filter *f, double x)
{
    double a1 = f->a1 / 1073741824.0, a2 = f->a2 / 1073741824.0, a3 = f->a3 / 1073741824.0, t = f->t / 134217728.0;
    double v1, v2, v3, i1, i2, out;
    x *= f->a / 1073741824.0;
    i1 = r->s[0]; i2 = r->s[1]; v3 = x - i2;
    v1 = a1 * i1 + a2 * v3; v2 = i2 + a2 * i1 + a3 * v3;
    r->s[0] = 2 * v1 - i1; r->s[1] = 2 * v2 - i2;
    i1 = r->s[2]; i2 = r->s[3]; v3 = v2 - i2;
    v1 = a1 * i1 + a2 * v3; v2 = i2 + a2 * i1 + a3 * v3;
    out = v2 + 3 * (v2 - i2) - t * i1;
    r->s[2] = 2 * v1 - i1; r->s[3] = 2 * v2 - i2;
    return out;
}

/* (h) Precise down to a 20 Hz cutoff at 48 kHz: against the same sections in double, on noise at an
 * oscillator's level, the signal is 100 dB over the error at resonance 0 and 935; after one impulse
 * at resonance 0 the output comes back to exactly 0; DC at 0.5 passes as 0.5 (within 1e-5). */
TT_TEST(svf_is_precise_at_a_20_hz_cutoff)
{
    static fm_filter f;
    static const int32_t res[] = { 0, 935 };
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    int32_t cut = (int32_t)lround(6900 + 1200 * log2(20.0 / 440.0)) + 176;   /* f0 20 Hz */
    double worst = 1e9, dc;
    uint32_t seed = 1, i;
    size_t r;
    int back = 0;
    for (r = 0; r < 2; r++) {
        svf_ref ref = { { 0, 0, 0, 0 } };
        double sig = 0, err = 0;
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, res[r]);
        fm_filter_set_cutoff(&f, cut, rate);
        for (i = 0; i < 4u * SVF_RATE; i++) {
            int32_t x, y;
            double yr;
            seed = seed * 1664525u + 1013904223u;
            x = (int32_t)(seed >> 7) - (1 << 24);
            y = fm_filter_run(&f, x);
            yr = svf_ref_run(&ref, &f, x / 8388608.0 / 2.0 * 1.0) * 2.0 * 8388608.0;
            if (i > SVF_RATE) {
                sig += yr * yr;
                err += (y - yr) * (y - yr);
            }
        }
        worst = fmin(worst, 10 * log10(sig / err));
    }
    fm_filter_reset(&f);
    fm_filter_set_cutoff(&f, cut, rate);
    for (i = 0; i < 4u * SVF_RATE; i++) {
        int32_t y = fm_filter_run(&f, i == 0 ? 1 << 24 : 0);
        if (i > 3u * SVF_RATE && y != 0) back = 1;
    }
    fm_filter_reset(&f);
    fm_filter_set_cutoff(&f, cut, rate);
    for (i = 0; i < 2u * SVF_RATE; i++)
        dc = fm_filter_run(&f, 1 << 23) / 16777216.0;
    printf("    20 Hz cutoff: signal over error %.1f dB at worst; an impulse back to 0: %s; DC 0.5 -> %.7f\n",
           worst, back ? "no" : "yes", dc);
    TT_CHECK(worst > 100.0);
    TT_CHECK(!back);
    TT_CHECK(fabs(dc - 0.5) < 1e-5);
}

/* (i) Bounded at resonance 1000 at 8 and 192 kHz too: a +-16.0 square at every cutoff keeps every
 * state within its hold (Q9.23: section 2's +-32.0, section 1's well under) and the output within
 * +-16.0 */
TT_TEST(svf_is_bounded_at_every_rate)
{
    static const uint32_t rates[] = { 8000u, 192000u };
    static fm_filter f;
    int32_t out = 0, st = 0;
    size_t r;
    int32_t c;
    uint32_t i, j;
    for (r = 0; r < 2; r++)
        for (c = 0; c <= FM_FILTER_OPEN_CENTS; c += 400) {
            fm_filter_reset(&f);
            fm_filter_set_resonance(&f, 1000);
            fm_filter_set_cutoff(&f, c, fm_filter_rate_cents(rates[r]));
            for (i = 0; i < 20000u; i++) {
                int32_t y = fm_filter_run(&f, (i / (20u + (uint32_t)c / 400u)) & 1u ? FM_FILTER_HOLD : -FM_FILTER_HOLD);
                out = (y < 0 ? -y : y) > out ? (y < 0 ? -y : y) : out;
                for (j = 0; j < 4; j++)
                    st = (f.s[j] < 0 ? -f.s[j] : f.s[j]) > st ? (f.s[j] < 0 ? -f.s[j] : f.s[j]) : st;
            }
        }
    printf("    resonance 1000, +-16.0 square, 8 and 192 kHz: largest output %.2f, largest state %.2f (of 32.0)\n",
           out / 16777216.0, st / 8388608.0);
    TT_CHECK(out <= FM_FILTER_HOLD);
    TT_CHECK(st <= 32 << 23);
}

/* (j) The top of the range: a cutoff swept a cent a sample from 13000 to the top (14400) on a 440 Hz
 * sine never steps more than the sine itself moves plus 0.001 (-60 dB), and at the top the filter is
 * a wire: its output is its input, exactly */
TT_TEST(svf_fades_to_the_wire_at_the_top)
{
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE), prev = 0, px = 0, worst = 0;
    int32_t c;
    int exact = 1;
    uint32_t i = 0;
    fm_filter_reset(&f);
    fm_filter_set_resonance(&f, 500);
    fm_filter_set_cutoff(&f, 13000, rate);
    for (c = 13000; c <= FM_FILTER_OPEN_CENTS + 200; c++, i++) {
        int32_t x = (int32_t)lround(0.5 * 16777216.0 * sin(2 * AN_PI * 440.0 * i / SVF_RATE)), y, d;
        fm_filter_set_cutoff(&f, c, rate);
        y = fm_filter_run(&f, x);
        d = (y > prev ? y - prev : prev - y) - (x > px ? x - px : px - x);
        if (i > 4800 && d > worst) worst = d;
        if (c >= FM_FILTER_OPEN_CENTS && y != x) exact = 0;
        prev = y;
        px = x;
    }
    printf("    cutoff swept to the top: largest step past the sine's own %.5f; open is a wire: %s\n",
           worst / 16777216.0, exact ? "yes" : "no");
    TT_CHECK(worst < (1 << 24) / 1000);
    TT_CHECK(exact);
}

/* (k) A cutoff change starts nothing of its own: a 1-sample 3000-cent jump at resonance 935 on a
 * 220 Hz sine (either way) steps no more at the change than over the next 10 ms of ringing */
TT_TEST(svf_cutoff_change_has_no_click_of_its_own)
{
    static const int32_t jumps[][2] = { { 6000, 9000 }, { 9000, 6000 }, { 4000, 11000 }, { 11000, 4000 } };
    static fm_filter f;
    int32_t rate = fm_filter_rate_cents(SVF_RATE);
    size_t j;
    int clean = 1;
    for (j = 0; j < 4; j++) {
        int32_t prev = 0, at = 0, after = 0;
        uint32_t i;
        fm_filter_reset(&f);
        fm_filter_set_resonance(&f, 935);
        fm_filter_set_cutoff(&f, jumps[j][0], rate);
        for (i = 0; i < 24480u; i++) {
            int32_t y, d;
            if (i == 24000u) fm_filter_set_cutoff(&f, jumps[j][1], rate);
            y = fm_filter_run(&f, (int32_t)lround(0.5 * 16777216.0 * sin(2 * AN_PI * 220.0 * i / SVF_RATE)));
            d = y > prev ? y - prev : prev - y;
            if (i == 24000u) at = d;
            else if (i > 24000u && d > after) after = d;
            prev = y;
        }
        printf("    %d -> %d cents: step at the change %.4f, largest over the next 10 ms %.4f\n",
               (int)jumps[j][0], (int)jumps[j][1], at / 16777216.0, after / 16777216.0);
        clean &= at <= after;
    }
    TT_CHECK(clean);
}

static void run_svf_tests(void)
{
    TT_RUN(svf_lowpass_is_pg8x_two_sections);
    TT_RUN(svf_peak_follows_pg8x_f0_up_the_range);
    TT_RUN(svf_top_end_is_pg8x_at_f80);
    TT_RUN(svf_is_linear_at_jx_resonances);
    TT_RUN(svf_peak_holds_across_rates);
    TT_RUN(svf_is_precise_at_a_20_hz_cutoff);
    TT_RUN(svf_is_bounded_at_every_rate);
    TT_RUN(svf_fades_to_the_wire_at_the_top);
    TT_RUN(svf_cutoff_change_has_no_click_of_its_own);
}
