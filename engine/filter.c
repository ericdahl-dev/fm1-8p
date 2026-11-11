/*
 * filter.c - per-voice filter.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The lowpass is four one-pole stages in series with the last stage fed
 * back to the input (times -k) for resonance. Each stage is
 *
 *     m = (1 - w) * x + w * x1      (x1: the stage input one sample ago)
 *     y += g * (m - y)
 *
 * The w = 1/4 term puts a zero at z = -1/3 in each stage. Without it, the
 * stages lag too little in phase at high cutoffs and the one-sample delay
 * in the feedback makes the loop oscillate at Nyquist; with it the loop
 * self-oscillates near the cutoff, at a feedback close to 4 (as in the
 * analog ladder), over most of the range (flat at high cutoffs; at the
 * lowest cutoffs it rings down; see docs/fixed-point.md). Across the
 * top FADE_CENTS of the cutoff range w, the feedback and 1 - g fade to
 * zero, so the fully open filter is an exact wire.
 *
 * m is a weighted average of x and x1, and y of m and the old y, so with
 * 0 <= w, g <= 1 no stage can exceed its input. The feedback sum
 * goes through a soft clip that never exceeds CLIP_MAX, so every state
 * stays within +-CLIP_MAX whatever the resonance: no overflow, no
 * runaway. See docs/fixed-point.md for the formats and the measured
 * response.
 */
#include "filter.h"

/* Tables from tools/gen_filter_tables.py. */

/* 2^(16 i / 1200), Q2.30: one entry per 16 cents across an octave. */
static const uint32_t exp2_16c[76] = {
    1073741824u, 1083711303u, 1093773347u, 1103928816u, 1114178575u,
    1124523502u, 1134964479u, 1145502398u, 1156138160u, 1166872673u,
    1177706854u, 1188641628u, 1199677930u, 1210816701u, 1222058893u,
    1233405467u, 1244857391u, 1256415645u, 1268081214u, 1279855096u,
    1291738297u, 1303731830u, 1315836721u, 1328054003u, 1340384721u,
    1352829926u, 1365390683u, 1378068064u, 1390863152u, 1403777040u,
    1416810831u, 1429965638u, 1443242585u, 1456642805u, 1470167444u,
    1483817656u, 1497594608u, 1511499477u, 1525533449u, 1539697724u,
    1553993511u, 1568422032u, 1582984518u, 1597682215u, 1612516377u,
    1627488271u, 1642599176u, 1657850383u, 1673243194u, 1688778924u,
    1704458901u, 1720284463u, 1736256962u, 1752377763u, 1768648242u,
    1785069790u, 1801643808u, 1818371712u, 1835254932u, 1852294909u,
    1869493099u, 1886850971u, 1904370007u, 1922051704u, 1939897572u,
    1957909136u, 1976087933u, 1994435516u, 2012953454u, 2031643326u,
    2050506731u, 2069545278u, 2088760595u, 2108154322u, 2127728115u,
    2147483648u
};

/* (1 - e^-x) / x at x = i / 16, Q1.30. */
static const uint32_t h_tab[65] = {
    1073741824u, 1040875655u, 1009343921u, 979085795u, 950043403u,
    922161677u, 895388214u, 869673137u, 844968974u, 821230532u,
    798414786u, 776480768u, 755389467u, 735103727u, 715588158u,
    696809050u, 678734282u, 661333251u, 644576792u, 628437111u,
    612887713u, 597903339u, 583459909u, 569534460u, 556105093u,
    543150919u, 530652014u, 518589368u, 506944842u, 495701125u,
    484841697u, 474350786u, 464213335u, 454414967u, 444941953u,
    435781179u, 426920119u, 418346805u, 410049801u, 402018178u,
    394241491u, 386709754u, 379413420u, 372343358u, 365490838u,
    358847508u, 352405376u, 346156797u, 340094455u, 334211347u,
    328500768u, 322956300u, 317571795u, 312341368u, 307259378u,
    302320422u, 297519323u, 292851119u, 288311055u, 283894569u,
    279597290u, 275415024u, 271343748u, 267379604u, 263518889u
};

