/*
 * filter.c - per-voice filter.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The lowpass is PG-8X 2.0.13's (#162, the resonance's width): two equal
 * resonant 2-pole sections in series, both at f0 with the same Q. PG-8X's
 * response fits that within 0.01..0.66 dB at every resonance and cutoff
 * measured, and its Q depends on the resonance alone (tests/test_svf.c's
 * rows). Each section is a state-variable filter by Zavalishin's TPT
 * (Andrew Simper's form): with t = tan(pi f0 / fs), k = 1 / Q and
 *
 *     a1 = 1 / (1 + t (t + k)), a2 = t a1, a3 = t a2
 *     v3 = x - ic2, v1 = a1 ic1 + a2 v3, v2 = ic2 + a2 ic1 + a3 v3
 *     ic1 = 2 v1 - ic1, ic2 = 2 v2 - ic2       (v2: the lowpass)
 *
 * The second section's output is lp + 2 t bp + t^2 hp, which moves its
 * two zeros from Nyquist to the origin: the analog prototype's top end
 * (PG-8X's), not the bilinear notch, worked out as v2 + 3 (v2 - ic2) -
 * t ic1. Each state's two products are summed in 64 bits with the
 * remainder carried to the next sample, so the sums are exact however
 * small the step (a 20 Hz cutoff's a3 is 1e-6). The input level is k Q0:
 * PG-8X's passband, Q0 / Q. The states are Q9.23 (the second section's
 * reach past 128 at the top resonance); the second's are held to +-32.0,
 * so nothing wraps, and that hold is past anything the output's own hold
 * lets out. Across the top FADE_CENTS of the cutoff the output fades to
 * the input, so the fully open filter is an exact wire: that is the top
 * of the range up to resonance 90. With more resonance the top is PG-8X's
 * (#168, RT_C0 below): no fade, and the open cutoff is not a wire. At resonance 0
 * and 44.1 to 48 kHz the top is PG-8X's (#151): f0 stops at F0_MAX_R0 and
 * the fade is FADE0_CENTS long. (Resonance 0 plays a cent and a few LSB
 * short of those: w, how much peak, reads 2 of 2^30 there, not 0, so the
 * stop is at -2049 cents, the damping 1 LSB under K_TOP0 and the fade up
 * to 7 LSB over its line. The fit was made with it.)
 */
#include "filter.h"
#include "cutoff_curve.h"
#include "res_top_tables.h"

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
#define OCTAVE       1200
#define OCT_BIAS     32

/* Resonance `permille` (0..1000) read from a RES_STEP table. */
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

/*
 * PG-8X's cutoff curve (#253). PG-8X 2.0.13 adds VCF ENV and VCF KEY to
 * VCF FREQ in knob steps, then plays the sum through one curve, measured
 * as cents(K) = 0.0988 K^2 + 115.03 K - 1474 (tools/cutoff_curve.py, which
 * generates cutoff_curve.h, #344). The engine sums its cutoff sources
 * linearly, in "linear cents": 115.03 of them a knob step, from
 * FM_CURVE_ZERO at step 0. Then it bends the sum once onto the curve:
 * C(u) = u + A (u - FM_CURVE_ZERO)^2, A = 0.0988 / 115.03^2 (FM_CURVE_A_Q32).
 * Below step 0 (u < FM_CURVE_ZERO) the curve adds nothing. At or above
 * the open cutoff the sum is open either way (C(u) >= u), so a voice
 * passes it as it is: no multiply, and an open filter is untouched.
 */

/* The curve itself: rising, for the inverse below. u is under twice
 * open there (and under open on a voice's path), so d is under 30275 and
 * d * d fits 32 bits: one 32 x 32 -> 64 multiply. */
static int32_t curve_of(int32_t u)
{
    uint32_t d, d2;
    if (u <= FM_CURVE_ZERO) return u;
    d = (uint32_t)(u - FM_CURVE_ZERO);
    d2 = d * d;
    return u + (int32_t)(((uint64_t)d2 * FM_CURVE_A_Q32 + (1ull << 31)) >> 32);
}

/* The curve for a voice: open stays open without the multiply. */
static int32_t cutoff_curve(int32_t u)
{
    return u >= FM_FILTER_OPEN_CENTS ? u : curve_of(u);
}

/* floor(sqrt(x)), bit by bit: event rate only */
static uint32_t isqrt64(uint64_t x)
{
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

int32_t fm_filter_linear_cents(int32_t cents)
{
    uint64_t y;
    uint32_t s;
    int32_t u;
    if (cents <= FM_CURVE_ZERO) return cents;
    if (cents > 2 * FM_FILTER_OPEN_CENTS) cents = 2 * FM_FILTER_OPEN_CENTS;
    /* C(u) = cents: d = (sqrt(1 + 4 A Y) - 1) / 2A, Y = cents - FM_CURVE_ZERO */
    y = (uint64_t)((int64_t)cents - FM_CURVE_ZERO);
    s = isqrt64((1ull << 32) + 4ull * FM_CURVE_A_Q32 * y); /* Q16 */
    /* s - 65536 is under 25000 (cents is held to 2 x open), so the
     * numerator stays under 8.2e8: a 32-bit divide */
    u = FM_CURVE_ZERO + (int32_t)(((s - 65536u) * 32768u + FM_CURVE_A_Q32_HALF) / FM_CURVE_A_Q32);
    /* the u whose curve value is nearest `cents` (the lower on a tie) */
    while (curve_of(u) < cents) u++;
    while (u > FM_CURVE_ZERO && curve_of(u - 1) >= cents) u--;
    if (u > FM_CURVE_ZERO && cents - curve_of(u - 1) <= curve_of(u) - cents) u--;
    return u;
}

int32_t fm_filter_voice_cents(int32_t cutoff_linear, int32_t track_permille,
                              uint8_t note, int32_t mod_cents)
{
    /* 100 linear cents per semitone, per mille: track * (note - 60) / 10 */
    int32_t track = track_permille * ((int32_t)note - 60) / 10;
    return cutoff_curve(cutoff_linear + track + mod_cents);
}

void fm_filter_clear(fm_filter *f)
{
    int i;
    for (i = 0; i < 4; i++) f->s[i] = f->c[i] = 0;
    f->hp = 0;
}

void fm_filter_reset(fm_filter *f)
{
    fm_filter_clear(f);
    f->a1 = ONE_Q30;
    f->a2 = f->a3 = f->t = f->z3 = f->zt = 0;
    f->res = 0;
    f->a = ONE_Q30;
    f->cut = FM_FILTER_OPEN_CENTS;
    f->rate = 0;
    f->ur = 0;
    f->fade = 0;
    f->hp_g = 0;
}

static int32_t mul_q30(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 30);
}

