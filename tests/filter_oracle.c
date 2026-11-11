/*
 * filter_oracle.c - the filter's per-sample processing as merged in #25 (engine/filter.c at
 * 7af3bd1), kept as a test-only reference: its lowpass, soft clip and highpass with every sum in
 * 64 bits, plus the resonance's bass shelf on the output (#98). It runs on the engine's own fm_filter, set up by the engine's own setters (the
 * coefficients are not what is under test), and is never built into the engine. Tests hold the
 * engine's fm_filter_process to it (test_filter_oracle.c), so the engine can change its arithmetic
 * and still be held to the sound that shipped.
 */
#include <stdint.h>

#include "../engine/filter.h"

/* #25's soft clip: a wire to +-2.0, a quadratic knee, flat at +-4.0. */
static int32_t oracle_soft_clip(int64_t u)
{
    int64_t a = (u < 0 ? -u : u) - (2 << 24);
    int32_t m;
    if (a <= 0) return (int32_t)u;
    m = a >= (4 << 24) ? (4 << 24) : (2 << 24) + (int32_t)(a - ((a * a) >> 27)); /* a - a^2/8 */
    return u < 0 ? -m : m;
}

static int32_t oracle_mul_q30(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 30);
}

/* One stage: m = x - w * (x - x1), y += g * (m - y). (At w = 1/4 the engine uses a shift, which
 * is the same value.) x1 is the stage's input one sample ago: the first stage's is kept in f->x1,
 * each later one's is the stage before it as it was (#25 kept all four; the values are the same). */
static void oracle_stage(int32_t *y, int32_t x, int32_t x1, int32_t w, int32_t g)
{
    int32_t m = x - oracle_mul_q30(x - x1, w);
    *y += oracle_mul_q30(m - *y, g);
}

/* #25's fm_filter_process, on `f` set up by the engine's setters. */
static int32_t oracle_filter_process(fm_filter *f, int32_t in)
{
    int64_t x = in;
    int32_t u, p0, p1, p2, w = f->fade >> 2; /* #25's w: 1/4 x fade */
    if (f->fade == 0 && f->hp_g == 0) { /* open, highpass off: a wire */
        f->s[3] = oracle_soft_clip(x);
        return f->s[3];
    }
    if (f->hp_g) { /* highpass = input minus a one-pole lowpass of it */
        f->hp += (int32_t)(((x - f->hp) * f->hp_g) >> 30);
        x -= f->hp;
    }
    x = (x * f->a) >> 13; /* #98's input level */
    u = oracle_soft_clip(x - (((int64_t)f->s[3] * f->k) >> 28));
    p0 = f->s[0];
    p1 = f->s[1];
    p2 = f->s[2];
    oracle_stage(&f->s[0], u, f->x1, w, f->g);
    oracle_stage(&f->s[1], f->s[0], p0, w, f->g);
    oracle_stage(&f->s[2], f->s[1], p1, w, f->g);
    oracle_stage(&f->s[3], f->s[2], p2, w, f->g);
    f->x1 = u;
    if (!f->h) return f->s[3];
    /* #98's bass shelf: a weighted average of the output and its one-pole lowpass at the cutoff */
    f->lp += (int32_t)((((int64_t)f->s[3] - f->lp) * f->g) >> 30);
    return f->s[3] + (int32_t)((((int64_t)f->lp - f->s[3]) * f->h) >> 13);
}
