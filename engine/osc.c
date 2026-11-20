/*
 * osc.c - band-limited oscillator.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The saw is a naive ramp with a polyBLEP (polynomial band-limited step)
 * correction on the two samples around each wrap, which removes most of
 * the aliasing a bare ramp produces. See docs/fixed-point.md. It falls, as
 * PG-8X's does (#260); the pulse and square are made of rising ones.
 */
#include "osc.h"

/* Keeps a rare path out of line so the per-sample path around it stays small. */
#if defined(__GNUC__) || defined(__clang__)
#define FM_NOINLINE __attribute__((noinline))
#else
#define FM_NOINLINE
#endif

/*
 * Note frequencies for MIDI notes 120..131 in Q16.16 Hz
 * (440 * 2^((n - 69) / 12) * 65536, rounded). Lower octaves are exact
 * right shifts of these. Regenerate with tools/gen_pitch_table.py.
 */
static const uint32_t top_octave_hz_q16[12] = {
    548668578u, 581294109u, 615859655u, 652480576u,
    691279090u, 732384684u, 775934544u, 822074013u,
    870957077u, 922746880u, 977616265u, 1035748353u
};

/* Note frequency in Q16.16 Hz for a whole note 0..131. */
static uint32_t note_hz_q16(uint32_t note)
{
    return top_octave_hz_q16[note % 12] >> (10 - note / 12);
}

/*
 * 2^(f / 12) in Q2.30 for a fraction of a semitone f (Q16, 0..1), by the
 * series e^y = 1 + y + y^2/2 + y^3/6 with y = f * ln 2 / 12 (y < 0.058,
 * so the error is under 0.001 cent).
 */
static uint32_t semitone_ratio_q30(uint32_t f)
{
    int64_t y = (int64_t)(((uint64_t)f * 62021760u) >> 16); /* ln 2 / 12, Q30 */
    int64_t y2 = (y * y) >> 30, y3 = (y2 * y) >> 30;
    return (uint32_t)((1 << 30) + y + y2 / 2 + (int32_t)y3 / 6);
}

uint32_t fm_osc_pitch_inc(int32_t pitch, uint32_t sample_rate)
{
    const int32_t octave = 12 << 16;
    int32_t up = 0, down = 0;
    uint32_t note, frac;
    uint64_t hz_q16, inc;
    if (sample_rate == 0) return 0; /* never divide by zero: no trap on pi32v2 */
    /* beyond +-256 semitones the result is pinned anyway; keeps sums small */
    if (pitch > (256 << 16)) pitch = 256 << 16;
    if (pitch < -(256 << 16)) pitch = -(256 << 16);
    /* fold into the table by whole octaves (exact factors of two) */
    if (pitch > FM_OSC_MAX_PITCH) {
        up = (pitch - FM_OSC_MAX_PITCH + octave - 1) / octave;
        pitch -= up * octave;
    } else if (pitch < 0) {
        down = (octave - 1 - pitch) / octave;
        pitch += down * octave;
    }
    note = (uint32_t)pitch >> 16;
    frac = (uint32_t)pitch & 0xFFFFu;
    hz_q16 = note_hz_q16(note);
    if (frac) hz_q16 = (hz_q16 * semitone_ratio_q30(frac)) >> 30;
    inc = (hz_q16 << 16) / sample_rate;
    if (up) inc = up > 16 ? FM_OSC_MAX_INC : inc << up;
    inc >>= down;
    return inc > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)inc;
}

void fm_osc_reset(fm_osc *o)
{
    o->phase = 0;
    o->inc = 0;
}

void fm_osc_set_inc(fm_osc *o, uint32_t inc)
{
    o->inc = inc > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : inc;
}

/*
 * The spike (FM_WAVE_SPIKE, #150): PG-8X 2.0.13's PULSE. Fitted to #150's
 * measurements (its level against the square at C2..C6 and its partials
 * at C4, C5 and C6, within 0.81 dB rms), it is an impulse train of area
 * SPIKE_AREA (in the square's amplitude x seconds) through two one-pole
 * lowpasses at SPIKE_LP_HZ, in its periodic steady state, less its mean.
 * With u the time since the impulse, tau the lowpasses' time constant, T
 * the period and q = e^(-T / tau):
 *   y(u) = (A / tau) e^(-u / tau) (u / tau / (1 - q) + (T / tau) q / (1 - q)^2) - A / T
 * A closed form of the phase, so the spike has no state: every path that
 * reads a wave at its phase plays it alike. It starts from 0 at each
 * impulse (continuous), so it needs no polyBLEP. The constants come from
 * the unmodulated pitch, on events, with no division per sample.
 */
#define SPIKE_LP_HZ 3050u
#define SPIKE_KQ 1811899801u /* 2 pi SPIKE_LP_HZ / ln 2 x 2^16 */
#define SPIKE_KA 94258198    /* A 2 pi SPIKE_LP_HZ (A / tau) in Q8.24: 5.618 */
#define SPIKE_A_Q24 4919u    /* A x 2^24 (A = 2.9317e-4 s) */
#define LN2_Q16 45426u

/* 2^(-i / 64), Q30, i = 0..64 */
static const int32_t exp2_tab[65] = {
    1073741824, 1062175491, 1050733751, 1039415261, 1028218693, 1017142735,
    1006186087, 995347464, 984625594, 974019220, 963527098, 953147997,
    942880699, 932724001, 922676710, 912737649, 902905651, 893179563,
    883558244, 874040567, 864625413, 855311680, 846098274, 836984114,
    827968132, 819049271, 810226483, 801498734, 792865000, 784324269,
    775875538, 767517817, 759250125, 751071493, 742980960, 734977579,
    727060411, 719228525, 711481005, 703816941, 696235434, 688735596,
    681316545, 673977412, 666717336, 659535466, 652430958, 645402981,
    638450708, 631573326, 624770026, 618040012, 611382493, 604796689,
    598281827, 591837143, 585461881, 579155293, 572916640, 566745190,
    560640218, 554601009, 548626854, 542717053, 536870912
};

/* The spike's 2^(-f), f = (i + 0.5) / 1024 (each step's middle), Q15: read with
 * no interpolation, 1 / 1024 of an octave a step, within +-0.034 % (-69 dB).
 * From: round(32768 * 2^(-(i + 0.5) / 1024)). */