/* The output is held to +-U_MAX (FM_FILTER_HOLD, 16.0): past anything
 * the pair plays (3.868, a ringed spike) through the bass boost (2.28 x)
 * at a level of 1.0. */
#define U_MAX FM_FILTER_HOLD

static int32_t hold(int32_t u)
{
    return u > U_MAX ? U_MAX : u < -U_MAX ? -U_MAX : u;
}

/*
 * Wide open (fade 0) with the highpass off the filter is a wire, and the
 * fast path in fm_filter_process keeps only the last sample, in s[3]
 * (Q8.24). This sets both lowpasses at that sample and the bandpasses at
 * 0, so the output carries on from where the wire left it, with no step,
 * and settles to the passband's level (Q0 / Q) at the sections' own pace.
 */
static void leave_open(fm_filter *f)
{
    int32_t v = hold(f->s[3]) >> 1;                         /* Q9.23 */
    int i;
    f->s[0] = f->s[2] = 0;
    f->s[1] = f->s[3] = v;
    for (i = 0; i < 4; i++) f->c[i] = 0;
}

/*
 * f0 under the cutoff: PG-8X 2.0.13's two sections' f0 (resonance 93,
 * tests/test_svf.c's method) against the engine's cutoff for the same VCF
 * FREQ, -176 cents at 6769 (F49) to -260 at 2639 (F16) and -246 at
 * 11927 (F88; there set by PG-8X's peak, -261; and F96's peaks, -343 at
 * 13022), every 1200 cents of the
 * cutoff from 0, read with linear interpolation (above F88 PG-8X's f0 nears
 * its Nyquist and goes on the slope from there).
 */
static const int16_t f0_off[13] = { -260, -260, -260, -237, -199, -181, -177, -188, -209, -228, -263, -357, -416 };
/* tan(pi 2^(d / 1200)), d = -14400 .. -1300 by 100 (fc / fs from 1/4096 to 0.47), Q5.27 */
static const int32_t tan_tab[132] = {
    102944, 109065, 115550, 122421, 129701, 137413, 145584, 154241,
    163413, 173130, 183425, 194332, 205888, 218130, 231101, 244843,
    259402, 274827, 291169, 308483, 326827, 346261, 366851, 388665,
    411776, 436262, 462203, 489688, 518806, 549657, 582341, 616969,
    653657, 692526, 733707, 777336, 823560, 872533, 924418, 979389,
    1037628, 1099331, 1164704, 1233965, 1307345, 1385089, 1467457, 1554724,
    1647182, 1745139, 1848923, 1958881, 2075380, 2198811, 2329584, 2468139,
    2614938, 2770473, 2935265, 3109866, 3294860, 3490869, 3698549, 3918597,
    4151754, 4398802, 4660572, 4937947, 5231862, 5543308, 5873339, 6223073,
    6593694, 6986464, 7402719, 7843881, 8311460, 8807063, 9332397, 9889280,
    10479647, 11105560, 11769216, 12472959, 13219292, 14010890, 14850613, 15741525,
    16686910, 17690295, 18755471, 19886522, 21087855, 22364234, 23720824, 25163231,
    26697566, 28330501, 30069350, 31922153, 33897787, 36006088, 38258006, 40665788,
    43243197, 46005792, 48971258, 52159828, 55594803, 59303210, 63316643, 67672340,
    72414584, 77596550, 83282761, 89552409, 96503927, 104261375, 112983562, 122877370,
    134217728, 147378461, 162881623, 181479693, 204299355, 233108465, 270849855, 322815567,
    399582160, 525816175, 775495129, 1518452104
};
#define F0_MAX (-1480)                          /* f0 / fs at most 0.425 (20.4 kHz at 48 kHz) */
/*
 * The top of the range at resonance 0 (#151). PG-8X 2.0.13's top only opens: from VCF FREQ 108 to 127 a saw's
 * harmonics rise with every step and none passes its open level. The sections can't follow it up there: past
 * about a third of the rate the second section's zeros at the origin lift the top over the input (t^2 at
 * Nyquist; +14.6 dB at 19.4 kHz for FREQ 120 at 44.1 kHz). So, as much as there is no peak (V_SLOPE, below):
 * - f0 / fs goes no further than 0.306 (F0_MAX_R0: t 1.43; 13.5 kHz at 44.1 kHz, 14.7 at 48);
 * - the output fades to the input linearly over the top FADE0_CENTS, from 12190 (FREQ 108.6) to open;
 * - the sections take 0.5 more damping (K_TOP0: k 1.85 to 2.35), in over 12510..12870 (FREQ 111..113.6): held
 *   at that f0 and faded, they dip the harmonics near 16 kHz 2.3 dB under PG-8X's without it, 1.1 with. In
 *   Q27: k itself is Q30, where 2.35 is past int32.
 * Ours is not strictly one way: near 12560, where section 1's zeros leave, the harmonics around 17.5 kHz dip
 * about 1.2 dB as the cutoff rises 110 cents (near -18 dB; before #151 they fell 21 dB there).
 * Fitted to PG-8X at 44.1 and 48 kHz with one set of constants (tests/test_vcf_top.c): FREQ 114..124 within
 * 1.3 dB at every harmonic to 19.4 kHz, 110 and 112 within 3.6.
 */
