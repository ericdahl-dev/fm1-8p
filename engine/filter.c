/*
 * filter.c - per-voice filter.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The lowpass is four one-pole stages in series, y += g * (x - y). Each
 * stage output is a weighted average of its input and its old output,
 * so with 0 <= g <= 1 no stage can exceed its input. See
 * docs/fixed-point.md for the formats and the measured response.
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

void fm_filter_reset(fm_filter *f)
{
    int i;
    for (i = 0; i < 4; i++) f->s[i] = 0;
    f->g = ONE_Q30;
}

void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents)
{
    if (cutoff_cents < 0) cutoff_cents = 0;
    f->g = cutoff_cents >= FM_FILTER_OPEN_CENTS
               ? ONE_Q30
               : coef_for(cutoff_cents, rate_cents);
}

void fm_filter_set_resonance(fm_filter *f, int32_t permille)
{
    (void)f;
    (void)permille;
}

/* One stage: y += g * (x - y). */
static int32_t stage(int32_t *y, int32_t x, int32_t g)
{
    *y += (int32_t)(((int64_t)(x - *y) * g) >> 30);
    return *y;
}

int32_t fm_filter_process(fm_filter *f, int32_t in)
{
    int32_t y = stage(&f->s[0], in, f->g);
    y = stage(&f->s[1], y, f->g);
    y = stage(&f->s[2], y, f->g);
    return stage(&f->s[3], y, f->g);
}