static const uint16_t spike_exp[1024] = {
    32757, 32735, 32713, 32690, 32668, 32646, 32624, 32602, 32580, 32558, 32536, 32514, 32492, 32470, 32448, 32426,
    32404, 32382, 32360, 32338, 32316, 32295, 32273, 32251, 32229, 32207, 32185, 32164, 32142, 32120, 32098, 32077,
    32055, 32033, 32012, 31990, 31968, 31947, 31925, 31903, 31882, 31860, 31839, 31817, 31796, 31774, 31753, 31731,
    31710, 31688, 31667, 31645, 31624, 31603, 31581, 31560, 31538, 31517, 31496, 31474, 31453, 31432, 31411, 31389,
    31368, 31347, 31326, 31304, 31283, 31262, 31241, 31220, 31199, 31178, 31157, 31135, 31114, 31093, 31072, 31051,
    31030, 31009, 30988, 30967, 30946, 30925, 30904, 30884, 30863, 30842, 30821, 30800, 30779, 30758, 30738, 30717,
    30696, 30675, 30654, 30634, 30613, 30592, 30572, 30551, 30530, 30510, 30489, 30468, 30448, 30427, 30406, 30386,
    30365, 30345, 30324, 30304, 30283, 30263, 30242, 30222, 30201, 30181, 30160, 30140, 30120, 30099, 30079, 30059,
    30038, 30018, 29998, 29977, 29957, 29937, 29916, 29896, 29876, 29856, 29836, 29815, 29795, 29775, 29755, 29735,
    29715, 29695, 29674, 29654, 29634, 29614, 29594, 29574, 29554, 29534, 29514, 29494, 29474, 29454, 29434, 29414,
    29395, 29375, 29355, 29335, 29315, 29295, 29275, 29256, 29236, 29216, 29196, 29177, 29157, 29137, 29117, 29098,
    29078, 29058, 29039, 29019, 28999, 28980, 28960, 28940, 28921, 28901, 28882, 28862, 28843, 28823, 28804, 28784,
    28765, 28745, 28726, 28706, 28687, 28668, 28648, 28629, 28609, 28590, 28571, 28551, 28532, 28513, 28493, 28474,
    28455, 28436, 28416, 28397, 28378, 28359, 28340, 28320, 28301, 28282, 28263, 28244, 28225, 28206, 28186, 28167,
    28148, 28129, 28110, 28091, 28072, 28053, 28034, 28015, 27996, 27977, 27958, 27940, 27921, 27902, 27883, 27864,
    27845, 27826, 27807, 27789, 27770, 27751, 27732, 27713, 27695, 27676, 27657, 27639, 27620, 27601, 27582, 27564,
    27545, 27527, 27508, 27489, 27471, 27452, 27434, 27415, 27396, 27378, 27359, 27341, 27322, 27304, 27285, 27267,
    27248, 27230, 27212, 27193, 27175, 27156, 27138, 27120, 27101, 27083, 27065, 27046, 27028, 27010, 26991, 26973,
    26955, 26937, 26918, 26900, 26882, 26864, 26846, 26828, 26809, 26791, 26773, 26755, 26737, 26719, 26701, 26683,
    26665, 26647, 26629, 26610, 26592, 26574, 26556, 26539, 26521, 26503, 26485, 26467, 26449, 26431, 26413, 26395,
    26377, 26359, 26342, 26324, 26306, 26288, 26270, 26253, 26235, 26217, 26199, 26182, 26164, 26146, 26129, 26111,
    26093, 26076, 26058, 26040, 26023, 26005, 25987, 25970, 25952, 25935, 25917, 25900, 25882, 25865, 25847, 25830,
    25812, 25795, 25777, 25760, 25742, 25725, 25708, 25690, 25673, 25655, 25638, 25621, 25603, 25586, 25569, 25551,
    25534, 25517, 25500, 25482, 25465, 25448, 25431, 25413, 25396, 25379, 25362, 25345, 25328, 25310, 25293, 25276,
    25259, 25242, 25225, 25208, 25191, 25174, 25157, 25140, 25123, 25106, 25089, 25072, 25055, 25038, 25021, 25004,
    24987, 24970, 24953, 24936, 24919, 24903, 24886, 24869, 24852, 24835, 24818, 24802, 24785, 24768, 24751, 24735,
    24718, 24701, 24684, 24668, 24651, 24634, 24618, 24601, 24584, 24568, 24551, 24534, 24518, 24501, 24485, 24468,
    24452, 24435, 24418, 24402, 24385, 24369, 24352, 24336, 24319, 24303, 24287, 24270, 24254, 24237, 24221, 24205,
    24188, 24172, 24155, 24139, 24123, 24106, 24090, 24074, 24058, 24041, 24025, 24009, 23992, 23976, 23960, 23944,
    23928, 23911, 23895, 23879, 23863, 23847, 23831, 23814, 23798, 23782, 23766, 23750, 23734, 23718, 23702, 23686,
    23670, 23654, 23638, 23622, 23606, 23590, 23574, 23558, 23542, 23526, 23510, 23494, 23478, 23462, 23447, 23431,
    23415, 23399, 23383, 23367, 23352, 23336, 23320, 23304, 23288, 23273, 23257, 23241, 23225, 23210, 23194, 23178,
    23163, 23147, 23131, 23116, 23100, 23084, 23069, 23053, 23038, 23022, 23006, 22991, 22975, 22960, 22944, 22929,
    22913, 22898, 22882, 22867, 22851, 22836, 22820, 22805, 22789, 22774, 22759, 22743, 22728, 22712, 22697, 22682,
    22666, 22651, 22636, 22620, 22605, 22590, 22574, 22559, 22544, 22529, 22513, 22498, 22483, 22468, 22453, 22437,
    22422, 22407, 22392, 22377, 22362, 22346, 22331, 22316, 22301, 22286, 22271, 22256, 22241, 22226, 22211, 22196,
    22181, 22166, 22151, 22136, 22121, 22106, 22091, 22076, 22061, 22046, 22031, 22016, 22001, 21986, 21971, 21957,
    21942, 21927, 21912, 21897, 21882, 21868, 21853, 21838, 21823, 21808, 21794, 21779, 21764, 21749, 21735, 21720,
    21705, 21691, 21676, 21661, 21647, 21632, 21617, 21603, 21588, 21574, 21559, 21544, 21530, 21515, 21501, 21486,
    21472, 21457, 21442, 21428, 21413, 21399, 21384, 21370, 21356, 21341, 21327, 21312, 21298, 21283, 21269, 21255,
    21240, 21226, 21211, 21197, 21183, 21168, 21154, 21140, 21126, 21111, 21097, 21083, 21068, 21054, 21040, 21026,
    21011, 20997, 20983, 20969, 20955, 20940, 20926, 20912, 20898, 20884, 20870, 20856, 20841, 20827, 20813, 20799,
    20785, 20771, 20757, 20743, 20729, 20715, 20701, 20687, 20673, 20659, 20645, 20631, 20617, 20603, 20589, 20575,
    20561, 20547, 20533, 20519, 20506, 20492, 20478, 20464, 20450, 20436, 20422, 20409, 20395, 20381, 20367, 20353,
    20340, 20326, 20312, 20298, 20285, 20271, 20257, 20244, 20230, 20216, 20202, 20189, 20175, 20162, 20148, 20134,
    20121, 20107, 20093, 20080, 20066, 20053, 20039, 20026, 20012, 19998, 19985, 19971, 19958, 19944, 19931, 19917,
    19904, 19890, 19877, 19863, 19850, 19837, 19823, 19810, 19796, 19783, 19770, 19756, 19743, 19729, 19716, 19703,
    19689, 19676, 19663, 19650, 19636, 19623, 19610, 19596, 19583, 19570, 19557, 19543, 19530, 19517, 19504, 19491,
    19477, 19464, 19451, 19438, 19425, 19412, 19398, 19385, 19372, 19359, 19346, 19333, 19320, 19307, 19294, 19281,
    19268, 19255, 19241, 19228, 19215, 19202, 19189, 19176, 19164, 19151, 19138, 19125, 19112, 19099, 19086, 19073,
    19060, 19047, 19034, 19021, 19008, 18996, 18983, 18970, 18957, 18944, 18931, 18919, 18906, 18893, 18880, 18867,
    18855, 18842, 18829, 18816, 18804, 18791, 18778, 18766, 18753, 18740, 18728, 18715, 18702, 18690, 18677, 18664,
    18652, 18639, 18626, 18614, 18601, 18589, 18576, 18563, 18551, 18538, 18526, 18513, 18501, 18488, 18476, 18463,
    18451, 18438, 18426, 18413, 18401, 18388, 18376, 18363, 18351, 18339, 18326, 18314, 18301, 18289, 18277, 18264,
    18252, 18240, 18227, 18215, 18203, 18190, 18178, 18166, 18153, 18141, 18129, 18117, 18104, 18092, 18080, 18068,
    18055, 18043, 18031, 18019, 18007, 17994, 17982, 17970, 17958, 17946, 17934, 17921, 17909, 17897, 17885, 17873,
    17861, 17849, 17837, 17825, 17813, 17800, 17788, 17776, 17764, 17752, 17740, 17728, 17716, 17704, 17692, 17680,
    17668, 17656, 17645, 17633, 17621, 17609, 17597, 17585, 17573, 17561, 17549, 17537, 17525, 17514, 17502, 17490,
    17478, 17466, 17454, 17443, 17431, 17419, 17407, 17395, 17384, 17372, 17360, 17348, 17337, 17325, 17313, 17302,
    17290, 17278, 17266, 17255, 17243, 17231, 17220, 17208, 17196, 17185, 17173, 17162, 17150, 17138, 17127, 17115,
    17104, 17092, 17080, 17069, 17057, 17046, 17034, 17023, 17011, 17000, 16988, 16977, 16965, 16954, 16942, 16931,
    16919, 16908, 16896, 16885, 16874, 16862, 16851, 16839, 16828, 16817, 16805, 16794, 16782, 16771, 16760, 16748,
    16737, 16726, 16714, 16703, 16692, 16681, 16669, 16658, 16647, 16635, 16624, 16613, 16602, 16590, 16579, 16568,
    16557, 16546, 16534, 16523, 16512, 16501, 16490, 16479, 16467, 16456, 16445, 16434, 16423, 16412, 16401, 16390
};