#define F0_MAX_R0   (-2050)
#define FADE0_CENTS 2210
#define FADE0_STEP  (ONE_Q30 / FADE0_CENTS)     /* 485856 */
typedef char fm_require_fade0_fits_q30[(int64_t)FADE0_CENTS * FADE0_STEP <= ONE_Q30 ? 1 : -1];
#define K_TOP0      67108864                    /* 0.5, Q27 */
#define K_TOP0_FROM 12510
#define K_TOP0_STEP 186413                      /* K_TOP0 / 360, rounded down: a cent's */
typedef char fm_require_k_top0_ramp_fits[(int64_t)360 * K_TOP0_STEP <= K_TOP0 ? 1 : -1];
/* How much of that, by w (how much peak, 0 at resonance 0):
 * - the f0 stop and the damping: all at resonance 0, none from w 0.8 (resonance 389: there is a peak, and it
 *   is #168's), 1 - 1.25 w between (V_SLOPE, Q28; resonance 180, VCF RES 8: 0.55; 290, RES 16: 0.28);
 * - the long fade: resonance 0's alone, 1 - 5 w, gone by w 0.2 (about resonance 100, VCF RES 4.5). With a peak
 *   a few per cent of the input against the sections' output, out of phase past f0, cuts a notch (VCF FREQ 114
 *   at resonance 340: a harmonic 23.8 dB under PG-8X's, 12.1 without);
 * - and where the long fade is not all there, the f0 stop gives way over the top FADE_CENTS, to none at open:
 *   there the short fade does the opening, and the stop under it only darkens the top (FREQ 124).
 * So no resonance at VCF FREQ 110..124 reads further from PG-8X than before #151 by more than 0.15 dB rms
 * (VCF RES 1..23 and 52, 56, at 44.1 and 48 kHz). */
#define V_SLOPE     335544320
#define V_W_END     858993460                   /* w 0.8: from there 1 - 1.25 w is 0 or under */
#define VF_SLOPE    5
/* Only at the rates it is fitted for, 44.1 to 48 kHz (fm_filter_rate_cents of them, and every rate between).
 * The fade and the damping are in cents of cutoff and the stop is a share of the rate: at a higher rate the
 * sections never reach the stop, and the long fade to the input cuts a notch (192 kHz: -50 dB at 15.4 kHz at
 * cutoff 12450). Every other rate keeps the top it had, coefficient for coefficient. */
#define TOP0_RATE_LO 14876                      /* 44.1 kHz */
#define TOP0_RATE_HI 15023                      /* 48 kHz */
#define D_MAX (-1383)                           /* f0 / fs at most 0.45 */
/*
 * k = 1 / Q by resonance per mille: PG-8X 2.0.13's (its F49 and F64 fits'
 * mean, tests/test_svf.c), Q30, linear between. Past 935 (its RES 127,
 * the most a JX-8P patch reaches; the RES knob and a CC go on) to Q 13.4 at 1000: it rings long
 * and decays, as PG-8X's never self-oscillates either.
 */
static const int16_t k_pm[14] = { 0, 290, 395, 400, 500, 575, 690, 756, 828, 860, 900, 925, 935, 1000 };
static const int32_t k_tab[14] = { 1555853578, 1107028459, 921274491, 913757962, 780616755, 677527015,
                                   537944002, 458487882, 365072384, 319976243, 248034877, 194877760,
                                   159987123, 79993562 };
/* section 1's zeros 5/8 of the way to the origin (section 2's all the way): PG-8X's top end at F80
 * within 1.7 dB to 4 f0 (none: 8 dB under; both: 4 over). Up toward Nyquist, where the analog
 * prototype isn't to be had and that shift only lifts the top, it tapers to none: full to t 0.4
 * (f0 / fs 0.12), none from t 1.2 (0.28) */
#define Z1_FULL  671088640                     /* 5/8, Q30 */
#define Z1_T0    53687091                      /* t 0.4, Q5.27 */
#define Z1_T1    161061274                     /* t 1.2, Q5.27 */
#define Z1_T0R   40265318                      /* t 0.3, Q5.27: at resonance 0 */
#define Z1_T1R   134217728                     /* t 1.0, Q5.27 */
#define K_TOP    683920082                     /* 0.637 (2 / pi), Q30 */
#define K_93     458487882                     /* k at RES 93 (756), where the f0 offset is measured */
#define RATE48_CENTS 15023                     /* fm_filter_rate_cents(48000) */
#define E_LIFT   912680550                     /* 0.85, Q30 */
#define T2_LIFT  524288                        /* 0.0625 (F80's t^2), Q9.23 */
#define OVER_MAX 3355443                       /* 0.4, Q9.23: no more than about 2.5 dB out (F96) */
#define T_SHIFT  80530637                      /* 0.6, Q5.27: the f0 shift's t at most */
typedef char t_shift_fits[T_SHIFT <= 80530637 ? 1 : -1];   /* c_t t overflows past t 0.63 */
#define T_TRIM   40265318                      /* 0.3, Q5.27: the trim's t at most */
#define Q0_Q30   741021855                     /* 1 / k at resonance 0 (Q 0.69): the level is k Q0, 1.0 there */
#define K_FLOOR  21474836                      /* the level's k, at least 0.02: self-oscillation starts */

/* how much k falls a per mille between the points, Q30 (no divide at control rate; as magnitudes, so
 * no constant looks like a ROM address) */
static const int32_t k_fall[13] = { 1547673, 1769086, 1503306, 1331413, 1374530, 1213766, 1203881,
                                    1297438, 1409255, 1798535, 2126285, 3489064, 1230671 };

static int32_t k_of(int32_t pm)
{
    int i;
    if (pm >= k_pm[13]) return k_tab[13];
    for (i = 0; pm > k_pm[i + 1]; i++) {}
    return k_tab[i] - k_fall[i] * (pm - k_pm[i]);    /* (within 3.5e6 x 290: 32 bits) */
}

