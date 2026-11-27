/*
 * reverb.c - the reverb after the chorus (#197). Both designs are written
 * from their papers: the FDN4 after Jot (1991) and Stautner and Puckette
 * (1982), the plate after Dattorro (J. Audio Eng. Soc. 45(9), 1997). The
 * lines are int16 in the caller's memory and hold the signal at half full
 * scale (the input is the chorus's Q8.24 mean, 1.0 to 16384), so the tank
 * has 6 dB of headroom; every product inside a loop is truncated toward
 * zero (no limit cycle), every store saturated, and the filter states are
 * int32.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "reverb.h"

#include "units.h"

/* A device build compiles one design (FM_REVERB_ONE); the host both. */
#ifdef FM_REVERB_ONE
#define HAS(d) (FM_REVERB_DESIGN == (d))
#else
#define HAS(d) 1
#endif

#define IN_SHIFT 10      /* Q8.24 to the lines' scale: 1.0 is 16384 */
#define FDN_RATE 44100u  /* the rate the FDN4's lengths are tuned at */
#define PLATE_RATE 29761u /* the paper's */
#define FDN_SWAY 8u      /* the swaying lines' excursion, samples each way */

/* the FDN4's lengths at FDN_RATE: 2 input allpasses (2.6 and 7.6 ms, g 0.5:
 * longer or stronger ones ring undamped into the tail), then the 4 lines
 * (mutually prime, 43.8 .. 85.7 ms) */
static const uint16_t FDN_AP[2] = {113, 337};
static const uint16_t FDN_LEN[4] = {1931, 2543, 3163, 3779};

/* the plate's at PLATE_RATE (Dattorro's Table 1): 4 input diffusers; the
 * tank's left half (modulated allpass, delay, allpass, delay) and right */