/* 2^(-e), e in Q16 (>= 0), Q30. */
static int32_t exp2_neg(uint64_t e)
{
    uint32_t n, i, f;
    int32_t a, b;
    if (e >= (30u << 16)) return 0;
    n = (uint32_t)(e >> 16);
    i = (uint32_t)(e >> 10) & 63u;
    f = (uint32_t)e & 1023u;
    a = exp2_tab[i];
    b = exp2_tab[i + 1];
    return (a - (int32_t)(((int64_t)(a - b) * f) >> 10)) >> n;
}

/*
 * 1 / dt as r * 2^(sh - 62): Newton's reciprocal (recip_seed, recip_step)
 * of dt shifted up to 2^31..2^32, as derive() takes osc1's. No division.
 */
static int64_t recip_seed(int64_t m);
static int64_t recip_step(int64_t m, int64_t x);
static uint32_t recip_of(uint32_t dt, uint8_t *sh)
{
    uint32_t m = dt;
    uint8_t s = 0;
    while (!(m & 0x80000000u)) {
        m <<= 1;
        s++;
    }
    *sh = s;
    return (uint32_t)recip_step(m >> 2, recip_step(m >> 2, recip_step(m >> 2, recip_seed(m >> 2))));
}

/* The spike's constants for oscillator `i`, from its increment as played
 * (pitch modulation included): on events and at the control rate, with no
 * division. */
static void update_spike(fm_osc_pair *p, uint8_t i)
{
    uint32_t dt = p->osc[i].inc, r, q;
    uint64_t g, g0, tt;
    int64_t p1, m;
    uint8_t sh;
    if (!dt || !p->sp_kq) {
        p->sp_g[i] = 0;
        p->sp_p1[i] = p->sp_zc[i] = p->sp_mean[i] = 0;
        return;
    }
    /* the decay per 2^32 of phase, Q16 log2: (1 / dt) x 2^32 x the decay
     * per sample (sp_kq: 2 pi f / (rate ln 2)) */
    if (dt < 4u) dt = 4u; /* keeps the shift below non-negative */
    r = recip_of(dt, &sh); /* 1 / dt = r x 2^(sh - 62) */
    g = ((uint64_t)p->sp_kq * r) >> (30 - sh);
    if (g > 0xFFFFFFFFu) g = 0xFFFFFFFFu;
    g0 = g;
    if (g < (1u << 16))                         /* past about 27 kHz (96 kHz rates and up): its shape at
                                                 * that pitch, one halving a period (q 0.5), where 1 / (1 - q)
                                                 * below stays in its reciprocal's range, and the mean
                                                 * below that period's; bounded, and above hearing */
        g = 1u << 16;
    p->sp_g[i] = (uint32_t)g;
    q = (uint32_t)exp2_neg(g);                 /* e^(-T / tau), Q30 */
    /* 1 / (1 - q), Q16, by Newton's reciprocal: 1 - q is 0.5..1 for any
     * pitch under 27 kHz (q under 0.5 while T / tau is over 0.69) */
    m = (int64_t)((1u << 30) - q);
    p1 = recip_step(m, recip_step(m, recip_seed(m))) >> 14;
    tt = (g * LN2_Q16) >> 16;                  /* T / tau, Q16 */
    {
        /* spike_at's two terms, folded: KA ln2 / (1 - q) on u / tau in log2 (Q26: 1 / (1 - q) is at
         * most 5.5 at the pitch limit, half the rate, so this is under 21.4: 1.44e9), and KA z's
         * constant (T / tau) q / (1 - q)^2 (Q22) */
        int64_t zc = ((((int64_t)((tt * q) >> 30)) * p1 >> 16) * p1) >> 16;   /* Q16 */
        p->sp_p1[i] = (int32_t)((((int64_t)SPIKE_KA * LN2_Q16) * p1) >> 30);
        p->sp_zc[i] = (int32_t)(((int64_t)SPIKE_KA * zc) >> 18);
    }
    m = (int64_t)(((uint64_t)p->sp_ar * dt) >> 32);  /* A / T */
    p->sp_mean[i] = (int32_t)(g0 < g ? (m * (int64_t)g0) >> 16 : m);   /* (clamped, g is 2^16: the shape's T) */
}

/* The spike of oscillator `i` at phase t (the time since its impulse),
 * Q8.24: e^(-u / tau) (spike_exp, no interpolation) times its linear term
 * w = a v + b (sp_p1, Q26, and sp_zc, Q22: w in Q9.22, under 128), less the
 * mean. Three 32 x 32 -> 64 multiplies: past 30 halvings of the decay (v
 * at 30 << 16) the impulse's tail is 0 and only the mean is left, and
 * under it v is under 2^21, so v a fits 64 bits and w 32. */
static int32_t spike_at(const fm_osc_pair *p, uint8_t i, uint32_t t)
{
    uint32_t v = (uint32_t)(((uint64_t)t * p->sp_g[i]) >> 32); /* u / tau in log2, Q16 */
    int32_t e, w;
    if (v >= (30u << 16)) return -p->sp_mean[i];
    e = (int32_t)(((uint32_t)spike_exp[(v >> 6) & 1023u] << 15) >> (v >> 16));   /* e^(-u / tau), Q30 */
    w = (int32_t)(((int64_t)v * p->sp_p1[i]) >> 20) + p->sp_zc[i];
    return (int32_t)(((int64_t)e * w) >> 28) - p->sp_mean[i];
}

/* polyBLEP rising saw at phase t with increment dt, Q8.24 (the pulse's and square's two). */
static int32_t saw_at(uint32_t t, uint32_t dt)
{
    /* naive saw 2t - 1 as Q0.31: -1.0 at t = 0, rising to +1.0 */
    int64_t v = (int32_t)(t ^ 0x80000000u);

    if (dt) {
        int64_t blep = 0; /* Q16 */
        if (t < dt) { /* just after the wrap: x = t/dt in [0, 1) */
            int64_t x = (int64_t)(((uint64_t)t << 16) / dt);
            blep = 2 * x - ((x * x) >> 16) - 65536;
        } else if (t > 0u - dt) { /* just before: x = (1 - t)/dt */
            int64_t x = (int64_t)(((uint64_t)(0u - t) << 16) / dt);
            blep = ((x * x) >> 16) - 2 * x + 65536;
        }
        v -= blep * 32768; /* Q16 -> Q0.31 (no left shift of negatives) */
    }
    return (int32_t)(v >> 7);
}

/*
 * Pulse of duty `width` (Q0.32 of a cycle) as the difference of two
 * polyBLEP saws offset by the width: saw(t) - saw(t + width). That is
 * +2(1 - width) for the last `width` of the cycle and -2 width for the
 * rest: peak-to-peak 2.0 and no DC, like an AC-coupled analog pulse. At
 * 50% that is +-1.0, the square; other widths are scaled by pw_gain.
 * `dt0` is the first saw's polyBLEP increment (0 right after a sync
 * restart, which brings its own correction), `dt` the second's. Kept out of
 * line: inlined at -Os, it makes wave_next save four more registers on
 * every call, the saw's included (+1.7 % at 8 voices in fm1-emu).
 */
static FM_NOINLINE int32_t pulse_at(uint32_t t, uint32_t dt0, uint32_t dt, uint32_t width)
{
    return saw_at(t, dt0) - saw_at(t + width, dt);
}

/*
 * The saw wave, falling from +1.0 at t = 0 to -1.0: the rising one negated.
 * PG-8X's saw falls where its square and spike rise (#260). Running free
 * that can't be heard, as the phase between the DCOs drifts; sync and the
 * ring hold it, and there the direction shapes the tone.
 */
static inline int32_t saw_fall(uint32_t t, uint32_t dt)
{
    return -saw_at(t, dt);
}

static void ring_coef(fm_osc_pair *p);

void fm_osc_pair_reset(fm_osc_pair *p, uint32_t seed)
{
    uint8_t i;
    for (i = 0; i < 2; i++) {
        fm_osc_reset(&p->osc[i]);
        p->wave[i] = FM_WAVE_SAW;
        p->width[i] = 0x80000000u;
        p->pw[i] = 0x80000000u;
        p->pw_gain[i] = 1 << 30;
        p->tune[i] = 0;
        p->base[i] = 0;
    }
    p->mod_oct[0] = p->mod_oct[1] = 0;
    p->pw_mod = 0;
    p->level[0] = p->req[0] = FM_OSC_LEVEL_FULL;
    p->level[1] = p->req[1] = 0;
    p->sync = 0;
    p->sync_after = 0;
    p->sync_pend = 0;
    p->dc_sum = 0;
    p->dc = 0;
    p->xmod = 0;
    p->ring = 0;
    p->special = 0;
    p->only1 = 1;
    p->xmod_inc = 0;
    p->recip0 = 0;
    p->recip0_sh = 0;
    p->noise = seed ? seed : 0x2545F491u;
    p->sp_kq = p->sp_ar = 0;
    for (i = 0; i < 2; i++) {
        p->sp_g[i] = 0;
        p->sp_p1[i] = p->sp_zc[i] = p->sp_mean[i] = 0;
    }
    ring_coef(p);
}