#define NOTE0_HZ_Q16 535809u      /* MIDI note 0, 8.1758 Hz, Q16.16 */
#define TWO_PI_Q29   3373259426u  /* 2 pi, Q3.29 */
#define ONE_Q30      (1 << 30)
#define FADE_CENTS   600
/* fade per cent below the top: 2^30 / 600, rounded down (1789569) */
#define FADE_STEP    (ONE_Q30 / FADE_CENTS)
/* (top - cutoff) * FADE_STEP is at most FADE_CENTS * FADE_STEP <= 2^30 */
typedef char fm_require_fade_fits_q30[
    (int64_t)FADE_CENTS * FADE_STEP <= ONE_Q30 ? 1 : -1];
#define W_ZERO       (1 << 28)   /* 1/4, Q1.30: the zero at z = -1/3 */
/* Feedback at resonance 1000, per mille, Q4.28: 4.2 / 1000. Just above
 * the loop's self-oscillation threshold, which in theory is about 4.0
 * over most of the range and falls to about 3.4 toward Nyquist. At the
 * lowest cutoffs (below about 15 Hz at 44.1 kHz) 4.2 is not enough. */
#define K_PER_MILLE  1127429
#define OCTAVE       1200
/* Offset that makes pitch differences non-negative before / and %. */
#define OCT_BIAS     32

/* 2^(f / 1200) for f in 0..1199 cents, Q2.30 (1.0 .. just under 2.0). */
static uint32_t exp2_cents(uint32_t f)
{
    uint32_t i = f >> 4, r = f & 15u;
    return exp2_16c[i] + (((exp2_16c[i + 1] - exp2_16c[i]) * r) >> 4);
}

/* Frequency of a pitch, Q16.16 Hz; c in -12000..19200 (init only). */
static uint64_t cents_hz_q16(int32_t c)
{
    uint32_t t = (uint32_t)(c + 16 * OCTAVE);
    uint32_t oct = t / OCTAVE;
    uint64_t hz = ((uint64_t)NOTE0_HZ_Q16 * exp2_cents(t % OCTAVE)) >> 30;
    return oct >= 16 ? hz << (oct - 16) : hz >> (16 - oct);
}

int32_t fm_filter_rate_cents(uint32_t sample_rate)
{
    /* largest pitch at or below the rate: a binary search, init only */
    uint64_t rate_q16 = (uint64_t)sample_rate << 16;
    int32_t lo = -12000, hi = 19200;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo + 1) / 2;
        if (cents_hz_q16(mid) <= rate_q16)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/*
 * One-pole coefficient for a corner `cents`, Q1.30: g = 1 - e^-x with
 * x = 2 pi fc / fs, as x * h(x). Corners at or above Nyquist use the
 * Nyquist value (about 0.957).
 */
static int32_t coef_for(int32_t cents, int32_t rate_cents)
{
    int32_t d = cents - rate_cents; /* pitch of fc / fs */
    uint32_t t, oct, r, x, i, frac, h;
    if (d > -OCTAVE) d = -OCTAVE;
    if (d < -(OCT_BIAS - 1) * OCTAVE) return 0;
    t = (uint32_t)(d + OCT_BIAS * OCTAVE);
    oct = t / OCTAVE; /* <= 31 */
    r = exp2_cents(t % OCTAVE) >> (OCT_BIAS - oct); /* fc / fs, Q2.30 */
    x = (uint32_t)(((uint64_t)r * TWO_PI_Q29) >> 29); /* <= pi, Q2.30 */
    i = x >> 26;
    frac = x & 0x3FFFFFFu;
    h = h_tab[i] - (uint32_t)(((uint64_t)(h_tab[i] - h_tab[i + 1]) * frac) >> 26);
    return (int32_t)(((uint64_t)x * h) >> 30);
}