static const uint16_t PL_IN[4] = {142, 107, 379, 277};
static const uint16_t PL_TANK[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
#define PL_EXC 16u /* the tank allpasses' excursion at PLATE_RATE */
/* the output taps (Dattorro's Table 2): the line (an index into PL_TANK)
 * and the tap at PLATE_RATE; the left's 7, then the right's */
static const uint8_t PL_TAP_LINE[14] = {5, 5, 6, 7, 1, 2, 3, 1, 1, 2, 3, 5, 6, 7};
static const uint16_t PL_TAP_AT[14] = {266, 2974, 1913, 1996, 1990, 187, 1066,
                                       353, 3627, 1228, 2673, 2111, 335, 121};
static const int8_t PL_TAP_SIGN[14] = {1, 1, -1, 1, -1, -1, -1, 1, 1, -1, 1, -1, -1, -1};

/* 2^(-i / 16), Q30, i = 0..16 */
static const int32_t EXP2_16[17] = {
    1073741824, 1028218693, 984625594, 942880699, 902905651, 864625413,
    827968132, 792865000, 759250125, 727060411, 696235434, 666717336,
    638450708, 611382493, 585461881, 560640218, 536870912};

/* 2^(-e), e Q16 >= 0, as Q15 (control rate only) */
static int32_t exp2_neg_q15(uint64_t e)
{
    uint32_t n = (uint32_t)(e >> 16), i = (uint32_t)(e >> 12) & 15u, f = (uint32_t)e & 4095u;
    int32_t a, b, v;
    if (n >= 16u) return 0;
    a = EXP2_16[i];
    b = EXP2_16[i + 1u];
    v = a - (int32_t)(((int64_t)(a - b) * f) >> 12); /* Q30 */
    return (v >> 15) >> n;
}

static uint32_t scaled(uint32_t len, uint32_t rate, uint32_t at)
{
    return (uint32_t)(((uint64_t)len * rate + at / 2u) / at);
}

/* the rate a design runs at */
static uint32_t run_rate(uint8_t design, uint32_t sample_rate)
{
    return design == FM_REVERB_PLATE ? sample_rate / 2u : sample_rate;
}

/* each line's length (samples) for DESIGN at SAMPLE_RATE; returns the
 * count (0: unknown) */
static uint32_t line_lengths(uint8_t design, uint32_t sample_rate, uint32_t *len)
{
    uint32_t i, rate = run_rate(design, sample_rate);
    if (design == FM_REVERB_FDN4 && HAS(FM_REVERB_FDN4)) {
        for (i = 0; i < 2u; i++) len[i] = scaled(FDN_AP[i], rate, FDN_RATE);
        for (i = 0; i < 4u; i++) len[2u + i] = scaled(FDN_LEN[i], rate, FDN_RATE);
        len[2] += 2u * FDN_SWAY + 2u; /* lines 0 and 2 sway */
        len[4] += 2u * FDN_SWAY + 2u;
        return 6u;
    }
    if (design == FM_REVERB_PLATE && HAS(FM_REVERB_PLATE)) {
        uint32_t exc = scaled(PL_EXC, rate, PLATE_RATE);
        for (i = 0; i < 4u; i++) len[i] = scaled(PL_IN[i], rate, PLATE_RATE);
        for (i = 0; i < 8u; i++) len[4u + i] = scaled(PL_TANK[i], rate, PLATE_RATE);
        len[4] += 2u * exc + 2u; /* the two modulated allpasses */
        len[8] += 2u * exc + 2u;
        return 12u;
    }
    return 0;
}

uint32_t fm_reverb_words(uint8_t design, uint32_t sample_rate)
{
    uint32_t len[FM_REVERB_LINES], n = line_lengths(design, sample_rate, len), i, w = 0;
    for (i = 0; i < n; i++) w += len[i];
    return w;
}

void fm_reverb_init(fm_reverb *r, uint8_t design, uint32_t sample_rate,
                    int16_t *mem, uint32_t words)
{
    uint32_t len[FM_REVERB_LINES], n, i, at = 0, rate;
    static const fm_reverb zero; /* (the engine has no C library) */
    *r = zero;
    r->design = design;
    r->sample_rate = sample_rate;
    r->mem = mem;
    r->words = words;
    n = line_lengths(design, sample_rate, len);
    if (!mem || !n || fm_reverb_words(design, sample_rate) > words) return;
    for (i = 0; i < n; i++) {
        r->line[i].b = mem + at;
        r->line[i].len = len[i];
        at += len[i];
    }
    rate = run_rate(design, sample_rate);
    r->ph_inc[0] = fm_rate_to_phase_inc(600u, 1000u, rate, 0);  /* 0.60 Hz */
    r->ph_inc[1] = fm_rate_to_phase_inc(790u, 1000u, rate, 0);  /* 0.79 Hz */
    r->ph[1] = 1u << 30;
    if (design == FM_REVERB_PLATE) {
        for (i = 0; i < 14u; i++) r->tap[i] = (uint16_t)scaled(PL_TAP_AT[i], rate, PLATE_RATE);
        r->exc = scaled(PL_EXC, rate, PLATE_RATE);
    }
    r->ready = 1;
    r->clear = 1;
    fm_reverb_set(r, 0, 2000, 100);
}

void fm_reverb_silence(fm_reverb *r)
{
    r->clear = 1;
}

static void empty(fm_reverb *r)
{
    uint32_t i;
    if (r->ready) {
        uint32_t n = fm_reverb_words(r->design, r->sample_rate);
        for (i = 0; i < n; i++) r->mem[i] = 0;
    }
    for (i = 0; i < FM_REVERB_LINES; i++) r->line[i].w = 0;
    for (i = 0; i < 4u; i++) r->lp[i] = 0;
    r->in_acc = r->yl = r->yr = r->pl = r->pr = 0;
    r->sw_y[0] = r->sw_y[1] = 0;
    r->odd = 0;
    r->clear = 0;
}

/* the gain that takes a loop of LEN samples at RATE to -60 dB in TIME_MS,
 * Q15: 10^(-3 len / (time rate)) = 2^(-9.9658 len / (time rate)) */
static int32_t loop_gain(uint32_t len, uint32_t rate, int32_t time_ms)
{
    uint64_t e = (uint64_t)653118u * len * 1000u / ((uint64_t)(uint32_t)time_ms * rate);
    return exp2_neg_q15(e);
}

void fm_reverb_set(fm_reverb *r, int32_t mix_pct, int32_t time_ms, int32_t tone_pct)
{
    uint32_t rate = run_rate(r->design, r->sample_rate), i;
    int was = fm_reverb_is_on(r);
    if (mix_pct < 0) mix_pct = 0;
    if (mix_pct > 100) mix_pct = 100;
    if (time_ms < 100) time_ms = 100;
    if (tone_pct < 0) tone_pct = 0;
    if (tone_pct > 100) tone_pct = 100;
    r->mix = mix_pct * 32767 / 100;
    if (r->design == FM_REVERB_FDN4) {
        for (i = 0; i < 4u; i++)
            r->gain[i] = loop_gain(r->line[2u + i].len - ((i == 0 || i == 2) ? FDN_SWAY + 2u : 0u), rate, time_ms);
    } else {
        /* one loop of the tank passes its decay four times: twice in each half */
        uint32_t loop = 0;
        for (i = 0; i < 8u; i++) loop += r->line[4u + i].len;
        r->gain[0] = loop_gain(loop, rate, 4 * time_ms);
    }
    if (tone_pct >= 100) {
        r->damp = 32768; /* none */
    } else {
        /* the lowpass's corner: 1 kHz at 0 to 16 kHz just under 100,
         * 2^(4 tone / 99); k = w / (1 + w), w = 2 pi fc / rate */
        int32_t fc = (int32_t)((16000 * (int64_t)exp2_neg_q15((uint64_t)4u * (uint32_t)(99 - tone_pct) * 65536u / 99u)) >> 15);
        uint64_t w = (uint64_t)205887u * (uint32_t)fc / rate;
        r->damp = (int32_t)(w * 32768u / (32768u + w));
    }
    if (!was && fm_reverb_is_on(r)) r->clear = 1;
}

/* ---- the lines ---- */

static inline int32_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}