/*
 * The top of the range with resonance (#168). PG-8X 2.0.13's filter up there is an analog-style pair: its
 * passband is the resonance's alone at every cutoff (it never becomes a wire: RES 64 -4.6 dB and RES 127 -20.0
 * at VCF FREQ 127 too), its peak grows 5 dB from FREQ 100 to 122 and then leaves the band upward, and it reads
 * the same in Hz at 44.1 and 48 kHz. The sections as they are below the top can't follow it:
 * - section 2's zeros at the origin lift the peak by 1 + t^2 (FREQ 120, RES 127: +18.7 dB over PG-8X's 36.7);
 * - section 1's zeros, a complex pair tapering from 5/8 of the way to the origin to none, cross the top band
 *   as a notch (FREQ 114: 27 dB under PG-8X's at 20 kHz);
 * - f0 stops at F0_MAX where PG-8X's goes on past Nyquist, and the fade to the input lets the level cut go
 *   (+5 dB at RES 64 and +20 at RES 127 between FREQ 120 and 124).
 * So from RT_C0 cents up, at the rates it is fitted for (44.1 to 48 kHz, as #151's top), and as much as the
 * handover's weight says (below):
 * - t, the damping k and section 1's zeros come from tables (engine/res_top_tables.h, a fit to PG-8X made
 *   outside the repository: tools/res_top.py), bilinear between the knots, and linear between the 44.1 and 48
 *   kHz sets for a rate between;
 * - section 1's zeros are a real double zero, (1 + c s)^2: z3 = 2 c + c^2 and zt = t c^2. A real pair has no
 *   notch wherever it sits;
 * - the level is PG-8X's passband alone, k(resonance) Q0, with no trim and no lift taken out;
 * - there is no fade to the input: the open cutoff (and everything a patch asks over it, VCF FREQ 125 up)
 *   plays PG-8X's VCF FREQ 127, where PG-8X's own sum of cutoff sources stops.
 * Past the tables' last resonance (935, VCF RES 127: the RES knob and a CC go on to 1000) k falls on as the
 * engine's own does, x k(resonance) / k(935).
 *
 * The weight is u = u_c u_r: u_c from 0 at RT_C0 to 1 at RT_C1 of cutoff; u_r the handover in resonance, 0 up
 * to FM_FILTER_TOP_RES_LO and 1 from FM_FILTER_TOP_RES_HI (filter.h: f->ur is how much of it plays). Every
 * coefficient is linear in a weight, so none steps with cutoff or resonance. The fade takes u. The sections
 * (t, k, the zeros, the level) take u / (u + (1 - u) fade): u where the old top is all there, and all the new
 * top's where the old one is faded out and not heard (the open cutoff), so that a move of u there moves only
 * the fade's share and not the sections under it.
 *
 * Flash code and flash data: nothing of this is read per sample, and nothing is in RAM but the int16 f->ur.
 * (When the per-sample loops move to RAM text, #304: apply(), rt_read, rt_get, k_of and tan_of stay flash
 * functions, the tables flash data, and mul_q30, which fm_filter_process uses too, must then be shared by
 * both sides.)
 */
#define RT_C0 11500
#define RT_C1 12100
typedef char fm_require_rt_cutoff_span_is_the_fades[RT_C1 - RT_C0 == FADE_CENTS ? 1 : -1];
typedef char fm_require_rt_tables_end_at_open[RT_NC >= 2 && RT_NR >= 2 ? 1 : -1];
#define RT_UR_Q30   65600                       /* 2^30 / FM_FILTER_UR_ONE, rounded down: a unit of f->ur */
typedef char fm_require_rt_ur_fits_q30[(int64_t)(FM_FILTER_UR_ONE - 1) * RT_UR_Q30 < ONE_Q30 ? 1 : -1];
#define RT_K_LAST_INV 27490                     /* 2^12 / k(935): k(resonance) / k(935) by a multiply */

/* A table's value at knot (i, j) and the Q15 fractions to the next knots, x 2^15. */
static int32_t rt_read(const int16_t tab[RT_NC][RT_NR], int i, int j, int32_t fc, int32_t fr)
{
    int32_t a = tab[i][j] * 32768 + (tab[i][j + 1] - tab[i][j]) * fr;
    int32_t b = tab[i + 1][j] * 32768 + (tab[i + 1][j + 1] - tab[i + 1][j]) * fr;
    return a + (int32_t)(((int64_t)(b - a) * fc) >> 15);
}

/* The same between the two rates' sets: wr 0 at 44.1 kHz, 2^15 at 48 kHz. */
static int32_t rt_get(const int16_t tab[2][RT_NC][RT_NR], int i, int j, int32_t fc, int32_t fr, int32_t wr)
{
    int32_t v = rt_read(tab[0], i, j, fc, fr);
    if (wr) v += (int32_t)(((int64_t)(rt_read(tab[1], i, j, fc, fr) - v) * wr) >> 15);
    return v;
}

/* tan(pi f / fs), Q5.27, for f / fs in cents (held to 1/4096 .. 0.45) */
static int32_t tan_of(int32_t d)
{
    int32_t i, r;
    if (d > D_MAX) d = D_MAX;
    if (d < -14400) d = -14400;
    i = (d + 14400) / 100;
    r = (d + 14400) % 100;
    return tan_tab[i] + (int32_t)((((int64_t)(tan_tab[i + 1] - tan_tab[i]) * r) * 42949673) >> 32);   /* / 100 */
}

