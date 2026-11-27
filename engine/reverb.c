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

#include "placement.h"
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
#define MIX_GLIDE 16u    /* the wet level's glide, Q15 a sample: full to off in 2048 samples (46 ms) */

/* the FDN4's lengths at FDN_RATE: 2 input allpasses (2.6 and 7.6 ms, g 0.5:
 * longer or stronger ones ring undamped into the tail), then the 4 lines
 * (mutually prime, 43.8 .. 85.7 ms) */
#if HAS(FM_REVERB_FDN4)
static const uint16_t FDN_AP[2] = {113, 337};
static const uint16_t FDN_LEN[4] = {1931, 2543, 3163, 3779};
#endif

/* the plate's at PLATE_RATE (Dattorro's Table 1): 4 input diffusers; the
 * tank's left half (modulated allpass, delay, allpass, delay) and right */
#if HAS(FM_REVERB_PLATE)
static const uint16_t PL_IN[4] = {142, 107, 379, 277};
static const uint16_t PL_TANK[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
#define PL_EXC 16u /* the tank allpasses' excursion at PLATE_RATE */
/* the output taps (Dattorro's Table 2): the line (an index into PL_TANK)
 * and the tap at PLATE_RATE; the left's 7, then the right's */
static const uint8_t PL_TAP_LINE[14] = {5, 5, 6, 7, 1, 2, 3, 1, 1, 2, 3, 5, 6, 7};
static const uint16_t PL_TAP_AT[14] = {266, 2974, 1913, 1996, 1990, 187, 1066,
                                       353, 3627, 1228, 2673, 2111, 335, 121};
#endif
/* (their signs: + + - + - - -, both sides; written out in plate_block) */

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
#if HAS(FM_REVERB_FDN4)
    if (design == FM_REVERB_FDN4) {
        for (i = 0; i < 2u; i++) len[i] = scaled(FDN_AP[i], rate, FDN_RATE);
        for (i = 0; i < 4u; i++) len[2u + i] = scaled(FDN_LEN[i], rate, FDN_RATE);
        len[2] += 2u * FDN_SWAY + 2u; /* lines 0 and 2 sway */
        len[4] += 2u * FDN_SWAY + 2u;
        return 6u;
    }
#endif
#if HAS(FM_REVERB_PLATE)
    if (design == FM_REVERB_PLATE) {
        uint32_t exc = scaled(PL_EXC, rate, PLATE_RATE);
        for (i = 0; i < 4u; i++) len[i] = scaled(PL_IN[i], rate, PLATE_RATE);
        for (i = 0; i < 8u; i++) len[4u + i] = scaled(PL_TANK[i], rate, PLATE_RATE);
        len[4] += 2u * exc + 2u; /* the two modulated allpasses */
        len[8] += 2u * exc + 2u;
        return 12u;
    }
#endif
    (void)i;
    (void)rate;
    return 0;
}

