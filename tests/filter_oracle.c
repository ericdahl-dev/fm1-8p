/*
 * filter_oracle.c - the filter's per-sample processing as a test-only reference, every sum in 64 bits:
 * the highpass (#25) and its bass boost (#93), the input level, and the lowpass as two equal
 * state-variable sections (#162): each state's products summed with the remainder carried, the
 * first's zeros z1 of the way to the origin, the second section's states held to +-32.0 (Q9.23) and its zeros at the origin, the output held to
 * +-16.0. It runs on the engine's own fm_filter, set up by the engine's own setters (the coefficients
 * are not what is under test), and is never built into the engine. Tests hold the engine's
 * fm_filter_process to it (test_filter_oracle.c): its 32-bit sums never wrap.
 */
#include <stdint.h>

#include "../engine/filter.h"

static int64_t oracle_hold(int64_t u, int64_t m)
{
    return u > m ? m : u < -m ? -m : u;
}

/* one section on v0 (Q9.23): its two states from s, carries c; returns its lowpass v2 and leaves
 * its bandpass's next value in *v1 */
static int64_t oracle_section(const fm_filter *f, int32_t *s, int32_t *c, int64_t v0, int64_t *v1, int held)
{
    int64_t i1 = s[0], i2 = s[1], v3 = v0 - i2, p, v2;
    p = (int64_t)f->a1 * i1 + (int64_t)f->a2 * v3 + c[0];
    *v1 = p >> 30;
    c[0] = (int32_t)(p - ((p >> 30) << 30));
    p = (int64_t)f->a2 * i1 + (int64_t)f->a3 * v3 + c[1];
    v2 = i2 + (p >> 30);
    c[1] = (int32_t)(p - ((p >> 30) << 30));
    s[0] = (int32_t)(held ? oracle_hold(2 * *v1 - i1, 32 << 23) : 2 * *v1 - i1);
    s[1] = (int32_t)(held ? oracle_hold(2 * v2 - i2, 32 << 23) : 2 * v2 - i2);
    return v2;
}

static int32_t oracle_filter_process(fm_filter *f, int32_t in)
{
    int64_t x = in, v1, v2, out, y, i1, i2;
    if (f->fade == 0 && f->hp_g == 0) { /* open, highpass off: a wire */
        f->s[3] = (int32_t)oracle_hold(x, FM_FILTER_HOLD);
        return f->s[3];
    }
    if (f->hp_g > 0) {                  /* highpass = input minus a one-pole lowpass of it */
        f->hp += (int32_t)(((x - f->hp) * f->hp_g) >> 30);
        x -= f->hp;
    } else if (f->hp_g < 0) {           /* the bass boost: 1.26 x + 1.02 that lowpass */
        f->hp += (int32_t)(((x - f->hp) * -(int64_t)f->hp_g) >> 30);
        x += ((x * 279172874) >> 30) + (((int64_t)f->hp * 1095216660) >> 30);
    } else {
        x = oracle_hold(x, (1 << 30) - 1);
    }
    x = oracle_hold(x, (1 << 30) - 1);
    i1 = f->s[0];
    i2 = f->s[1];
    v2 = oracle_section(f, &f->s[0], &f->c[0], oracle_hold((x * f->a) >> 30, FM_FILTER_HOLD) >> 1, &v1, 0);
    {   /* section 1's zeros 5/8 of the way to the origin */
        int64_t dz = 3 * (v2 - i2) - ((f->t * i1) >> 27);
        v2 = (int32_t)(v2 + ((dz * f->z1) >> 30));
    }
    i1 = f->s[2];
    i2 = f->s[3];
    v2 = oracle_section(f, &f->s[2], &f->c[2], v2, &v1, 1);
    out = v2 + 3 * (v2 - i2) - ((f->t * i1) >> 27);
    y = oracle_hold(out, FM_FILTER_HOLD >> 1) * 2;
    if (f->fade == (1 << 30)) return (int32_t)y;
    return (int32_t)oracle_hold(x + (((y - x) * f->fade) >> 30), FM_FILTER_HOLD);
}