/* The coefficients from the cutoff, rate and resonance as set: control rate, a few 32-bit divides. */
static void apply(fm_filter *f)
{
    int32_t c = f->cut < 0 ? 0 : f->cut, i, r, d, t, t48, tt, k, kl, trim, den, a1, w, v, vf;
    int32_t u = 0, us = 0, t_old, tn = 0, kn = 0, cn = 0, an = 0;   /* the resonant top (#168) and its weights */
    int was_open = fm_filter_is_wire(f);
    i = c / 1200;
    r = c % 1200;
    d = c + (i >= 12 ? f0_off[12] + (f0_off[12] - f0_off[11]) * r / 1200
                     : f0_off[i] + (f0_off[i + 1] - f0_off[i]) * r / 1200) - f->rate;   /* f0 / fs, cents */
    t = tan_of(d);                                                                       /* Q5.27, <= 6.72 */
    k = k_of(f->res);                                                                    /* Q30, <= 1.45 */
    w = ONE_Q30 - (int32_t)(((int64_t)k * Q0_Q30) >> 30);     /* how much peak: 0 at resonance 0 */
    w = w <= 0 ? 0 : w >= ONE_Q30 / 2 ? ONE_Q30 : 2 * w;      /* twice it, to 1 from about resonance 400 */
    /* how much of resonance 0's top (v) and of its long fade (vf): none with a peak or at a rate it is not
     * fitted for. A resonant patch pays one compare here (w), and further down three tests for zero and
     * a subtract; no multiply */
    v = vf = 0;
    if (w < V_W_END && f->rate >= TOP0_RATE_LO && f->rate <= TOP0_RATE_HI) {
        v = ONE_Q30 - (int32_t)(((int64_t)w * V_SLOPE) >> 28);
        if (v < 0) v = 0;
        if (w < ONE_Q30 / VF_SLOPE) vf = ONE_Q30 - VF_SLOPE * w;
    }
    /* PG-8X's Q falls a little at high cutoffs (F80: 3..10 %, F49: none): k + K_TOP k^2 t, and its
     * passband rises half as much (the level's k: k + half that). Fitted to F80 (t 0.25), and taken no
     * further than t 0.3 (F82): at most 0.4 more, k within 1.85 */
    /* PG-8X's own top: how its sections move at high f0 goes by f0's tan at 48 kHz (PG-8X's), whatever
     * the rate here */
    t48 = tan_of(d + f->rate - RATE48_CENTS);
    tt = t48 < T_TRIM ? t48 : T_TRIM;
    trim = (int32_t)(((((int64_t)k * k) >> 30) * (((int64_t)tt * K_TOP) >> 27)) >> 30);
    kl = k + trim / 2;
    k += trim;
    /* and its f0 moves with the resonance there, the offset being RES 93's: up under it, down over it,
     * by (240 + 900 t) t (k - k93) cents (F80: +150 at RES 0, -32 at 127; F64: +24; F49: +6), its t
     * taken to 0.6 (F96; PG-8X's peaks at F88 and F96, RES 112 and 127) */
    tt = t48 < (k > K_93 ? T_TRIM : T_SHIFT) ? t48 : (k > K_93 ? T_TRIM : T_SHIFT);   /* (up: as before, t 0.3) */
    {
        int64_t c_t = ((int64_t)240 << 27) + 900 * (int64_t)tt;                         /* 240 + 900 t, Q27 */
        /* (c_t t: Q54 cents under 2^63 for t to 0.63, so T_SHIFT 0.6 at most; to Q10 under 2^19;
         * times k - k93, within 2^31: under 2^50) */
        int32_t shift = (int32_t)((((c_t * tt) >> 44) * ((int64_t)k - K_93)) >> 40);
        /* PG-8X's f0 goes no further (F104: 20.4..20.9 kHz); at resonance 0, F0_MAX_R0 */
        int32_t top = F0_MAX;
        if (v) {
            int32_t vs = v, open = FM_FILTER_OPEN_CENTS - c;    /* open >= 0: the cutoff is clamped */
            if (vf != ONE_Q30 && open < FADE_CENTS)             /* gives way under the short fade: vf at open */
                vs = (int32_t)(((int64_t)v * (vf + (((int64_t)(ONE_Q30 - vf) * (open * FADE_STEP)) >> 30))) >> 30);
            top -= (int32_t)(((int64_t)(F0_MAX - F0_MAX_R0) * vs) >> 30);
        }
        if (d + shift > top) shift = top - d;
        if (shift) t = tan_of(d + shift);
    }
    /* the fade of the top below the resonant one (it sets how the resonant top's sections come in) */
    f->fade = f->cut <= FM_FILTER_OPEN_CENTS - FADE_CENTS ? ONE_Q30 : (FM_FILTER_OPEN_CENTS - f->cut) * FADE_STEP;
    if (f->fade != ONE_Q30) {   /* toward its cube, as much as there is a peak (1 - k Q0, as the level's
                                 * fold): the sections' peak near Nyquist (zeros at the origin) to PG-8X's
                                 * height at F104 (+8.8..17.6 dB); the passband is the input's either way,
                                 * and at resonance 0 the fade is as it was */
        int32_t f1 = f->fade, f3;                       /* (w: as section 1's zeros) */
        f3 = (int32_t)(((int64_t)f1 * f1) >> 30);
        f3 = (int32_t)(((int64_t)f3 * f1) >> 30);
        f->fade = f1 - (int32_t)(((int64_t)(f1 - f3) * w) >> 30);
    }
    if (vf) {   /* resonance 0's fade, over the top FADE0_CENTS: toward it by vf */
        int32_t top = FM_FILTER_OPEN_CENTS - f->cut;    /* >= 0: the cutoff is clamped */
        int32_t f0 = top >= FADE0_CENTS ? ONE_Q30 : top * FADE0_STEP;
        f->fade += (int32_t)(((int64_t)(f0 - f->fade) * vf) >> 30);
    }
    t_old = t;                                  /* the zeros' taper and the lift below go by this t */
    if (f->ur > 0 && c > RT_C0 && f->rate >= TOP0_RATE_LO && f->rate <= TOP0_RATE_HI) {     /* the resonant top */
        int32_t ur = f->ur >= FM_FILTER_UR_ONE ? ONE_Q30 : f->ur * RT_UR_Q30;
        int32_t uc = c >= RT_C1 ? ONE_Q30 : (c - RT_C0) * FADE_STEP;
        int32_t res = f->res > rt_rk[RT_NR - 1] ? rt_rk[RT_NR - 1] : f->res < rt_rk[0] ? rt_rk[0] : f->res;
        int32_t fc, fr, wr, kr;
        int ci, rj;
        u = mul_q30(ur, uc);
        us = u;
        if (f->fade != ONE_Q30) {               /* u / (u + (1 - u) fade), to 15 bits: a 32-bit divide */
            uint32_t u15 = (uint32_t)u >> 15, den15 = (uint32_t)(u + mul_q30(ONE_Q30 - u, f->fade)) >> 15;
            us = den15 ? (int32_t)(((u15 << 15) / den15) << 15) : 0;
            if (us > ONE_Q30) us = ONE_Q30;
        }
        for (ci = 0; ci < RT_NC - 2 && c >= rt_ck[ci + 1]; ci++) {}
        for (rj = 0; rj < RT_NR - 2 && res >= rt_rk[rj + 1]; rj++) {}
        fc = ((c - rt_ck[ci]) << 15) / (rt_ck[ci + 1] - rt_ck[ci]);          /* (c is at most the last knot, open) */
        fr = ((res - rt_rk[rj]) << 15) / (rt_rk[rj + 1] - rt_rk[rj]);
        wr = ((f->rate - TOP0_RATE_LO) << 15) / (TOP0_RATE_HI - TOP0_RATE_LO);
        tn = rt_get(rt_t, ci, rj, fc, fr, wr) >> (RT_T_SHIFT + 15 - 27);     /* Q5.27 */
        kn = rt_get(rt_k, ci, rj, fc, fr, wr) >> (RT_K_SHIFT + 15 - 27);     /* Q27 (to 3.0: past Q30's int32) */
        cn = rt_get(rt_c, ci, rj, fc, fr, wr) >> (RT_C_SHIFT + 15 - 30);     /* Q30 */
        /* the level's k, without the trim; under the tables' first resonance (inside the handover) it is that
         * resonance's, as the tables' values are: the handover fades one top in, it does not move it */
        kr = k_of(f->res < rt_rk[0] ? rt_rk[0] : f->res);
        if (f->res > rt_rk[RT_NR - 1]) {        /* past VCF RES 127: on down as the engine's k goes */
            int32_t ratio = (int32_t)(((int64_t)kr * RT_K_LAST_INV) >> 12);
            if (ratio < ONE_Q30) kn = mul_q30(kn, ratio);
        }
        an = (int32_t)(((int64_t)(kr > K_FLOOR ? kr : K_FLOOR) * Q0_Q30) >> 30);
        t += mul_q30(tn - t, us);
    }
    {   /* resonance 0's damping at the top (K_TOP0), by v */
        int32_t up = f->cut - K_TOP0_FROM, k0 = 0, kq;
        if (v && up > 0) k0 = (int32_t)(((int64_t)(up >= 360 ? K_TOP0 : up * K_TOP0_STEP) * v) >> 30);
        kq = (k >> 3) + k0;                     /* Q27 */
        if (u) kq += mul_q30(kn - kq, us);
        /* 1 + t (t + k), Q9.23: t within 6.72 and k within 3.0, Q27, so the product is under 2^59 */
        den = (1 << 23) + (int32_t)(((int64_t)t * (t + kq)) >> 31);
    }
    a1 = (int32_t)((0x80000000u / (uint32_t)(den >> 7)) << 15);                         /* 2^31 / Q16: Q15, to Q30 */
    /* one Newton step, a1 (2 - den a1): 15 bits to 30, so the lowpass's DC gain is 1 (within 1e-8) */
    a1 = (int32_t)(((int64_t)a1 * ((1ll << 31) - (((int64_t)den * a1) >> 23))) >> 30);
    f->a1 = a1;
    f->a2 = (int32_t)(((int64_t)a1 * t) >> 27);
    f->a3 = (int32_t)(((int64_t)f->a2 * t) >> 27);
    f->t = t;
    {   /* section 1's zeros: 5/8 of the way to the origin, to none near Nyquist, from t 0.4 to 1.2 where
         * there is a peak (PG-8X's peaks at F88..F96), from 0.3 to 1.0 at resonance 0 (its slope past f0
         * up to F104, as match.py's VCF_FREQ reads it); between, by w (twice 1 - k Q0, to 1) */
        int32_t c1, c2;
        int32_t zp = t_old <= Z1_T0 ? Z1_FULL : t_old >= Z1_T1 ? 0
                   : (int32_t)(((int64_t)(Z1_T1 - t_old) * 25) >> 2);          /* Z1_FULL / (Z1_T1 - Z1_T0): 6.25 */
        int32_t z0 = t_old <= Z1_T0R ? Z1_FULL : t_old >= Z1_T1R ? 0
                   : (int32_t)(((int64_t)(Z1_T1R - t_old) * 479349029) >> 26);  /* Z1_FULL / 0.7: 50/7, Q26 */
        zp = z0 + (int32_t)(((int64_t)(zp - z0) * w) >> 30);   /* z1 */
        /* as 1 + c1 s + c2 s^2 in s = j tan(pi f / fs): c1 = 2 z1, c2 = z1; the resonant top's (1 + c s)^2 */
        c1 = 2 * zp;
        c2 = zp;
        if (u) {
            c1 += mul_q30(2 * cn - c1, us);
            c2 += mul_q30(mul_q30(cn, cn) - c2, us);
        }
        f->z3 = c1 + c2;                                         /* <= 1.89, Q30 (3 z1 without the top) */
        f->zt = (int32_t)(((int64_t)t * c2) >> 30);              /* t c2, Q5.27 */
    }
    f->a = (int32_t)(((int64_t)(kl > K_FLOOR ? kl : K_FLOOR) * Q0_Q30) >> 30);            /* <= 1.0 (1.2 at the top) */
    {   /* the zero corrections lift the resonant peak past F80 over PG-8X's (F88: 1.4 dB, F96: 4.0):
         * the level takes out 1 + E_LIFT (t^2 - t80^2) of it (none to F80; t^2 - t80^2 held to 0.4, so
         * 1.34 at most; t at the engine's rate, as z1: the lift is the digital sections'), by a 32-bit divide and a Newton step (it in Q9.23, 1 / it in Q30) */
        int64_t over = (((int64_t)t_old * t_old) >> 31) - T2_LIFT;                              /* t^2 past F80's, Q9.23 */
        int32_t e, inv;
        over = over < 0 ? 0 : over > OVER_MAX ? OVER_MAX : over;
        /* a lift of the peak, so as much of it as there is a peak: none at resonance 0, nearly all at 935
         * (1 - k / k0 = 1 - k Q0: the untrimmed k, from the table) */
        over = (over * (ONE_Q30 - (((int64_t)k_of(f->res) * Q0_Q30) >> 30))) >> 30;
        e = (1 << 23) + (int32_t)((over * E_LIFT) >> 30);
        inv = (int32_t)((0x80000000u / (uint32_t)(e >> 7)) << 15);
        inv = (int32_t)(((int64_t)inv * ((1ll << 31) - (((int64_t)e * inv) >> 23))) >> 30);
        f->a = (int32_t)(((int64_t)f->a * inv) >> 30);
    }
    if (u) {    /* the resonant top: its level by the sections' weight, and no fade by u */
        f->a += mul_q30(an - f->a, us);
        f->fade += mul_q30(ONE_Q30 - f->fade, u);
    }
    if (was_open && !fm_filter_is_wire(f)) leave_open(f);
}