/* the smallest power of two at least N */
static uint32_t pow2_at_least(uint32_t n)
{
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/*
 * The lines live in one buffer a power of two long (the plate: its input
 * diffusers in a second, small one, so the tank's fits 16 K words at
 * 44.1 kHz), under one pointer that steps down each sample: line k's
 * sample written a samples ago is at (p + base_k + a) & mask, its write at
 * (p + base_k) & mask. Each line has len + 1 words, so no line's write
 * reaches a sample another still reads. An add and a mask a read: no
 * per-line state, no wrap test (Dattorro's and Mutable's layout).
 */
static void layout(uint8_t design, uint32_t sample_rate, uint32_t *len, uint32_t *n_lines,
                   uint32_t *first, uint32_t *second)
{
    uint32_t n = line_lengths(design, sample_rate, len), i, a = 0, b = 0, split = design == FM_REVERB_PLATE ? 4u : 0u;
    for (i = 0; i < n; i++) {
        if (i < split) a += len[i] + 1u;
        else b += len[i] + 1u;
    }
    *n_lines = n;
    *first = n ? pow2_at_least(b) : 0;      /* the main lines */
    *second = split ? pow2_at_least(a) : 0; /* the plate's input diffusers */
}

uint32_t fm_reverb_words(uint8_t design, uint32_t sample_rate)
{
    uint32_t len[FM_REVERB_LINES], n, a, b;
    layout(design, sample_rate, len, &n, &a, &b);
    return a + b;
}

void fm_reverb_init(fm_reverb *r, uint8_t design, uint32_t sample_rate,
                    int16_t *mem, uint32_t words)
{
    uint32_t len[FM_REVERB_LINES], n, i, a, b, at = 0, at2 = 0, rate, split;
    static const fm_reverb zero; /* (the engine has no C library) */
    *r = zero;
    r->design = design;
    r->sample_rate = sample_rate;
    r->mem = mem;
    r->words = words;
    layout(design, sample_rate, len, &n, &a, &b);
    if (!mem || !n || a + b > words) return;
    split = design == FM_REVERB_PLATE ? 4u : 0u;
    r->buf = mem;
    r->mask = a - 1u;
    r->buf2 = mem + a;
    r->mask2 = b ? b - 1u : 0u;
    /* the write taps sweep each whole buffer, so a word no line reads now may still hold an old sample
     * that a window moves onto later: only after a full sweep of zeros is every word 0 */
    r->longest = a > b ? a : b;
    for (i = 0; i < n; i++) {
        r->len[i] = (uint16_t)len[i];
        if (i < split) { r->base[i] = (uint16_t)at2; at2 += len[i] + 1u; }
        else { r->base[i] = (uint16_t)at; at += len[i] + 1u; }
    }
    rate = run_rate(design, sample_rate);
    r->ph_inc[0] = fm_rate_to_phase_inc(600u, 1000u, rate, 0);  /* 0.60 Hz */
    r->ph_inc[1] = fm_rate_to_phase_inc(790u, 1000u, rate, 0);  /* 0.79 Hz */
    r->ph[1] = 1u << 30;
#if HAS(FM_REVERB_PLATE)
    if (design == FM_REVERB_PLATE) {
        for (i = 0; i < 14u; i++) r->tap[i] = (uint16_t)scaled(PL_TAP_AT[i], rate, PLATE_RATE);
        r->exc = scaled(PL_EXC, rate, PLATE_RATE);
    }
#endif
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
        /* (the buffers' sizes from their masks: no layout again, no divide; in the ISR once, after a
         * turn-on or all sound off: 16 K or 17 K words) */
        uint32_t n = r->mask + 1u + (r->buf2 && r->mask2 ? r->mask2 + 1u : 0u);
        for (i = 0; i < n; i++) r->mem[i] = 0;
    }
    r->p = 0;
    for (i = 0; i < 4u; i++) r->lp[i] = 0;
    r->in_acc = r->yl = r->yr = r->pl = r->pr = 0;
    r->sw_y[0] = r->sw_y[1] = 0;
    r->odd = 0;
    r->quiet = 0;
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
    r->mix_to = mix_pct * 32767 / 100;   /* (the level glides there: process_block) */
    r->quiet = 0;   /* (a new TONE can move a lowpass the idle skip would not) */
    if (r->design == FM_REVERB_FDN4) {
        for (i = 0; i < 4u; i++)
            r->gain[i] = loop_gain(r->len[2u + i] - ((i == 0 || i == 2) ? FDN_SWAY + 2u : 0u), rate, time_ms);
    } else {
        /* one loop of the tank passes its decay four times: twice in each half */
        uint32_t loop = 0;
        for (i = 0; i < 8u; i++) loop += r->len[4u + i];
        loop -= 2u * (2u * r->exc + 2u);   /* (the modulated allpasses' slack isn't in the loop: #408's review) */
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

static FM_BOTH int32_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}

/* a x g (Q15), truncated toward zero: inside a loop, rounding's half-LSB
 * bias holds a tail a few LSB up forever (a limit cycle); toward zero, no
 * product grows a sample, so every tail falls to 0. 32 bits: |a| <= 65535
 * and |g| <= 32768 everywhere here (the lines and the allpasses' inner
 * values are saturated to 16 bits first), so a x g fits; no 64-bit
 * multiply, no branch. With g 32768 it is a itself. */
static FM_BOTH int32_t mulr(int32_t a, int32_t g)
{
    int32_t p = a * g;
    return (p + ((p >> 31) & 32767)) >> 15;
}

#if HAS(FM_REVERB_FDN4)
static FM_BOTH int32_t half(int32_t v) /* v / 2 toward zero */
{
    return (v + (int32_t)((uint32_t)v >> 31)) >> 1;
}
#endif

/* after a block: quiet grows while every line took only zeros and every state is 0 (NZ: all of them
 * or-ed; the damping lowpasses reach 0 too, so a silent input lets everything rest) */
static FM_BOTH void quiet_after(fm_reverb *r, int32_t nz, uint32_t steps)
{
    if (nz) r->quiet = 0;
    else if (r->quiet < 0x40000000u) r->quiet += steps;
}

/* the damping one-pole: lp + k (d - lp), k Q15 (32768: none), the step rounded away from zero: it moves
 * lp toward d by 1 LSB at least and never past it (k <= 32768), so lp reaches d. Truncated toward zero,
 * the step had a dead band of 32768 / k LSB that canceled the loop's loss, and a damped tail held a DC
 * for good (#408's review); this way the tail decays as an undamped one does, through the loop's gain
 * (truncated toward zero). As cheap as mulr: no branch. */
static FM_BOTH int32_t damp(int32_t lp, int32_t d, int32_t k)
{
    int32_t p = (d - lp) * k;
    return lp + ((p + (~(p >> 31) & 32767)) >> 15);
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
static FM_BOTH void sway_set(fm_reverb *r, uint32_t k, uint32_t back_q8)
{
    uint32_t f = back_q8 & 255u, j = f >> 3, sub = f & 7u;
    r->sw_i[k] = (back_q8 >> 8) - 1u;
    r->sw_eta[k] = ETA[j] + (((ETA[j + 1u] - ETA[j]) * (int32_t)sub) >> 3);
}

/* a triangle, -32768..32767, from a phase */
static FM_BOTH int32_t tri(uint32_t ph)
{
    int32_t t = (int32_t)(ph >> 15);
    return t < 65536 ? t - 32768 : 98303 - t;
}

/* the sway, Q8 samples, +-EXC */
static FM_BOTH uint32_t sway_back(uint32_t base, uint32_t exc, uint32_t ph)
{
    return (base << 8) + (uint32_t)(((int32_t)exc << 8) + ((tri(ph) * (int32_t)exc) >> 7));
}

/* (the per-sample loops take their state into locals for the block, and
 * read and write the lines by the macros: AT(off) is the sample at offset
 * OFF past the pointer, so line k's sample written a samples ago is
 * AT(base_k + a) and its write AT(base_k)) */
#define AT(off) B[(p + (off)) & m]
#define AT2(off) B2[(p + (off)) & m2]

#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_FDN4
static FM_HOT void fdn_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    int16_t *B = r->buf;
    const uint32_t m = r->mask;
    uint32_t p = r->p, i;
    const uint32_t b0 = r->base[0], b1 = r->base[1], b2 = r->base[2], b3 = r->base[3], b4 = r->base[4],
                   b5 = r->base[5];
    const uint32_t a0 = b0 + r->len[0], a1 = b1 + r->len[1], a3 = b3 + r->len[3], a5 = b5 + r->len[5];
    const int32_t g0 = r->gain[0], g1 = r->gain[1], g2 = r->gain[2], g3 = r->gain[3], k = r->damp,
                  mix = r->mix;
    int32_t lp0 = r->lp[0], lp1 = r->lp[1], lp2 = r->lp[2], lp3 = r->lp[3];
    int32_t sy0 = r->sw_y[0], sy1 = r->sw_y[1];
    uint32_t s0, s2;
    int32_t e0, e2, nz = 0;
    sway_set(r, 0, sway_back(r->len[2] - 2u * FDN_SWAY - 2u, FDN_SWAY, r->ph[0]));
    sway_set(r, 1, sway_back(r->len[4] - 2u * FDN_SWAY - 2u, FDN_SWAY, r->ph[1]));
    r->ph[0] += r->ph_inc[0] * frames;
    r->ph[1] += r->ph_inc[1] * frames;
    s0 = b2 + r->sw_i[0];
    s2 = b4 + r->sw_i[1];
    e0 = r->sw_eta[0];
    e2 = r->sw_eta[1];
    for (i = 0; i < frames; i++) {
        int32_t x = sat16(((left[i] >> 1) + (right[i] >> 1)) >> IN_SHIFT), d, v, d0, d1, d2, d3, a, b, c, e;
        d = AT(a0);                                  /* the two input allpasses, g 0.5 */
        v = sat16(x - mulr(d, 16384));
        AT(b0) = (int16_t)v;
        nz |= v;
        x = d + mulr(v, 16384);
        d = AT(a1);
        v = sat16(x - mulr(d, 16384));
        AT(b1) = (int16_t)v;
        nz |= v;
        x = d + mulr(v, 16384);
        /* the swaying reads: y = eta x[n - i] + x[n - i - 1] - eta y[n - 1] */
        sy0 = d0 = sat16(mulr(AT(s0), e0) + AT(s0 + 1u) - mulr(sy0, e0));
        d1 = AT(a3);
        sy1 = d2 = sat16(mulr(AT(s2), e2) + AT(s2 + 1u) - mulr(sy1, e2));
        d3 = AT(a5);
        /* damping (k 32768: none) */
        lp0 = damp(lp0, d0, k);
        lp1 = damp(lp1, d1, k);
        lp2 = damp(lp2, d2, k);
        lp3 = damp(lp3, d3, k);
        d = mulr(lp0, g0);
        v = mulr(lp1, g1);
        a = d + v; b = d - v;                        /* Hadamard, halved: orthonormal */
        d = mulr(lp2, g2);
        v = mulr(lp3, g3);
        c = d + v; e = d - v;
        {
            int32_t w2 = sat16(half(a + c) + x), w3 = sat16(half(b + e) + x), w4 = sat16(half(a - c) + x),
                    w5 = sat16(half(b - e) + x);
            AT(b2) = (int16_t)w2;
            AT(b3) = (int16_t)w3;
            AT(b4) = (int16_t)w4;
            AT(b5) = (int16_t)w5;
            nz |= w2 | w3 | w4 | w5;
        }
        /* the sides: two lines each, one of them inverted (L/R correlation about 0, where the same
         * signs read 0.33); |d0 - d3| <= 65534, so the product fits */
        left[i] += ((d0 - d3) * mix) >> (15 - IN_SHIFT + 1);
        right[i] += ((d1 - d2) * mix) >> (15 - IN_SHIFT + 1);
        p--;
    }
    r->p = p;
    r->lp[0] = lp0; r->lp[1] = lp1; r->lp[2] = lp2; r->lp[3] = lp3;
    r->sw_y[0] = sy0; r->sw_y[1] = sy1;
    quiet_after(r, nz | sy0 | sy1 | lp0 | lp1 | lp2 | lp3, frames);
}
#endif

#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_PLATE
static FM_HOT void plate_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    int16_t *B = r->buf, *B2 = r->buf2;
    const uint32_t m = r->mask, m2 = r->mask2;
    uint32_t p = r->p, i, j, ticks = (frames + r->odd) / 2u;
    const uint16_t *base = r->base, *len = r->len;
    const int32_t g = r->gain[0], k = r->damp, mix = r->mix;
    int32_t lpl = r->lp[0], lpr = r->lp[1], sy0 = r->sw_y[0], sy1 = r->sw_y[1];
    int32_t yl = r->yl, yr = r->yr, pl = r->pl, pr = r->pr, in_acc = r->in_acc;
    uint32_t odd = r->odd, s4, s8;
    int32_t e4, e8, nz = 0;
    /* each line's offsets: its write (w), and its full delay before this tick's write (f) or after it
     * (h: a line written earlier in the tick is read one sample nearer, so lines 5, 7 and 9 and the taps
     * are one sample short of their nominal lengths, as the first loop had them; inaudible) */
    const uint32_t f0 = base[0] + len[0], f1 = base[1] + len[1], f2 = base[2] + len[2], f3 = base[3] + len[3];
    const uint32_t w4 = base[4], w5 = base[5], w6 = base[6], w7 = base[7], w8 = base[8], w9 = base[9],
                   w10 = base[10], w11 = base[11];
    const uint32_t h5 = w5 + len[5] - 1u, f6 = w6 + len[6], h7 = w7 + len[7] - 1u, h9 = w9 + len[9] - 1u,
                   f10 = w10 + len[10], f11 = w11 + len[11];
    uint32_t tap[14];
    sway_set(r, 0, sway_back(len[4] - 2u * r->exc - 2u, r->exc, r->ph[0]));
    sway_set(r, 1, sway_back(len[8] - 2u * r->exc - 2u, r->exc, r->ph[1]));
    r->ph[0] += r->ph_inc[0] * ticks;
    r->ph[1] += r->ph_inc[1] * ticks;
    s4 = w4 + r->sw_i[0];
    s8 = w8 + r->sw_i[1];
    e4 = r->sw_eta[0];
    e8 = r->sw_eta[1];
    for (j = 0; j < 14u; j++)   /* the taps read after the tick's writes: one sample nearer */
        tap[j] = base[4u + PL_TAP_LINE[j]] + r->tap[j] - 1u;
    for (i = 0; i < frames; i++) {
        int32_t x = sat16(((left[i] >> 1) + (right[i] >> 1)) >> IN_SHIFT), wl, wr_;
        if (!odd) {
            in_acc = x;
            wl = yl;
            wr_ = yr;
        } else {
            int32_t d, v, a, b;
            x = (in_acc + x) >> 1;                   /* the pair's mean: down to half the rate */
            d = AT2(f0); v = sat16(x - mulr(d, 24576)); AT2(base[0]) = (int16_t)v; nz |= v; x = d + mulr(v, 24576);
            d = AT2(f1); v = sat16(x - mulr(d, 24576)); AT2(base[1]) = (int16_t)v; nz |= v; x = d + mulr(v, 24576);
            d = AT2(f2); v = sat16(x - mulr(d, 20480)); AT2(base[2]) = (int16_t)v; nz |= v; x = d + mulr(v, 20480);
            d = AT2(f3); v = sat16(x - mulr(d, 20480)); AT2(base[3]) = (int16_t)v; nz |= v; x = d + mulr(v, 20480);
            /* left half, fed by the right's end */
            a = x + mulr(AT(f11), g);
            sy0 = b = sat16(mulr(AT(s4), e4) + AT(s4 + 1u) - mulr(sy0, e4));   /* the modulated allpass, g -0.7 */
            v = sat16(a + mulr(b, 22938));
            AT(w4) = (int16_t)v;
            nz |= v;
            a = sat16(b - mulr(v, 22938));
            AT(w5) = (int16_t)a;
            nz |= a;
            lpl = damp(lpl, AT(h5), k);
            b = mulr(lpl, g);
            d = AT(f6); v = sat16(b - mulr(d, 16384)); AT(w6) = (int16_t)v;
            nz |= v;
            v = sat16(d + mulr(v, 16384));
            AT(w7) = (int16_t)v;
            nz |= v;
            /* right half, fed by the left's end */
            a = x + mulr(AT(h7), g);
            sy1 = b = sat16(mulr(AT(s8), e8) + AT(s8 + 1u) - mulr(sy1, e8));
            v = sat16(a + mulr(b, 22938));
            AT(w8) = (int16_t)v;
            nz |= v;
            a = sat16(b - mulr(v, 22938));
            AT(w9) = (int16_t)a;
            nz |= a;
            lpr = damp(lpr, AT(h9), k);
            b = mulr(lpr, g);
            d = AT(f10); v = sat16(b - mulr(d, 16384)); AT(w10) = (int16_t)v;
            nz |= v;
            v = sat16(d + mulr(v, 16384));
            AT(w11) = (int16_t)v;
            nz |= v;
            /* the taps (Dattorro's), x 0.6; their sums pass 16 bits, so 64-bit, once a tick */
            a = AT(tap[0]) + AT(tap[1]) - AT(tap[2]) + AT(tap[3]) - AT(tap[4]) - AT(tap[5]) - AT(tap[6]);
            b = AT(tap[7]) + AT(tap[8]) - AT(tap[9]) + AT(tap[10]) - AT(tap[11]) - AT(tap[12]) - AT(tap[13]);
            pl = yl;
            pr = yr;
            yl = (int32_t)(((int64_t)a * 19661) >> 15);
            yr = (int32_t)(((int64_t)b * 19661) >> 15);
            p--;
            wl = (pl + yl) >> 1;                     /* and back up, linearly */
            wr_ = (pr + yr) >> 1;
        }
        odd ^= 1u;
        left[i] += (int32_t)(((int64_t)wl * mix) >> (15 - IN_SHIFT));
        right[i] += (int32_t)(((int64_t)wr_ * mix) >> (15 - IN_SHIFT));
    }
    r->p = p;
    r->odd = (uint8_t)odd;
    r->lp[0] = lpl; r->lp[1] = lpr; r->sw_y[0] = sy0; r->sw_y[1] = sy1;
    r->yl = yl; r->yr = yr; r->pl = pl; r->pr = pr; r->in_acc = in_acc;
    quiet_after(r, nz | sy0 | sy1 | yl | yr | pl | pr | in_acc | lpl | lpr, ticks);
}
#endif