int32_t fm_filter_voice_cents(int32_t cutoff_cents, int32_t track_permille,
                              uint8_t note, int32_t env_level,
                              int32_t env_amount_cents)
{
    /* 100 cents per semitone, per mille: track * (note - 60) / 10 */
    int32_t track = track_permille * ((int32_t)note - 60) / 10;
    int32_t env = (int32_t)(((int64_t)env_amount_cents * env_level) >> 30);
    return cutoff_cents + track + env;
}

void fm_filter_clear(fm_filter *f)
{
    int i;
    for (i = 0; i < 4; i++) f->s[i] = 0;
    f->x1 = 0;
    f->hp = 0;
}

void fm_filter_reset(fm_filter *f)
{
    fm_filter_clear(f);
    f->g = ONE_Q30;
    f->k = f->k_res = 0;
    f->fade = 0;
    f->hp_g = 0;
}

static int32_t mul_q30(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 30);
}

/*
 * Wide open (fade 0: g = 1, w = 0, k = 0) with the highpass off, each
 * stage passes its input through exactly, so running the filter leaves
 * every stage input and output equal to the last sample. The fast path in
 * fm_filter_process skips that and keeps only the last sample, in s[3];
 * this fills in the rest when a setting change ends the fast path, so the
 * filter carries on as if it had run all along.
 */
static void leave_open(fm_filter *f)
{
    int i;
    for (i = 0; i < 3; i++) f->s[i] = f->s[3];
    f->x1 = f->s[3];
}

void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents)
{
    int32_t g;
    int was_open = fm_filter_is_wire(f);
    if (cutoff_cents < 0) cutoff_cents = 0;
    if (cutoff_cents > FM_FILTER_OPEN_CENTS) cutoff_cents = FM_FILTER_OPEN_CENTS;
    g = coef_for(cutoff_cents, rate_cents);
    /* 1.0 up to the fade, then down to 0 at open */
    f->fade = cutoff_cents <= FM_FILTER_OPEN_CENTS - FADE_CENTS
                  ? ONE_Q30
                  : (FM_FILTER_OPEN_CENTS - cutoff_cents) * FADE_STEP;
    f->g = ONE_Q30 - mul_q30(ONE_Q30 - g, f->fade);
    f->k = mul_q30(f->k_res, f->fade);
    if (was_open && !fm_filter_is_wire(f)) leave_open(f);
}

void fm_filter_set_resonance(fm_filter *f, int32_t permille)
{
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    f->k_res = permille * K_PER_MILLE;
    f->k = mul_q30(f->k_res, f->fade);
}

/*
 * Soft clip, Q8.24: a wire up to +-2.0 (so a full-scale input passes
 * untouched), then a quadratic knee that flattens out at +-4.0 for
 * inputs of +-6.0 and beyond. Continuous, with a continuous slope.
 */
#define KNEE      (2 << 24)
#define KNEE_SPAN (4 << 24)
#define CLIP_MAX  (4 << 24)

static int32_t soft_clip(int32_t u)
{
    int32_t a = (u < 0 ? -u : u) - KNEE, m;
    if (a <= 0) return u;
    m = a >= KNEE_SPAN ? CLIP_MAX                     /* a < 2^26 below: a^2 in one 32x32->64 multiply */
                       : KNEE + a - (int32_t)(((int64_t)a * a) >> 27); /* a - a^2/8 */
    return u < 0 ? -m : m;
}

/*
 * Every Q8.24 input (to +-128.0) gives exactly the output of the 64-bit
 * filter, with 32-bit sums on the usual path:
 * - The highpass state stays within the input's range. While both are
 *   within +-32.0 (an oscillator stays near +-1.0), the highpass output
 *   is within +-64.0, and less the feedback (within +-4.0 * 4.2) the
 *   lowpass input sum still fits 32 bits.
 * - Louder, the highpass runs in 64 bits and its output is clamped to
 *   +-IN_MAX (32.0). That changes nothing: a clamped value less the
 *   largest feedback still drives the soft clip past its saturation (6.0)
 *   with the same sign, as the unclamped one does. With the highpass off
 *   the input is clamped the same way.
 * Only the products need 64 bits, and each is one 32x32->64 multiply.
 */