void fm_filter_set_cutoff(fm_filter *f, int32_t cutoff_cents,
                          int32_t rate_cents)
{
    if (cutoff_cents < 0) cutoff_cents = 0;
    if (cutoff_cents > FM_FILTER_OPEN_CENTS) cutoff_cents = FM_FILTER_OPEN_CENTS;
    f->cut = (int16_t)cutoff_cents;
    f->rate = (int16_t)rate_cents; /* about 11900 (8 kHz) .. 17400 (192 kHz) */
    apply(f);
}

void fm_filter_set_resonance(fm_filter *f, int32_t permille)
{
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    f->res = (int16_t)permille;
    f->ur = (int16_t)fm_filter_ur_target(f);    /* at once, the handover with it */
    apply(f);
}

void fm_filter_turn_resonance(fm_filter *f, int32_t permille)
{
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    f->res = (int16_t)permille;
    apply(f);                                   /* the handover stays where it plays: fm_filter_handover_step */
}

int fm_filter_handover_step(fm_filter *f)
{
    int32_t to = fm_filter_ur_target(f), d = to - f->ur;
    if (!d) return 0;
    if (d > FM_FILTER_UR_STEP) d = FM_FILTER_UR_STEP;
    if (d < -FM_FILTER_UR_STEP) d = -FM_FILTER_UR_STEP;
    f->ur = (int16_t)(f->ur + d);
    apply(f);
    return f->ur != to ? 3 : 1;
}