/* a x g (Q15), truncated toward zero: inside a loop, rounding's half-LSB
 * bias holds a tail a few LSB up forever (a limit cycle); toward zero, no
 * product grows a sample, so every tail falls to 0 */
static inline int32_t mulr(int32_t a, int32_t g)
{
    /* 32 bits: |a| <= 65535 and |g| <= 32767 everywhere here (the lines and the allpasses' inner values
     * are saturated to 16 bits first), so a x g fits; no 64-bit multiply, no branch */
    int32_t p = a * g;
    return (p + ((p >> 31) & 32767)) >> 15;
}

static inline int32_t half(int32_t v) /* v / 2 toward zero */
{
    return v >= 0 ? v >> 1 : -((-v) >> 1);
}

/* the sample written BACK samples ago (1 <= back <= len) */
static inline int32_t rd(const fm_rv_line *l, uint32_t back)
{
    uint32_t i = l->w + l->len - back;
    if (i >= l->len) i -= l->len;
    return l->b[i];
}

static inline void wr(fm_rv_line *l, int32_t v)
{
    l->b[l->w] = (int16_t)sat16(v);
    if (++l->w >= l->len) l->w = 0;
}

/* a Schroeder allpass the line's length long: the line holds v */
static inline int32_t ap(fm_rv_line *l, int32_t x, int32_t g)
{
    int32_t d = rd(l, l->len), v = sat16(x - mulr(d, g));
    l->b[l->w] = (int16_t)v;
    if (++l->w >= l->len) l->w = 0;
    return d + mulr(v, g);
}

/* (1 - D) / (1 + D) for D = 1 + f / 32, f = 0..32, Q15: the allpass
 * interpolator's coefficient for a fraction of 1 to 2 samples */
static const int16_t ETA[33] = {
    0, -504, -993, -1467, -1928, -2374, -2809, -3231, -3641, -4040, -4428,
    -4806, -5174, -5532, -5881, -6222, -6554, -6877, -7193, -7501, -7802,
    -8096, -8383, -8663, -8937, -9204, -9466, -9722, -9973, -10218, -10458,
    -10693, -10923};