#define IN_MAX ((1 << 29) - 1)
#define WITHIN_32(v) ((uint32_t)(v) + (1u << 29) < (1u << 30)) /* -32.0 <= v < 32.0 */

static int32_t clamp_in(int32_t x)
{
    return x > IN_MAX ? IN_MAX : x < -IN_MAX ? -IN_MAX : x;
}

void fm_filter_set_highpass(fm_filter *f, uint8_t step, int32_t rate_cents)
{
    /* corners in cents: off, 100 Hz, 240 Hz, 560 Hz */
    static const int32_t corner[FM_FILTER_HIGHPASS_STEPS] = {
        0, 4335, 5851, 7318
    };
    if (step >= FM_FILTER_HIGHPASS_STEPS) step = FM_FILTER_HIGHPASS_STEPS - 1;
    /* off -> on: start from the signal as it is (output = input), not
     * from state left from the last time it was on, which would click */
    if (!f->hp_g) {
        f->hp = 0;
        if (step && fm_filter_is_wire(f)) leave_open(f); /* hp_g is 0 here */
    }
    f->hp_g = step ? coef_for(corner[step], rate_cents) : 0;
}

/* One stage: m = x - w * (x - x1), y += g * (m - y); x1 is the stage's
 * input one sample ago. */
static void stage(int32_t *y, int32_t x, int32_t x1, int32_t w, int32_t g)
{
    int32_t m = x - mul_q30(x - x1, w);
    *y += mul_q30(m - *y, g);
}

/* stage() with w = 1/4, as a shift. */
static void stage_quarter(int32_t *y, int32_t x, int32_t x1, int32_t g)
{
    int32_t m = x - ((x - x1) >> 2);
    *y += mul_q30(m - *y, g);
}

int32_t fm_filter_process(fm_filter *f, int32_t in)
{
    int32_t x, u, p0, p1, p2;
    if (fm_filter_is_wire(f)) { /* the default patch: a wire (see leave_open) */
        f->s[3] = soft_clip(clamp_in(in));
        return f->s[3];
    }
    if (!f->hp_g) {
        x = clamp_in(in);
    } else if (WITHIN_32(in) && WITHIN_32(f->hp)) { /* highpass = input minus a one-pole lowpass of it */
        f->hp += mul_q30(in - f->hp, f->hp_g);
        x = in - f->hp;
    } else { /* louder than any oscillator: the same in 64 bits */
        int64_t d;
        f->hp += (int32_t)((((int64_t)in - f->hp) * f->hp_g) >> 30);
        d = (int64_t)in - f->hp;
        x = d > IN_MAX ? IN_MAX : d < -IN_MAX ? -IN_MAX : (int32_t)d;
    }
    u = soft_clip(x - (int32_t)(((int64_t)f->s[3] * f->k) >> 28));
    /* each stage after the first takes the one before it, now and as it
     * was one sample ago */
    p0 = f->s[0];
    p1 = f->s[1];
    p2 = f->s[2];
    if (f->fade == ONE_Q30) { /* below the fade: w = 1/4, w * d is d >> 2 */
        stage_quarter(&f->s[0], u, f->x1, f->g);
        stage_quarter(&f->s[1], f->s[0], p0, f->g);
        stage_quarter(&f->s[2], f->s[1], p1, f->g);
        stage_quarter(&f->s[3], f->s[2], p2, f->g);
    } else { /* w = 1/4 x fade, exactly (W_ZERO is 2^28) */
        int32_t w = f->fade >> 2;
        stage(&f->s[0], u, f->x1, w, f->g);
        stage(&f->s[1], f->s[0], p0, w, f->g);
        stage(&f->s[2], f->s[1], p1, w, f->g);
        stage(&f->s[3], f->s[2], p2, w, f->g);
    }
    f->x1 = u;
    return f->s[3];
}