/* osc1 at full alone with nothing special: fm_osc_pair_next's fast path */
static void update_only1(fm_osc_pair *p)
{
    p->only1 = (uint8_t)(p->level[0] == FM_OSC_LEVEL_FULL && p->level[1] == 0 &&
                         !p->special);
}

/*
 * Each wave's level against the same oscillator's saw, Q1.30, as PG-8X
 * 2.0.13 plays them (#132: each DCO alone, steady C2..C6): square +0.76 dB
 * on DCO-1 and -2.38 on DCO-2 over the engine's (+4.8 dB re the saw before),
 * noise -1.34 and -4.26. Saw and pulse 1.0: unchanged. The spike (#150)
 * at the square's level on DCO-1, and 3.6 dB under that on DCO-2.
 */
static const int32_t wave_level[2][FM_WAVE_COUNT] = {
    { 1 << 30, 1 << 30, 1171925138, 920237379, 1171925138 }, /* osc1: saw, pulse, square, noise, spike */
    { 1 << 30, 1 << 30, 816394123, 657506227, 774283260 },    /* osc2 */
};

/* The level played: as set, times the wave's level (exactly as set at 1.0). */
static void apply_level(fm_osc_pair *p, uint8_t which)
{
    int32_t g = wave_level[which][p->wave[which]];
    p->level[which] = g == 1 << 30 ? p->req[which]
                                   : (int32_t)(((int64_t)p->req[which] * g) >> 30);
    /* the ring is silent with osc2 unheard: osc1 alone stays the fast path */
    p->special = (uint8_t)(p->sync || p->xmod || (p->ring && p->level[1]));
    update_only1(p);
    ring_coef(p);
}

void fm_osc_pair_set_level(fm_osc_pair *p, uint8_t which, int32_t level)
{
    if (which > 1) return;
    if (level < 0) level = 0;
    if (level > FM_OSC_LEVEL_FULL) level = FM_OSC_LEVEL_FULL;
    p->req[which] = level;
    apply_level(p, which);
}

/*
 * Gain (Q1.30) that brings a pulse of duty `w` to a peak of 1.0, like the
 * saw: the pulse peaks at 2 max(w, 1 - w), so the gain is 1 / that. One
 * division, on events only.
 */
static int32_t pulse_gain(uint32_t w)
{
    uint64_t m = w >= 0x80000000u ? w : 0u - w; /* max(w, 1 - w), Q0.32 */
    return (int32_t)((1ull << 61) / m);
}

/*
 * pulse_gain without the divide, for widths the LFO moves every control
 * tick: 1 / (2 max(w, 1 - w)) from the seed 48/17 - 32/17 m and two
 * Newton steps, x = x (2 - m x), good to about 1e-5 (0.0001 dB).
 */
/* Toward 1 / m for m in 0.5..1 (Q30): the seed, then Newton steps (Q30). */
static int64_t recip_seed(int64_t m)
{
    return 3031741621ll - ((2021161081ll * m) >> 30);
}

static int64_t recip_step(int64_t m, int64_t x)
{
    return (x * ((1ll << 31) - ((m * x) >> 30))) >> 30;
}

static int32_t pulse_gain_fast(uint32_t w)
{
    int64_t m = (w >= 0x80000000u ? w : 0u - w) >> 2; /* Q30, 0.5..1 */
    return (int32_t)(recip_step(m, recip_step(m, recip_seed(m))) >> 1);
}

/* 1 % of a cycle, Q0.32: the narrowest pulse the LFO can make. */
#define PW_MIN 42949673u

/*
 * The duty oscillator `i` plays: its width, kept at least two samples
 * (two increments) from either edge of the cycle. A narrower pulse falls
 * inside the polyBLEP's two-sample span, so it is mostly aliasing; at C7
 * a 1% pulse is 0.2 samples wide. Above a quarter of the sample rate
 * there is room only for a square.
 */
static void update_pw(fm_osc_pair *p, uint8_t i)
{
    /* limits from the unmodulated pitch: the LFO's phase never sets the
     * timbre (docs/modulation.md#pitch) */
    uint32_t inc = p->base[i], w = p->width[i], lo;
    if (p->pw_mod) {
        int64_t m = (int64_t)w + p->pw_mod;
        if (m < PW_MIN) m = PW_MIN;
        if (m > 0x100000000ll - PW_MIN) m = 0x100000000ll - PW_MIN;
        w = (uint32_t)m;
    }
    if (inc >= 0x40000000u) {
        w = 0x80000000u;
    } else if (inc) { /* no pitch yet: nothing to limit (0 - 0 would be 0) */
        lo = inc << 1;
        if (w < lo) w = lo;
        if (w > 0u - lo) w = 0u - lo;
    }
    p->pw[i] = w;
    p->pw_gain[i] = p->pw_mod ? pulse_gain_fast(w) : pulse_gain(w);
}

/*
 * What sync and cross-mod need from the increments, on every change of
 * them while either is on (events and the LFO's control ticks; no
 * division). recip0 / 2^recip0_sh is 1 / osc1's increment: osc1's increment
 * shifted up to 2^31..2^32 is m, and 2^62 / m (2^30..2^31) comes from three
 * Newton steps, good to the Q30 rounding. xmod_inc is osc2's increment
 * times the cross-mod depth.
 */
static inline void derive(fm_osc_pair *p)
{
    uint32_t m = p->osc[0].inc;
    uint8_t s = 0;
    if (!p->special) return;
    p->xmod_inc = (uint32_t)(((uint64_t)p->osc[1].inc * (uint32_t)p->xmod) >> 30);
    if (m == 0) {
        p->recip0 = 0;
        p->recip0_sh = 0;
        return;
    }
    while (!(m & 0x80000000u)) {
        m <<= 1;
        s++;
    }
    p->recip0 = (uint32_t)recip_step(m >> 2, recip_step(m >> 2, recip_step(m >> 2, recip_seed(m >> 2))));
    p->recip0_sh = (uint8_t)(46 - s); /* t / inc0 in Q16 = t * recip0 >> recip0_sh */
}

/* derive() out of line, for the event-rate setters (one copy, not one in
 * each); the LFO's per-tick fm_osc_pair_set_pitch_mod keeps its own. */
static FM_NOINLINE void update_derived(fm_osc_pair *p)
{
    derive(p);
}

void fm_osc_pair_set_pw_mod(fm_osc_pair *p, int32_t offset)
{
    uint8_t i;
    if (offset == p->pw_mod) return;
    p->pw_mod = offset;
    for (i = 0; i < 2; i++)
        if (p->wave[i] == FM_WAVE_PULSE) update_pw(p, i);
}

/*
 * 2^x for x in Q16.16 octaves, times `inc`, capped just under Nyquist.
 * The fraction uses a quartic fit of 2^f on [0, 1) (Q30 coefficients,
 * under 0.011 cent off); whole octaves are shifts. No division.
 */
static FM_NOINLINE uint32_t mod_inc(uint32_t inc, int32_t x)
{
    static const int64_t c[4] = { 744149770, 259040010, 56213420, 14325366 };
    int32_t k;
    int64_t f, acc;
    uint64_t out;
    if (x == 0) return inc;
    k = x >> 16; /* floor: arithmetic shift */
    f = x & 0xFFFF;
    acc = c[3];
    acc = ((acc * f) >> 16) + c[2];
    acc = ((acc * f) >> 16) + c[1];
    acc = ((acc * f) >> 16) + c[0];
    acc = (1 << 30) + ((acc * f) >> 16); /* 2^f, Q2.30, 1.0..2.0 */
    out = ((uint64_t)inc * (uint64_t)acc) >> 30;
    if (k > 0) out = k > 8 ? FM_OSC_MAX_INC : out << k;
    else if (k < 0) out = k < -31 ? 0 : out >> -k;
    return out > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)out;
}