/* swaying line K's read for this block, BACK_Q8 samples (Q8) ago: whole
 * samples i and an allpass's fraction of 1 to 2 more, so every frequency
 * keeps its level (a linear read would be a lowpass in the loop and cut
 * a long tail's highs short) */
static void sway_set(fm_reverb *r, uint32_t k, uint32_t back_q8)
{
    uint32_t f = back_q8 & 255u, j = f >> 3, sub = f & 7u;
    r->sw_i[k] = (back_q8 >> 8) - 1u;
    r->sw_eta[k] = ETA[j] + (((ETA[j + 1u] - ETA[j]) * (int32_t)sub) >> 3);
}

/* y = eta x[n - i] + x[n - i - 1] - eta y[n - 1] */
static inline int32_t rd_sway(fm_reverb *r, const fm_rv_line *l, uint32_t k)
{
    int32_t y = sat16(mulr(rd(l, r->sw_i[k]), r->sw_eta[k]) + rd(l, r->sw_i[k] + 1u) - mulr(r->sw_y[k], r->sw_eta[k]));
    return r->sw_y[k] = y;   /* (16 bits, as a line's sample: the products after it stay in 32) */
}

/* a triangle, -32768..32767, from a phase */
static inline int32_t tri(uint32_t ph)
{
    int32_t t = (int32_t)(ph >> 15);
    return t < 65536 ? t - 32768 : 98303 - t;
}

/* the sway, Q8 samples, +-EXC */
static inline uint32_t sway_back(uint32_t base, uint32_t exc, uint32_t ph)
{
    return (base << 8) + (uint32_t)(((int32_t)exc << 8) + ((tri(ph) * (int32_t)exc) >> 7));
}

/* the damping lowpass: k Q15, 32768 passes it through */
static inline int32_t damp_lp(int32_t *s, int32_t x, int32_t k)
{
    if (k >= 32768) return *s = x;
    *s += mulr(x - *s, k);
    return *s;
}

#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_FDN4
static void fdn_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    fm_rv_line *L = r->line;
    uint32_t i;
    sway_set(r, 0, sway_back(L[2].len - 2u * FDN_SWAY - 2u, FDN_SWAY, r->ph[0]));
    sway_set(r, 1, sway_back(L[4].len - 2u * FDN_SWAY - 2u, FDN_SWAY, r->ph[1]));
    r->ph[0] += r->ph_inc[0] * frames;
    r->ph[1] += r->ph_inc[1] * frames;
    for (i = 0; i < frames; i++) {
        int32_t x = sat16(((left[i] >> 1) + (right[i] >> 1)) >> IN_SHIFT), d0, d1, d2, d3, y0, y1, y2, y3, a, b, c, e;
        x = ap(&L[0], x, 16384);
        x = ap(&L[1], x, 16384);
        d0 = rd_sway(r, &L[2], 0);
        d1 = rd(&L[3], L[3].len);
        d2 = rd_sway(r, &L[4], 1);
        d3 = rd(&L[5], L[5].len);
        y0 = mulr(damp_lp(&r->lp[0], d0, r->damp), r->gain[0]);
        y1 = mulr(damp_lp(&r->lp[1], d1, r->damp), r->gain[1]);
        y2 = mulr(damp_lp(&r->lp[2], d2, r->damp), r->gain[2]);
        y3 = mulr(damp_lp(&r->lp[3], d3, r->damp), r->gain[3]);
        a = y0 + y1; b = y0 - y1; c = y2 + y3; e = y2 - y3; /* Hadamard, halved: orthonormal */
        wr(&L[2], half(a + c) + x);
        wr(&L[3], half(b + e) + x);
        wr(&L[4], half(a - c) + x);
        wr(&L[5], half(b - e) + x);
        /* the sides: two lines each, one of them inverted (L/R correlation about 0, where the same
         * signs read 0.33) */
        left[i] += ((d0 - d3) * r->mix) >> (15 - IN_SHIFT + 1);   /* (|d0 - d3| <= 65534: fits) */
        right[i] += ((d1 - d2) * r->mix) >> (15 - IN_SHIFT + 1);
    }
}
#endif

