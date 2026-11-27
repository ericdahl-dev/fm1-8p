/* Reverb tests (#197), through the reverb module's API and the engine's. Both designs run every test
 * the module has: the FDN4 and the plate, picked at init (the device builds one). */
#include <math.h>

#include "../engine/reverb.h"

#define RV_RATE 44100u
#define RV_SECONDS 14u

static int16_t rv_mem[40000];
static int32_t rv_l[RV_RATE], rv_r[RV_RATE];
static const uint8_t RV_DESIGNS[2] = {FM_REVERB_FDN4, FM_REVERB_PLATE};

/* The wet's energy (L + R, 10 ms windows, dB) of a burst through `r`, `secs` long: rv_db[i] for window
 * i. The burst is 200 ms of white noise at -12 dBFS (Q8.24) into both sides, the way a played chord
 * fills the tank; a one-sample impulse spreads too thin for int16 lines to carry 60 dB of its tail. */
static double rv_db[RV_SECONDS * 100u];
static uint32_t rv_seed = 1;
static int32_t rv_noise(void)
{
    rv_seed ^= rv_seed << 13;
    rv_seed ^= rv_seed >> 17;
    rv_seed ^= rv_seed << 5;
    return (int32_t)(rv_seed >> 8) - (1 << 23);   /* +-0.5 */
}
static void rv_burst(fm_reverb *r, uint32_t secs)
{
    static int32_t dry[RV_RATE];
    uint32_t s, i, w = 0;
    double acc = 0;
    rv_seed = 1;
    for (s = 0; s < secs; s++) {
        for (i = 0; i < RV_RATE; i++)
            rv_l[i] = rv_r[i] = dry[i] = (s == 0 && i < RV_RATE / 5u) ? rv_noise() / 2 : 0;
        fm_reverb_process_block(r, rv_l, rv_r, RV_RATE);
        for (i = 0; i < RV_RATE; i++) {
            double l = rv_l[i] - dry[i], rr = rv_r[i] - dry[i];   /* the wet alone */
            acc += l * l + rr * rr;
            if ((i + 1u) % (RV_RATE / 100u) == 0) {
                rv_db[w++] = 10 * log10(acc + 1e-9);
                acc = 0;
            }
        }
    }
}

/* T60 from the tail's -5 to -35 dB (T30, doubled), against its loudest 10 ms after the burst, in seconds. */
static double rv_t60(uint32_t windows)
{
    uint32_t i, peak = 20, a = 0, b = 0;
    for (i = 20; i < windows; i++)
        if (rv_db[i] > rv_db[peak]) peak = i;
    for (i = peak; i < windows && !b; i++) {
        if (!a && rv_db[i] < rv_db[peak] - 5) a = i;
        if (rv_db[i] < rv_db[peak] - 35) b = i;
    }
    return b && a ? 2.0 * (b - a) / 100.0 : -1.0;
}

static fm_reverb rv;

/* The plate's own tank allpasses smear its energy, so it can't fall faster than about 0.7 s: from 2 s up
 * it keeps TIME (within 4 %), under that it reads long (1.12 s at 1 s, 0.68 s at 0.5 s) */
TT_TEST(reverb_tail_falls_60_db_in_its_time)
{
    static const int32_t times[2][2] = {{1000, 4000}, {2000, 4000}};
    uint32_t d, t;
    for (d = 0; d < 2u; d++)
        for (t = 0; t < 2u; t++) {
            int32_t time = times[d][t];
            double t60;
            TT_CHECK(fm_reverb_words(RV_DESIGNS[d], RV_RATE) <= sizeof rv_mem / sizeof rv_mem[0]);
            fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
            fm_reverb_set(&rv, 100, time, 100);   /* tone 100: no damping, so the decay is TIME's */
            rv_burst(&rv, (uint32_t)time / 1000u * 2u + 2u);
            t60 = rv_t60(((uint32_t)time / 1000u * 2u + 2u) * 100u);
            printf("    design %u, time %d ms: T60 %.2f s\n", (unsigned)RV_DESIGNS[d], (int)time, t60);
            TT_CHECK(fabs(t60 - time / 1000.0) <= time / 1000.0 * 0.10);
        }
}