/* Sets oscillator `i`'s increment for `pitch` plus its tuning, and the LFO
 * offset on top. */
static void tune_osc(fm_osc_pair *p, uint8_t i, int32_t pitch,
                     uint32_t sample_rate)
{
    p->base[i] = fm_osc_pitch_inc(pitch + p->tune[i], sample_rate);
    if (p->sp_ar != SPIKE_A_Q24 * sample_rate) { /* the spike's rate terms: a divide, on events */
        p->sp_kq = sample_rate ? SPIKE_KQ / sample_rate : 0;
        p->sp_ar = SPIKE_A_Q24 * sample_rate;
    }
    fm_osc_set_inc(&p->osc[i], mod_inc(p->base[i], p->mod_oct[i]));
    update_pw(p, i);
    if (p->wave[i] == FM_WAVE_SPIKE) update_spike(p, i);
}

/* sync or cross-mod on: fm_osc_pair_next takes the slower path */
static FM_NOINLINE void update_special(fm_osc_pair *p)
{
    p->special = (uint8_t)(p->sync || p->xmod || (p->ring && p->level[1]));
    p->sync_pend = 0;
    update_only1(p);
    update_derived(p);
}

void fm_osc_pair_set_sync(fm_osc_pair *p, uint8_t on)
{
    p->sync = on ? 1 : 0;
    update_special(p);
}

void fm_osc_pair_set_ring(fm_osc_pair *p, uint8_t on)
{
    p->ring = on ? 1 : 0;
    update_special(p);
}

void fm_osc_pair_set_xmod(fm_osc_pair *p, int32_t depth)
{
    if (depth < 0) depth = 0;
    if (depth > FM_OSC_LEVEL_FULL) depth = FM_OSC_LEVEL_FULL;
    p->xmod = depth;
    update_special(p);
}

void fm_osc_pair_set_pitch_mod(fm_osc_pair *p, int32_t oct1, int32_t oct2)
{
    int32_t old1 = p->mod_oct[0], old2 = p->mod_oct[1];
    if (oct1 == old1 && oct2 == old2) return;
    p->mod_oct[0] = oct1;
    p->mod_oct[1] = oct2;
    fm_osc_set_inc(&p->osc[0], mod_inc(p->base[0], oct1));
    fm_osc_set_inc(&p->osc[1], mod_inc(p->base[1], oct2));
    if (p->wave[0] == FM_WAVE_SPIKE && oct1 != old1) update_spike(p, 0);
    if (p->wave[1] == FM_WAVE_SPIKE && oct2 != old2) update_spike(p, 1);
    derive(p);
}

void fm_osc_pair_set_tune(fm_osc_pair *p, uint8_t which, int32_t cents,
                          int32_t pitch, uint32_t sample_rate)
{
    if (which >= 2) return;
    if (cents < -12800) cents = -12800; /* keeps the sums from overflowing */
    if (cents > 12800) cents = 12800;
    p->tune[which] = cents * 65536 / 100; /* Q16.16 semitones */
    tune_osc(p, which, pitch, sample_rate);
    update_derived(p);
}

void fm_osc_pair_set_width(fm_osc_pair *p, uint8_t which, int32_t pct)
{
    if (which >= 2) return;
    if (pct < 1) pct = 1; /* 100% would not fit Q0.32 (it wraps to 0) */
    if (pct > 99) pct = 99;
    p->width[which] = (uint32_t)pct * 42949673u; /* 2^32 / 100, rounded */
    update_pw(p, which);
}

void fm_osc_pair_set_wave(fm_osc_pair *p, uint8_t which, uint8_t wave)
{
    if (which >= 2 || wave >= FM_WAVE_COUNT) return;
    p->wave[which] = wave;
    apply_level(p, which);
    if (wave == FM_WAVE_PULSE) update_pw(p, which); /* any LFO offset */
    if (wave == FM_WAVE_SPIKE) update_spike(p, which);
}

void fm_osc_pair_set_pitch(fm_osc_pair *p, int32_t pitch, uint32_t sample_rate)
{
    tune_osc(p, 0, pitch, sample_rate);
    tune_osc(p, 1, pitch, sample_rate);
    update_derived(p);
}

/*
 * White noise from a xorshift32 generator (period 2^32 - 1, three shifts
 * and three XORs a sample), as Q8.24 in -1.0..+1.0. Pitch has no effect.
 */
static int32_t noise_next(fm_osc_pair *p)
{
    uint32_t x = p->noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->noise = x;
    return (int32_t)x >> 7;
}

/* Oscillator `which`'s wave at phase t without band-limiting, Q8.24 (noise: 0). */
static int32_t naive_at(const fm_osc_pair *p, uint8_t which, uint32_t t)
{
    int32_t saw = (int32_t)(t ^ 0x80000000u) >> 7;
    uint32_t w = p->wave[which] == FM_WAVE_PULSE ? p->pw[which] : 0x80000000u;
    int32_t pulse = saw - ((int32_t)((t + w) ^ 0x80000000u) >> 7);
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse * p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse;
    case FM_WAVE_NOISE:
        return 0;
    case FM_WAVE_SPIKE: /* continuous: it is its own naive wave */
        return spike_at(p, which, t);
    default:
        return -saw;
    }
}

static int32_t wave_at(fm_osc_pair *p, uint8_t which);
static int32_t wave_at_dt(fm_osc_pair *p, uint8_t which, uint32_t dt0);

/*
 * osc2's sample, with the polyBLEP of a sync reset in the last sample.
 * After a reset osc2's phase is under one increment, so its own polyBLEP
 * already treats the reset as a full wrap (a step of -2.0); sync_after
 * replaces that with the real jump.
 */
static int32_t osc2_at(fm_osc_pair *p)
{
    int32_t v;
    if (!p->sync_pend)
        return wave_at(p, 1);
    /* the first sample after a restart: no polyBLEP of osc2's own for the
     * restart (dt 0 for the saw that starts there), the restart's instead */
    v = wave_at_dt(p, 1, 0);
    p->sync_pend = 0;
    return v + p->sync_after;
}

/*
 * Oscillator `which`'s sample at its current phase, Q8.24. `dt0` is the
 * increment the polyBLEP of the saw that starts at the phase uses (0: none);
 * a pulse's second saw always uses the oscillator's increment.
 */
static int32_t wave_at_dt(fm_osc_pair *p, uint8_t which, uint32_t dt0)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse_at(t, dt0, o->inc, p->pw[which]) *
                          p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse_at(t, dt0, o->inc, 0x80000000u);
    case FM_WAVE_NOISE:
        return noise_next(p);
    case FM_WAVE_SPIKE:
        return spike_at(p, which, t);
    default:
        return saw_fall(t, dt0);
    }
}

/* Oscillator `which`'s sample at its current phase, Q8.24. */
static int32_t wave_at(fm_osc_pair *p, uint8_t which)
{
    return wave_at_dt(p, which, p->osc[which].inc);
}

/*
 * How far the next sample is past an osc1 wrap that left osc1 at phase t:
 * t / osc1's increment in Q16 (0..1), from the reciprocal (no division).
 */
static uint64_t restart_x(const fm_osc_pair *p, uint32_t t)
{
    return ((uint64_t)t * p->recip0) >> p->recip0_sh;
}

/* A polyBLEP term of osc2's first saw (Q8.24 at unit gain, the rising saw's) at osc2's gain and
 * direction: the saw wave falls, so its own term is negated. */
static int32_t osc2_gain(const fm_osc_pair *p, int64_t v)
{
    /* noise has no polyBLEP, and the spike no step at its wrap */
    if (p->wave[1] == FM_WAVE_NOISE || p->wave[1] == FM_WAVE_SPIKE) return 0;
    if (p->wave[1] == FM_WAVE_PULSE) v = (v * p->pw_gain[1]) >> 30;
    return (int32_t)(p->wave[1] == FM_WAVE_SAW ? -v : v); /* the saw falls */
}

/*
 * The polyBLEP osc2's first saw put on the sample just computed at phase
 * `t` for a wrap due later in that sample, negated: a restart came first,
 * so that wrap never happens. The same sum saw_at made (one division, as
 * there), times the wave's gain and, for the saw, its falling sign
 * (osc2_gain).
 */
static int32_t unwrap_blep(const fm_osc_pair *p, uint32_t t, uint32_t dt)
{
    int64_t x = (int64_t)(((uint64_t)(0u - t) << 16) / dt);
    return osc2_gain(p, (((x * x) >> 16) - 2 * x + 65536) * 256); /* Q16 -> Q8.24 */
}