#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_PLATE
/* one sample of the tank at half the rate: in -> 4 diffusers -> the two halves */
static void plate_tick(fm_reverb *r, int32_t x)
{
    fm_rv_line *L = r->line;
    int32_t a, b, g = r->gain[0];
    uint32_t i;
    x = ap(&L[0], x, 24576);
    x = ap(&L[1], x, 24576);
    x = ap(&L[2], x, 20480);
    x = ap(&L[3], x, 20480);
    /* left half, fed by the right's end */
    a = x + mulr(rd(&L[11], L[11].len), g);
    b = rd_sway(r, &L[4], 0); /* the modulated allpass, g -0.7 */
    { int32_t v = sat16(a + mulr(b, 22938)); wr(&L[4], v); a = b - mulr(v, 22938); }
    wr(&L[5], a);
    b = mulr(damp_lp(&r->lp[0], rd(&L[5], L[5].len), r->damp), g);
    wr(&L[7], ap(&L[6], b, 16384));
    /* right half, fed by the left's end */
    a = x + mulr(rd(&L[7], L[7].len), g);
    b = rd_sway(r, &L[8], 1);
    { int32_t v = sat16(a + mulr(b, 22938)); wr(&L[8], v); a = b - mulr(v, 22938); }
    wr(&L[9], a);
    b = mulr(damp_lp(&r->lp[1], rd(&L[9], L[9].len), r->damp), g);
    wr(&L[11], ap(&L[10], b, 16384));
    /* the taps, x 0.6 */
    a = b = 0;
    for (i = 0; i < 7u; i++) {
        int32_t t = rd(&L[4u + PL_TAP_LINE[i]], r->tap[i]);
        a += PL_TAP_SIGN[i] > 0 ? t : -t;
    }
    for (i = 7; i < 14u; i++) {
        int32_t t = rd(&L[4u + PL_TAP_LINE[i]], r->tap[i]);
        b += PL_TAP_SIGN[i] > 0 ? t : -t;
    }
    r->pl = r->yl;
    r->pr = r->yr;
    r->yl = (int32_t)(((int64_t)a * 19661) >> 15);   /* (7 taps: past 16 bits, so 64 bits, once a tick) */
    r->yr = (int32_t)(((int64_t)b * 19661) >> 15);
}

static void plate_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    uint32_t i, ticks = (frames + r->odd) / 2u;
    sway_set(r, 0, sway_back(r->line[4].len - 2u * r->exc - 2u, r->exc, r->ph[0]));
    sway_set(r, 1, sway_back(r->line[8].len - 2u * r->exc - 2u, r->exc, r->ph[1]));
    r->ph[0] += r->ph_inc[0] * ticks;
    r->ph[1] += r->ph_inc[1] * ticks;
    for (i = 0; i < frames; i++) {
        int32_t x = sat16(((left[i] >> 1) + (right[i] >> 1)) >> IN_SHIFT), wl, wr_;
        if (!r->odd) {
            r->in_acc = x;
            wl = r->yl;
            wr_ = r->yr;
        } else {
            plate_tick(r, (r->in_acc + x) >> 1); /* the pair's mean: down to half the rate */
            wl = (r->pl + r->yl) >> 1;           /* and back up, linearly */
            wr_ = (r->pr + r->yr) >> 1;
        }
        r->odd ^= 1u;
        left[i] += (int32_t)(((int64_t)wl * r->mix) >> (15 - IN_SHIFT));   /* (the taps' sum can pass 16 bits) */
        right[i] += (int32_t)(((int64_t)wr_ * r->mix) >> (15 - IN_SHIFT));
    }
}
#endif

void fm_reverb_process_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    if (!fm_reverb_is_on(r)) return;
    if (r->clear) empty(r);
#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_FDN4
    if (r->design == FM_REVERB_FDN4) {
        fdn_block(r, left, right, frames);
        return;
    }
#endif
#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_PLATE
    if (r->design == FM_REVERB_PLATE) plate_block(r, left, right, frames);
#endif
}