/* The tail's high share: energy of the first difference over the energy (a rising-tilt weight), over
 * 0.3 .. 1.3 s after a burst's start: higher is brighter */
static double rv_bright(fm_reverb *r)
{
    static int32_t dry[RV_RATE];
    uint32_t i;
    double e = 0, dif = 0, pl = 0, pr = 0;
    rv_seed = 1;
    for (i = 0; i < RV_RATE; i++)
        rv_l[i] = rv_r[i] = dry[i] = i < RV_RATE / 5u ? rv_noise() / 2 : 0;
    fm_reverb_process_block(r, rv_l, rv_r, RV_RATE);
    for (i = RV_RATE * 3u / 10u; i < RV_RATE; i++) {   /* both sides */
        double l = rv_l[i] - dry[i], rr = rv_r[i] - dry[i];
        e += l * l + rr * rr;
        dif += (l - pl) * (l - pl) + (rr - pr) * (rr - pr);
        pl = l;
        pr = rr;
    }
    return dif / (e + 1e-9);
}

TT_TEST(reverb_a_lower_tone_darkens_the_tail)
{
    uint32_t d;
    for (d = 0; d < 2u; d++) {
        double b[3];
        static const int32_t tones[3] = {100, 60, 20};
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
            fm_reverb_set(&rv, 100, 3000, tones[k]);
            b[k] = rv_bright(&rv);
        }
        printf("    design %u: brightness at tone 100, 60, 20: %.4f %.4f %.4f\n", (unsigned)RV_DESIGNS[d], b[0], b[1], b[2]);
        TT_CHECK(b[1] < b[0] && b[2] < b[1] && b[2] < b[0] * 0.7);   /* (the plate, at half the rate, has less top to lose) */
    }
}

/* Every product in a loop truncates toward zero, so even the longest tail ends in exact silence: no
 * limit cycle holding a tone a few LSB up, no DC */
TT_TEST(reverb_the_longest_tail_ends_in_silence)
{
    uint32_t d, s, i;
    for (d = 0; d < 2u; d++) {
        int32_t peak = 0;
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
        fm_reverb_set(&rv, 100, 10000, 100);
        rv_burst(&rv, 1);
        for (s = 0; s < 60u; s++) {
            for (i = 0; i < RV_RATE; i++) rv_l[i] = rv_r[i] = 0;
            fm_reverb_process_block(&rv, rv_l, rv_r, RV_RATE);
        }
        for (i = 0; i < RV_RATE; i++) {
            int32_t a = rv_l[i] < 0 ? -rv_l[i] : rv_l[i], b = rv_r[i] < 0 ? -rv_r[i] : rv_r[i];
            peak = a > peak ? a : b > peak ? b : peak;
        }
        printf("    design %u: 61 s after a burst at TIME 10 s, the wet peaks at %d (Q8.24)\n", (unsigned)RV_DESIGNS[d], (int)peak);
        TT_CHECK_EQ(peak, 0);
    }
}

/* Stereo from one input: the tail's two sides are far from the same */
TT_TEST(reverb_the_sides_are_decorrelated)
{
    uint32_t d, i;
    for (d = 0; d < 2u; d++) {
        static int32_t dry[RV_RATE];
        double lr = 0, ll = 0, rr = 0, c;
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
        fm_reverb_set(&rv, 100, 2000, 100);
        rv_seed = 1;
        for (i = 0; i < RV_RATE; i++) rv_l[i] = rv_r[i] = dry[i] = i < RV_RATE / 5u ? rv_noise() / 2 : 0;
        fm_reverb_process_block(&rv, rv_l, rv_r, RV_RATE);
        for (i = RV_RATE / 4u; i < RV_RATE; i++) {
            double l = rv_l[i] - dry[i], r = rv_r[i] - dry[i];
            lr += l * r;
            ll += l * l;
            rr += r * r;
        }
        c = lr / sqrt(ll * rr + 1e-9);
        printf("    design %u: the tail's L/R correlation %.3f\n", (unsigned)RV_DESIGNS[d], c);
        TT_CHECK(fabs(c) < 0.3);
    }
}