int fm_reverb_idle(const fm_reverb *r)
{
    return r->idle;
}


/* (in flash: it checks and, after a turn-on or all sound off, empties the lines, then enters the
 * block's loop in RAM text, #391; a call goes from flash into RAM through FM_FAR, never back) */
void fm_reverb_process_block(fm_reverb *r, int32_t *left, int32_t *right, uint32_t frames)
{
    if (!fm_reverb_is_on(r)) return;
    if (r->clear) empty(r);
    /* MIX glides, a step a block of MIX_GLIDE a sample (#408's review): a dry patch loaded mid-tail fades
     * the tail out instead of cutting it, and a turned knob doesn't jump */
    {
        int32_t step = frames > 32767u / MIX_GLIDE ? 32767 : (int32_t)(MIX_GLIDE * frames);
        if (r->mix < r->mix_to) r->mix = r->mix_to - r->mix > step ? r->mix + step : r->mix_to;
        else if (r->mix > r->mix_to) r->mix = r->mix - r->mix_to > step ? r->mix - step : r->mix_to;
    }
    r->idle = 0;
    if (r->quiet > r->longest) {   /* every line holds only zeros: idle while the input is silent */
        uint32_t i, steps;
        int32_t in = 0;
        for (i = 0; i < frames; i++) in |= left[i] | right[i];
        if (!in) {   /* the loop would add 0 to every sample: skip it, its clocks moving on as they would */
            steps = r->design == FM_REVERB_PLATE ? (frames + r->odd) / 2u : frames;
            if (r->design == FM_REVERB_PLATE) r->odd = (uint8_t)((r->odd + frames) & 1u);
            r->ph[0] += r->ph_inc[0] * steps;
            r->ph[1] += r->ph_inc[1] * steps;
            r->p -= steps;
            r->idle = 1;
            return;
        }
    }
#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_FDN4
    if (r->design == FM_REVERB_FDN4) {
        FM_FAR(fdn_block)(r, left, right, frames);
        return;
    }
#endif
#if !defined(FM_REVERB_ONE) || FM_REVERB_DESIGN == FM_REVERB_PLATE
    if (r->design == FM_REVERB_PLATE) FM_FAR(plate_block)(r, left, right, frames);
#endif
}