/*
 * The after side of the polyBLEP of osc2's own wrap, `t` (its phase now)
 * past it: what saw_at would have added on the next sample, had a restart
 * not followed the wrap within the sample (one division, as there).
 */
static int32_t wrap_after_blep(const fm_osc_pair *p, uint32_t t, uint32_t dt)
{
    int64_t x = (int64_t)(((uint64_t)t << 16) / dt);
    return osc2_gain(p, -(2 * x - ((x * x) >> 16) - 65536) * 256);
}

/*
 * osc1 wrapped with sync on: restart osc2 at the same point of its own
 * cycle (osc1 is `t`, its new phase, into the sample, so osc2 is
 * t * inc2 / inc1 into its cycle) and set up the polyBLEP of osc2's jump.
 * Returns the correction for the sample just computed. Once per osc1
 * cycle, so kept out of the per-sample path.
 */
static FM_NOINLINE int32_t sync_reset(fm_osc_pair *p, uint32_t t)
{
    /* osc2's increment this sample (bent, with cross-mod) for both ends */
    uint32_t inc2 = p->osc[1].inc, cont = p->osc[1].phase + inc2, now;
    int64_t x = (int64_t)restart_x(p, t);
    int32_t half, before;
    now = (uint32_t)(((uint64_t)x * inc2) >> 16);
    /* osc2 was at cont - now when osc1 wrapped, and jumps to 0 there */
    half = (naive_at(p, 1, 0) - naive_at(p, 1, cont - now)) / 2;
    /* the sample just computed, (1 - x) before the step: x^2 of the polyBLEP;
     * the next one, x after it: 2x - x^2 - 1 */
    before = (int32_t)((half * ((x * x) >> 16)) >> 16);
    p->sync_after = (int32_t)((half * (2 * x - ((x * x) >> 16) - 65536)) >> 16);
    p->sync_pend = 1;
    if (cont < p->osc[1].phase) { /* osc2's own wrap was due in this sample */
        if (cont < now) /* the restart came first: that wrap never happens */
            before += unwrap_blep(p, p->osc[1].phase, inc2);
        else /* it came first: its polyBLEP's after side, `cont` (where osc2
              * would be now without the restart) past it */
            p->sync_after += wrap_after_blep(p, cont, inc2);
    }
    p->osc[1].phase = now;
    p->osc[0].phase = t;
    return before;
}

/* Moves both oscillators on one sample; returns sync's correction for it (0 without). */
static int32_t advance(fm_osc_pair *p)
{
    uint32_t t = p->osc[0].phase + p->osc[0].inc;
    if (p->sync && t < p->osc[0].phase)
        return sync_reset(p, t);
    p->osc[1].phase += p->osc[1].inc;
    p->osc[0].phase = t;
    return 0;
}

/* x times a level (Q1.30); full passes x through untouched */
static inline int32_t gain(int32_t x, int32_t level)
{
    return level == FM_OSC_LEVEL_FULL ? x : (int32_t)(((int64_t)x * level) >> 30);
}

/*
 * The ring's a, b and c by osc1's wave (Q8.24), from PG-8X's fit (#105),
 * osc1 measured against the clean wave (PG-8X's peaks over its plateau
 * read as a deeper b at peak 1):
 *   saw 1.12, -0.85, 0.235; pulse (fit at PG-8X's 2-3 % width) 1.66, -1.0,
 *   0.15; square 0.84, -0.83, 0.21.
 * Noise isn't measured: plain ring modulation, 1.0, -1.0 and 0. The spike
 * (#150) keeps the pulse's a; its b and c are refit for its own shape (it
 * peaks near 2.0, the impulse response's crest) to PG-8X's products with
 * DCO-1 on PULSE (A3, DCO-2 a saw a fourth up, every line about -35 dB re
 * f2; f1 and 2f1 at -34): 1.66, -0.398, 0.177.
 * The fits ran free, which can't tell the saw's direction; the saw's apply
 * to the falling saw, as PG-8X's (#260), which matches its SYNC 2 spectra.
 */
static const int32_t ring_a[FM_WAVE_COUNT] = { 18790482, 27850179, 14092861, 16777216, 27850179 };
static const int32_t ring_b[FM_WAVE_COUNT] = { -14260634, -16777216, -13925089, -16777216, -6679130 };
static const int32_t ring_c[FM_WAVE_COUNT] = { 3942615, 2516582, 3523215, 0, 2973000 };

/*
 * osc2's sample x ringed by osc1's sample m: x (a + b m) + c m, Q8.24,
 * held to +-4.0. The law is asymmetric: with squares at full levels one of
 * its four levels reaches about -2.46 with osc1 added, and a hold at
 * +-2.0 cut that side alone, which left a DC offset the envelopes then
 * moved through the audio (CHURCHBELL's sub-audio, #266). Its widest
 * reach (a pulse osc1: osc2 times 2.66, plus 0.15 of osc1, plus osc1 at
 * full) is under 4.0, so the hold only guards the sums. It stays inside
 * the filter's hold (+-8.0, FM_FILTER_HOLD), which the open-filter render
 * passes straight through.
 */
/* The ring's a and b for osc1's wave, times osc2's wave level: osc2's
 * sample at its level against the saw, before the ring; the c term (osc1
 * bleeding through) is osc1's alone. Exactly the table's at 1.0. */
static void ring_coef(fm_osc_pair *p)
{
    uint8_t w = p->wave[0];
    int32_t g = wave_level[1][p->wave[1]];
    p->ring_ab[0] = g == 1 << 30 ? ring_a[w] : (int32_t)(((int64_t)ring_a[w] * g) >> 30);
    p->ring_ab[1] = g == 1 << 30 ? ring_b[w] : (int32_t)(((int64_t)ring_b[w] * g) >> 30);
}

#define RING_MAX (4 << 24)
static int32_t ring(const fm_osc_pair *p, int32_t x, int32_t m)
{
    uint8_t w = p->wave[0];
    int32_t g = p->ring_ab[0] + (int32_t)(((int64_t)p->ring_ab[1] * m) >> 24);
    int32_t v = (int32_t)(((int64_t)x * g + (int64_t)ring_c[w] * m) >> 24);
    return v > RING_MAX ? RING_MAX : v < -RING_MAX ? -RING_MAX : v;
}

/*
 * A synced pair's sample v with the DC taken off (#139): a synced osc2
 * restarts part way through its cycle, so the pair repeats every osc1
 * cycle with that cycle's mean in it. Each sample leaves less the offset,
 * held to +-4.0, and goes into the cycle's sum; where osc1 wraps, the sum
 * times osc1's increment (1 / the cycle's length in samples, Q0.32) is the
 * mean the offset left in, and the offset moves by it. Summing what
 * leaves, not what came in, takes off the DC the hold adds when the
 * offset shifts the wave into it (a ringed osc2 runs into +-4.0). The
 * offset needs no hold of its own: past the wave's range every sample
 * leaves at the hold and pulls it back, so it stays within about +-4.0
 * (+-8.0 at worst, where a cycle's mean overshoots after osc1 is pitched
 * up mid-cycle and cycle_mean holds it at +-4.0; v less it then fits
 * 32 bits with room).
 */
static int32_t cycle_mean(int64_t sum, uint32_t inc)
{
    /* sum * inc >> 32 in two halves: a cycle osc1 was held in (pitched
     * down to a stop, then back) can sum past what the product holds */
    int64_t m = (sum >> 32) * (int64_t)inc +
                (int64_t)(((uint64_t)sum & 0xFFFFFFFFu) * inc >> 32);
    return m > RING_MAX ? RING_MAX : m < -RING_MAX ? -RING_MAX : (int32_t)m;
}