/* A full-scale square into the longest TIME: the lines saturate, so the wet stays bounded by what the
 * saturated lines can sum to (the plate: 7 taps x 0.6, 8.4 x full scale at most; far inside Q8.24's
 * +-128) and never wraps (a wrap would jump by most of full scale between two samples) */
TT_TEST(reverb_full_scale_input_saturates_and_does_not_wrap)
{
    uint32_t d, s, i;
    for (d = 0; d < 2u; d++) {
        int32_t worst_jump = 0, worst = 0, prev = 0;
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
        fm_reverb_set(&rv, 100, 10000, 100);
        for (s = 0; s < 3u; s++) {
            for (i = 0; i < RV_RATE; i++) rv_l[i] = rv_r[i] = ((i / 100u) & 1u) ? (1 << 24) : -(1 << 24);
            fm_reverb_process_block(&rv, rv_l, rv_r, RV_RATE);
            for (i = 0; i < RV_RATE; i++) {
                int32_t w = rv_l[i] - (((i / 100u) & 1u) ? (1 << 24) : -(1 << 24)), j = w - prev;
                j = j < 0 ? -j : j;
                worst_jump = j > worst_jump ? j : worst_jump;
                worst = (w < 0 ? -w : w) > worst ? (w < 0 ? -w : w) : worst;
                prev = w;
            }
        }
        printf("    design %u: full-scale square, the wet's peak %.2f, its largest step %.2f (full scale 1.0)\n",
               (unsigned)RV_DESIGNS[d], worst / 16777216.0, worst_jump / 16777216.0);
        TT_CHECK(worst <= (17 << 24) / 2);
        TT_CHECK(worst_jump < 3 << 23);
    }
}

/* With no memory, or too little, it stays off whatever its settings; turned on again from off, it starts
 * from silent lines, not the last tail */
TT_TEST(reverb_without_memory_is_off_and_turning_on_starts_silent)
{
    uint32_t d, i;
    for (d = 0; d < 2u; d++) {
        int32_t peak = 0;
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, NULL, 0);
        fm_reverb_set(&rv, 100, 2000, 100);
        TT_CHECK(!fm_reverb_is_on(&rv));
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, fm_reverb_words(RV_DESIGNS[d], RV_RATE) - 1u);
        fm_reverb_set(&rv, 100, 2000, 100);
        TT_CHECK(!fm_reverb_is_on(&rv));
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
        fm_reverb_set(&rv, 100, 4000, 100);
        TT_CHECK(fm_reverb_is_on(&rv));
        rv_burst(&rv, 1);                       /* a tail in the lines ... */
        fm_reverb_set(&rv, 0, 4000, 100);       /* ... off ... */
        TT_CHECK(!fm_reverb_is_on(&rv));
        fm_reverb_set(&rv, 100, 4000, 100);     /* ... and on: silence in, silence out */
        for (i = 0; i < RV_RATE; i++) rv_l[i] = rv_r[i] = 0;
        fm_reverb_process_block(&rv, rv_l, rv_r, RV_RATE);
        for (i = 0; i < RV_RATE; i++) peak |= rv_l[i] | rv_r[i];
        TT_CHECK_EQ(peak, 0);
    }
}

static void run_reverb_tests(void)
{
    TT_RUN(reverb_tail_falls_60_db_in_its_time);
    TT_RUN(reverb_a_lower_tone_darkens_the_tail);
    TT_RUN(reverb_the_longest_tail_ends_in_silence);
    TT_RUN(reverb_the_sides_are_decorrelated);
    TT_RUN(reverb_full_scale_input_saturates_and_does_not_wrap);
    TT_RUN(reverb_without_memory_is_off_and_turning_on_starts_silent);
}