/*
 * Every Q8.24 input (to +-128.0) gives a bounded output and no overflow:
 * - The highpass state stays within the input's range. While both are
 *   within +-32.0 the highpass output is within +-64.0 (the boost's within
 *   +-36.5 while both are within +-16.0); louder, it runs in 64 bits and
 *   its output is clamped to +-IN_MAX (64.0). The input level is at most
 *   1.0, so the sections' input is held to +-16.0, Q9.23's 8.0.
 * - Section 1: its input is held to +-16.0 before and after the level, so
 *   it is within 8.0 (Q9.23); its states, linear in it, reach at most 95
 *   (worst-case gain over rate, cutoff and resonance: 189 at +-32.0, the
 *   firmware review's), inside Q9.23's 256.
 * - Section 2: its states are held to +-32.0 (SEC2_MAX). Then v3 is within
 *   43, v1 within 32 + 0.5 x 43 = 53.5 and v2 within 32 + 16 + 43 = 91, so
 *   2 v - ic, the next state before its hold, is within 214: inside
 *   Q9.23's 256. The output, v2 + 3 (v2 - ic2) - t ic1, is summed in 32
 *   bits as ic2 - t ic1 (t within 6.31, D_MAX's: 32 + 6.31 x 32 = 234,
 *   inside Q9.23's 256), halved, plus 4 dv in Q10.22 (dv = v2 - ic2 =
 *   a2 ic1 + a3 v3, with a2 within 0.5 and a3 under 1.0: within 16 + 43
 *   = 59), so within 234 + 4 x 59 = 470: inside Q10.22's 512. Then it is
 *   held to +-16.0 (U_MAX >> 2 in Q10.22).
 * - The products: a1, a2 and a3 are within 1.0, the states within 256 and
 *   the carries under 2^30, so each 64-bit sum is under 2^40.
 */
#define IN_MAX ((1 << 30) - 1)
#define SEC2_MAX (32 << 23)                     /* section 2's states, Q9.23 */
#define WITHIN_32(v) ((uint32_t)(v) + (1u << 29) < (1u << 30)) /* -32.0 <= v < 32.0 */
#define WITHIN_16(v) ((uint32_t)(v) + (1u << 28) < (1u << 29)) /* -16.0 <= v < 16.0 */

static int32_t clamp_in(int32_t x)
{
    return x > IN_MAX ? IN_MAX : x < -IN_MAX ? -IN_MAX : x;
}

/*
 * Highpass step 4 is the JX-8P's HPF position 0, which is not flat but a
 * bass boost. PG-8X 2.0.13 against its flat position (HPF 1; noise,
 * 2026-11-10 spectral probe, #93) is +6.5 dB at 30 Hz, +5.4 at 60, +3.7
 * at 120, +2.6 at 250, +2.1 at 500 and +2.0 at 1 kHz: a low shelf on top
 * of 2 dB of gain. Here it is
 *
 *     y = 1.26 * x + 1.02 * lowpass(x)
 *
 * with the highpass's own one-pole lowpass at 62 Hz, which fits those six
 * points within 0.1 dB. The gain at DC is 2.28 (+7.2 dB). The 1.26 is the
 * input plus BOOST_HI of it.
 */
#define BOOST_HI 279172874  /* 0.26, Q1.30 */
#define BOOST_LO 1095216660 /* 1.02, Q1.30 */