/*
 * A cycle is a fractional number of samples (220 Hz at 48 kHz: 218.18),
 * so a restart's polyBLEP blip falls on one side of the wrap or the
 * other, and a cycle's mean jitters by up to one sample's swing over its
 * length even when nothing changes. The offset moved by all of each
 * mean would step with that jitter, un-band-limited, and the steps'
 * pattern is a comb at tens of Hz (#253: CLAV, a synced square, -24 dB
 * re f0 where PG-8X has -52). So the part of a mean within one full
 * step (1.0) per cycle, the most the jitter of one wave at full reaches,
 * moves the offset by 1/32 of it each cycle; the jitter averages out
 * under it. Anything past that moves it at once, as before: a real
 * change (a note's first cycle, a retune, a sweep, osc1 coming back from
 * a stop), or the share of a wider pair's jitter (osc1 mixed in, the
 * ring) past the step, which still leaves the comb lower than taking it
 * all. While a sweep runs, the offset trails it by up to that step (0.018
 * at A5, 0.06 an octave up). A vibrato on osc2 alone moves the DC by less
 * than the step each cycle, so the offset trails it too: a wobble at the
 * vibrato's rate, sub-audio, up to about 0.024 of full (-32 dBFS, A6,
 * 100 cents; taking the whole mean, about 0.0013). That is accepted for the
 * comb it takes off CLAV. Shifts and a hold at the wrap only: nothing per
 * sample, no state.
 */
#define SYNC_DC_SLOW 5 /* the jitter's share moves the offset by 1/2^5 a cycle */
static inline int32_t sync_dc(fm_osc_pair *p, int32_t v, int wrapped)
{
    int32_t y = v - p->dc;
    y = y > RING_MAX ? RING_MAX : y < -RING_MAX ? -RING_MAX : y;
    p->dc_sum += y;
    if (wrapped) {
        int32_t m = cycle_mean(p->dc_sum, p->osc[0].inc);
        int32_t jit = (int32_t)(p->osc[0].inc >> 8); /* 1.0 (Q8.24) / the cycle's length */
        int32_t small = m > jit ? jit : m < -jit ? -jit : m;
        /* rounded, not floored: a floor leaves a standing DC of about +16 LSB */
        p->dc += m - small + ((small + (1 << (SYNC_DC_SLOW - 1))) >> SYNC_DC_SLOW);
        p->dc_sum = 0;
    }
    return y;
}

/*
 * One sample with sync or cross-mod on. Cross-mod runs osc2 this sample
 * at its increment times (1 + depth * osc1), 0..2x and capped under
 * Nyquist, so its polyBLEPs follow the bent pitch; the set increment is
 * put back after. With osc2 at level 0 neither changes the sound, but
 * osc2 still moves with sync so its phase is right when its level changes.
 */
static FM_NOINLINE int32_t next_special(fm_osc_pair *p)
{
    uint32_t inc = p->osc[1].inc, t0 = p->osc[0].phase;
    int32_t a = 0, b;
    if (p->level[0] || p->xmod || p->ring)
        a = wave_at(p, 0);
    if (p->level[1] == 0) { /* osc2 unheard: with sync, only its phase restarts */
        uint32_t t = p->osc[0].phase + p->osc[0].inc;
        if (p->sync && t < p->osc[0].phase)
            p->osc[1].phase = (uint32_t)((restart_x(p, t) * p->osc[1].inc) >> 16);
        else
            p->osc[1].phase += p->osc[1].inc;
        p->osc[0].phase = t;
        p->sync_pend = 0;
        return gain(a, p->level[0]);
    }
    if (p->xmod) {
        /* a saw osc1 bends osc2 up over its cycle, as before the saw fell (#260) */
        int32_t m = p->wave[0] == FM_WAVE_SAW ? -a : a;
        int64_t now = (int64_t)inc + (((int64_t)p->xmod_inc * m) >> 24);
        p->osc[1].inc = now < 0 ? 0u : now > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)now;
    }
    b = osc2_at(p);
    b += advance(p);
    p->osc[1].inc = inc;
    if (p->ring) { /* osc1 plus the ringed osc2: the pair stays in +-4.0 */
        int32_t v = gain(a, p->level[0]) + gain(ring(p, b, a), p->req[1]);
        b = v > RING_MAX ? RING_MAX : v < -RING_MAX ? -RING_MAX : v;
    } else {
        b = gain(a, p->level[0]) + gain(b, p->level[1]);
    }
    return p->sync ? sync_dc(p, b, p->osc[0].phase < t0) : b;
}

/* Oscillator `which`'s sample, then its phase moves on (no sync). */
static int32_t wave_next(fm_osc_pair *p, uint8_t which)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase;
    o->phase = t + o->inc;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse_at(t, o->inc, o->inc, p->pw[which]) *
                          p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse_at(t, o->inc, o->inc, 0x80000000u);
    case FM_WAVE_NOISE:
        return noise_next(p);
    case FM_WAVE_SPIKE:
        return spike_at(p, which, t);
    default:
        return saw_fall(t, o->inc);
    }
}

/*
 * a * level 0 + b * level 1. An oscillator at level 0 only advances its
 * phase, so it costs one add. osc1 alone
 * without sync or cross-mod, the most common setting, is the first test,
 * so it costs no more than before those existed.
 */
int32_t fm_osc_pair_next(fm_osc_pair *p)
{
    int32_t a, b;
    if (p->only1) {
        p->osc[1].phase += p->osc[1].inc;
        return wave_next(p, 0);
    }
    if (p->special)
        return next_special(p);
    if (p->level[0] == 0) {
        p->osc[0].phase += p->osc[0].inc;
        return gain(wave_next(p, 1), p->level[1]);
    }
    if (p->level[1] == 0) {
        p->osc[1].phase += p->osc[1].inc;
        return gain(wave_next(p, 0), p->level[0]);
    }
    a = wave_next(p, 0);
    b = wave_next(p, 1);
    return gain(a, p->level[0]) + gain(b, p->level[1]);
}

/*
 * Oscillator `which`'s next `n` samples (no sync) times `level`, its wave
 * chosen once: into out[], or with `add` added to what out[] holds (osc1),
 * as fm_osc_pair_next sums them. Inlined into fm_osc_pair_render with
 * `add` known and `level` often known to be full, so each loop is
 * one wave's own, the pulse's two saws inline (pulse_at stays out of
 * line for the per-sample paths, where inlining it costs 8 voices).
 */
static inline void wave_run(fm_osc_pair *p, uint8_t which, int32_t *out,
                            uint32_t n, int32_t level, int add)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase, dt = o->inc, i;
    int32_t b;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE: {
        uint32_t w = p->pw[which];
        int32_t g = p->pw_gain[which];
        for (i = 0; i < n; i++, t += dt) {
            b = (int32_t)(((int64_t)(saw_at(t, dt) - saw_at(t + w, dt)) * g) >> 30);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    }
    case FM_WAVE_SQUARE:
        for (i = 0; i < n; i++, t += dt) {
            b = saw_at(t, dt) - saw_at(t + 0x80000000u, dt);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    case FM_WAVE_NOISE:
        for (i = 0; i < n; i++, t += dt) {
            b = noise_next(p);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    case FM_WAVE_SPIKE:
        for (i = 0; i < n; i++, t += dt) {
            b = spike_at(p, which, t);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    default:
        for (i = 0; i < n; i++, t += dt) {
            b = saw_fall(t, dt);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    }
    o->phase = t;
}

/*
 * Samples before osc1's next wrap at its current phase, at most `left`:
 * no restart of osc2 falls in them. One divide per osc1 cycle.
 */
static uint32_t until_wrap(const fm_osc_pair *p, uint32_t left)
{
    uint32_t inc0 = p->osc[0].inc, m;
    if (inc0 == 0) return left; /* osc1 stopped: it never wraps */
    m = ~p->osc[0].phase / inc0;
    return m < left ? m : left;
}

static void ring_wave_run(fm_osc_pair *p, int32_t *out, uint32_t n);
static inline int32_t ring_sum(const fm_osc_pair *p, int32_t a, int32_t b);

/*
 * sync_dc over a run of `n` samples in out[] with no wrap of osc1 in it:
 * each less the offset, held to +-4.0, into the cycle's sum.
 */
static void sync_dc_run(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    int64_t sum = 0;
    int32_t dc = p->dc;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = out[i] - dc;
        y = y > RING_MAX ? RING_MAX : y < -RING_MAX ? -RING_MAX : y;
        sum += y;
        out[i] = y;
    }
    p->dc_sum += sum;
}

/*
 * osc2's runs over a chunk whose osc1 part is already in out[] (osc1's
 * phase back at the chunk's start): free runs between osc1's wraps (the
 * whole chunk without sync), added in at osc2's level or, with `ring`,
 * ringed by osc1's raw samples there; the sample where osc1 wraps (sync_reset's
 * correction) and the one after it (the restart's polyBLEP) take
 * next_special's own steps. One loop for sync and the ring: the test of
 * `ring` is per run and per wrap, not per sample.
 */
static void pair_runs(fm_osc_pair *p, int32_t *out, uint32_t n, int ring)
{
    uint32_t inc0 = p->osc[0].inc, i = 0, m, t;
    int32_t level = p->level[1], b;
    while (i < n) {
        m = p->sync_pend ? 0 : p->sync ? until_wrap(p, n - i) : n - i;
        if (m) {
            if (ring)
                ring_wave_run(p, out + i, m);
            else
                wave_run(p, 1, out + i, m, level, 1);
            if (p->sync) sync_dc_run(p, out + i, m); /* synced: the DC off (#139) */
            p->osc[0].phase += m * inc0;
            i += m;
            continue;
        }
        /* a restart's next sample, or osc1 wraps in this one */
        t = p->osc[0].phase;
        b = osc2_at(p);
        b += advance(p);
        b = ring ? ring_sum(p, out[i], b) : out[i] + gain(b, level);
        out[i] = p->sync ? sync_dc(p, b, p->osc[0].phase < t) : b;
        i++;
    }
}

/*
 * osc1's next `n` samples times its level into out[], as next_special
 * makes them (0 at level 0, where it isn't drawn: a noise keeps its
 * generator). Leaves osc1's phase moved on by n.
 */
static void sync_osc1(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    if (p->level[0]) {
        wave_run(p, 0, out, n, p->level[0], 0);
    } else {
        for (i = 0; i < n; i++) out[i] = 0;
        p->osc[0].phase += n * p->osc[0].inc;
    }
}


/*
 * The next `n` samples with sync on and cross-mod off, as next_special
 * gives them. Sync never changes osc1, so it goes into out[] first, as in
 * a plain chunk. Between osc1's wraps osc2 runs free, so those runs are
 * wave_run's, added in; the sample where osc1 wraps (sync_reset's
 * correction) and the one after it (the restart's polyBLEP) take
 * next_special's own steps. At level 0 osc2 only moves its phase: by m
 * increments, or a restart.
 */
static void sync_run(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    uint32_t t0 = p->osc[0].phase, inc0 = p->osc[0].inc, i = 0, m;
    int32_t level = p->level[1];
    if (level == 0) {
        while (i < n) {
            m = until_wrap(p, n - i);
            p->osc[1].phase += m * p->osc[1].inc;
            p->osc[0].phase += m * inc0;
            i += m;
            if (i < n) { /* osc1 wraps in this sample: osc2 restarts */
                p->osc[0].phase += inc0;
                p->osc[1].phase = (uint32_t)((restart_x(p, p->osc[0].phase) *
                                              p->osc[1].inc) >> 16);
                i++;
            }
        }
        if (n) p->sync_pend = 0;
        p->osc[0].phase = t0;
        sync_osc1(p, out, n);
        return;
    }
    sync_osc1(p, out, n);
    p->osc[0].phase = t0;
    pair_runs(p, out, n, 0);
}

/*
 * osc1's raw sample a and osc2's b as next_special sums a ring pair: osc1
 * at its level plus the ringed osc2 at its own, held to +-4.0.
 */
static inline int32_t ring_sum(const fm_osc_pair *p, int32_t a, int32_t b)
{
    int32_t v = gain(a, p->level[0]) + gain(ring(p, b, a), p->req[1]);
    return v > RING_MAX ? RING_MAX : v < -RING_MAX ? -RING_MAX : v;
}

/*
 * osc2's next `n` free-running samples ringed by osc1's raw samples in
 * out[], which become the pair's. Square and saw (osc2 on 12 of the 13
 * factory tones with the ring) get loops of their own, wave_run's with
 * ring_sum for the sum; pulse and noise take wave_next a sample at a
 * time, which saves two more copies of the ring's loop (the FM-1 runs
 * from a 32 KB cache).
 */
static void ring_wave_run(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    fm_osc *o = &p->osc[1];
    uint32_t t = o->phase, dt = o->inc, i;
    switch (p->wave[1]) {
    case FM_WAVE_SQUARE:
        for (i = 0; i < n; i++, t += dt)
            out[i] = ring_sum(p, out[i], saw_at(t, dt) - saw_at(t + 0x80000000u, dt));
        break;
    case FM_WAVE_SAW:
        for (i = 0; i < n; i++, t += dt)
            out[i] = ring_sum(p, out[i], saw_fall(t, dt));
        break;
    default: /* pulse, noise and spike: a sample at a time */
        for (i = 0; i < n; i++)
            out[i] = ring_sum(p, out[i], wave_next(p, 1));
        return;
    }
    o->phase = t;
}

/*
 * The next `n` samples of a ring pair with osc2 heard and cross-mod off
 * (the JX-8P's X-MOD, and SYNC 2 with sync), as next_special gives them.
 * osc1 goes into out[] raw, at full level whatever its own: the ring reads
 * it there. Then osc2's free runs take ring_wave_run, the whole chunk
 * without sync, between osc1's wraps with it; the wrap's two samples take
 * next_special's own steps, as in sync_run.
 */
static void ring_run(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    uint32_t t0 = p->osc[0].phase;
    wave_run(p, 0, out, n, FM_OSC_LEVEL_FULL, 0);
    p->osc[0].phase = t0;
    pair_runs(p, out, n, 1);
}

/*
 * The path is chosen once for the chunk: osc1 alone, osc2 alone, both
 * summed (osc1 into out[], then osc2 added), sync in runs between osc1's
 * wraps, or cross-mod a sample at a time. An unheard oscillator only moves
 * its phase, by n increments at once. Two noises go a sample at a time, as
 * they share one generator and fm_osc_pair_next draws them in turn.
 */
void fm_osc_pair_render(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    int noises = p->wave[0] == FM_WAVE_NOISE && p->wave[1] == FM_WAVE_NOISE;
    if (p->special) {
        if (!p->xmod && !noises) {
            if (p->ring && p->level[1]) {
                ring_run(p, out, n);
                return;
            }
            /* the ring with osc2 unheard is silent, but next_special still
             * draws osc1 for it: a noise osc1 at level 0 goes per sample */
            if (!p->ring || p->level[0]) {
                sync_run(p, out, n);
                return;
            }
        }
        for (i = 0; i < n; i++) out[i] = next_special(p);
        return;
    }
    if (p->only1) {
        p->osc[1].phase += n * p->osc[1].inc;
        wave_run(p, 0, out, n, FM_OSC_LEVEL_FULL, 0);
        return;
    }
    if (p->level[0] == 0) {
        p->osc[0].phase += n * p->osc[0].inc;
        wave_run(p, 1, out, n, p->level[1], 0);
        return;
    }
    if (p->level[1] == 0) {
        p->osc[1].phase += n * p->osc[1].inc;
        wave_run(p, 0, out, n, p->level[0], 0);
        return;
    }
    if (noises) {
        for (i = 0; i < n; i++) out[i] = fm_osc_pair_next(p);
        return;
    }
    wave_run(p, 0, out, n, p->level[0], 0);
    wave_run(p, 1, out, n, p->level[1], 1);
}

/* 2^(m / 12) in Q2.30, m 0..11 (rounded) */
static const uint32_t semitone_q30[12] = {
    1073741824u, 1137589835u, 1205234447u, 1276901417u, 1352829926u, 1433273380u,
    1518500250u, 1608794974u, 1704458901u, 1805811301u, 1913190429u, 2026954652u
};

/* 2^(tune / 12) in Q16.16 for a tune in Q16.16 semitones (set_tune keeps
 * it within 128 semitones: whole octaves -11..10, so the shift is 4..25) */
static uint32_t tune_ratio_q16(int32_t tune)
{
    int32_t n = tune >> 16; /* floor: the fraction below is 0..1 up */
    int32_t oct = n >= 0 ? n / 12 : -((11 - n) / 12);
    uint64_t q = ((uint64_t)semitone_q30[n - 12 * oct] * semitone_ratio_q30((uint32_t)tune & 0xFFFFu)) >> 30;
    return (uint32_t)(q >> (14 - oct));
}

void fm_osc_pair_start_phase(fm_osc_pair *p, uint8_t which, uint32_t a, int32_t slope)
{
    if (which >= 2 || slope < 0) return;
    /* Q16.16 times Q16.16 is Q32: its low 32 bits are the fraction of a cycle */
    p->osc[which].phase = a + (uint32_t)((uint64_t)(uint32_t)slope * tune_ratio_q16(p->tune[which]));
}