void fm_filter_set_highpass(fm_filter *f, uint8_t step, int32_t rate_cents)
{
    /* corners in cents: off, 100 Hz, 240 Hz, 560 Hz; the boost's 62 Hz */
    static const int32_t corner[FM_FILTER_HIGHPASS_STEPS] = {
        0, 4335, 5851, 7318, 3507
    };
    if (step >= FM_FILTER_HIGHPASS_STEPS) step = FM_FILTER_HIGHPASS_STEPS - 1;
    /* off -> on: start from the signal as it is (output = input), not
     * from state left from the last time it was on, which would click */
    if (!f->hp_g) {
        f->hp = 0;
        if (step && fm_filter_is_wire(f)) leave_open(f); /* hp_g is 0 here */
    }
    f->hp_g = step ? coef_for(corner[step], rate_cents) : 0;
    if (step == FM_FILTER_HIGHPASS_BOOST) f->hp_g = -f->hp_g; /* negative: the boost */
}

/* section 2's states held to +-SEC2_MAX */
static inline int32_t sec_hold(int32_t v)
{
    return v > SEC2_MAX ? SEC2_MAX : v < -SEC2_MAX ? -SEC2_MAX : v;
}

int32_t fm_filter_process(fm_filter *f, int32_t in)
{
    int32_t x;
    if (fm_filter_is_wire(f)) { /* the default patch: a wire (see leave_open) */
        f->s[3] = hold(clamp_in(in));
        return f->s[3];
    }
    /* hp_g < 0: the bass boost; 0: off; > 0: a highpass. Each test here
     * costs about 0.9 % of the FM-1 at full load (emu F), so the boost,
     * the costliest path and the one most factory tones take (23 of 32),
     * is tested first: worst case 49.3 %, against 49.4 % highpass first
     * and 50.0 % off first. */
    if (f->hp_g < 0) {
        if (WITHIN_16(in) && WITHIN_16(f->hp)) { /* 1.26 x + 1.02 lowpass(x) */
            f->hp += mul_q30(in - f->hp, -f->hp_g);
            x = in + mul_q30(in, BOOST_HI) + mul_q30(f->hp, BOOST_LO);
        } else {
            int64_t d;
            f->hp += (int32_t)((((int64_t)in - f->hp) * -f->hp_g) >> 30);
            d = (int64_t)in + (((int64_t)in * BOOST_HI) >> 30) + (((int64_t)f->hp * BOOST_LO) >> 30);
            x = d > IN_MAX ? IN_MAX : d < -IN_MAX ? -IN_MAX : (int32_t)d;
        }
    } else if (!f->hp_g) {
        x = clamp_in(in);
    } else {
        if (WITHIN_32(in) && WITHIN_32(f->hp)) { /* input minus a one-pole lowpass of it */
            f->hp += mul_q30(in - f->hp, f->hp_g);
            x = in - f->hp;
        } else { /* louder than any oscillator: the same in 64 bits */
            int64_t d;
            f->hp += (int32_t)((((int64_t)in - f->hp) * f->hp_g) >> 30);
            d = (int64_t)in - f->hp;
            x = d > IN_MAX ? IN_MAX : d < -IN_MAX ? -IN_MAX : (int32_t)d;
        }
    }
    {
        int32_t a1 = f->a1, a2 = f->a2, a3 = f->a3, i1, i2, v1, dv, v3, y, u;
        int64_t p;
        x = hold(x);                             /* the input held to 16.0 (an input past it acts as it) */
        v3 = (mul_q30(x, f->a) >> 1) - f->s[1];  /* its level (at most 1.2: 19.2 at most), Q9.23 */
        i1 = f->s[0];
        i2 = f->s[1];
        p = (int64_t)a1 * i1 + (int64_t)a2 * v3 + f->c[0];
        v1 = (int32_t)(p >> 30);
        f->c[0] = (int32_t)(p & 0x3FFFFFFF);
        p = (int64_t)a2 * i1 + (int64_t)a3 * v3 + f->c[1];
        dv = (int32_t)(p >> 30);                 /* v2 - i2 */
        f->c[1] = (int32_t)(p & 0x3FFFFFFF);
        f->s[0] = 2 * v1 - i1;
        f->s[1] = i2 + 2 * dv;
        /* section 1's output with its zeros z1 of the way to the origin: v2 + z1 (3 (v2 - i2) - t i1),
         * by 3 z1 and t z1 from apply */
        u = i2 + dv + (int32_t)(((int64_t)dv * f->z3) >> 30) - (int32_t)(((int64_t)i1 * f->zt) >> 27);
        i1 = f->s[2];                            /* section 2, on section 1's */
        i2 = f->s[3];
        v3 = u - i2;
        p = (int64_t)a1 * i1 + (int64_t)a2 * v3 + f->c[2];
        v1 = (int32_t)(p >> 30);
        f->c[2] = (int32_t)(p & 0x3FFFFFFF);
        p = (int64_t)a2 * i1 + (int64_t)a3 * v3 + f->c[3];
        dv = (int32_t)(p >> 30);
        f->c[3] = (int32_t)(p & 0x3FFFFFFF);
        /* zeros at the origin, v2 + 3 (v2 - i2) - t i1 = i2 + 4 dv - t i1, in Q10.22: within 470 (above),
         * under 512; 4 dv is even, so halving i2 - t i1 alone is exact */
        u = ((i2 - (int32_t)(((int64_t)f->t * i1) >> 27)) >> 1) + 2 * dv;
        v1 = 2 * v1 - i1;
        dv = i2 + 2 * dv;
        /* held to +-SEC2_MAX: one test for both (each within it is each + SEC2_MAX within 2 SEC2_MAX
         * unsigned, and so is their OR; an OR past it with both within only takes the exact hold) */
        if ((((uint32_t)v1 + SEC2_MAX) | ((uint32_t)dv + SEC2_MAX)) > 2u * SEC2_MAX) {
            v1 = sec_hold(v1);
            dv = sec_hold(dv);
        }
        f->s[2] = v1;
        f->s[3] = dv;
        if ((uint32_t)u + (U_MAX >> 2) <= (uint32_t)U_MAX >> 1)   /* within +-U_MAX / 4: one test */
            y = u * 4;                                            /* Q8.24 */
        else
            y = u < 0 ? -U_MAX : U_MAX;
        if (f->fade == ONE_Q30)
            return y;
        return x + mul_q30(y - x, f->fade);     /* (the top of the range: to the input, held as a wire's) */
    }
}
