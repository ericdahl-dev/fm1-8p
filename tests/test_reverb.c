/* Reverb tests (#197), through the reverb module's API and the engine's. Both designs run every test
 * the module has: the FDN4 and the plate, picked at init (the device builds one). */
#include <math.h>

#include "../engine/fm1_8p.h"
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

/* The output, pinned: an FNV-1a hash of both sides over a burst and its tail, for each design, at block
 * sizes 16 (the engine's chunks), 7 and 256. Taken from the first working loop (814156b) before it was
 * made block-wise (#197): a rewrite for speed must keep every sample. */
static uint32_t rv_hash(uint8_t design, uint32_t block)
{
    static int32_t l[256], r[256];
    uint32_t h = 2166136261u, done = 0, total = RV_RATE * 3u, i;
    fm_reverb_init(&rv, design, RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
    fm_reverb_set(&rv, 70, 2500, 60);
    rv_seed = 7;
    while (done < total) {
        uint32_t n = total - done < block ? total - done : block;
        for (i = 0; i < n; i++) l[i] = r[i] = done + i < RV_RATE / 4u ? rv_noise() / 2 : 0;
        fm_reverb_process_block(&rv, l, r, n);
        for (i = 0; i < n; i++) {
            uint32_t k;
            for (k = 0; k < 4u; k++) { h ^= ((uint32_t)l[i] >> (8u * k)) & 255u; h *= 16777619u; }
            for (k = 0; k < 4u; k++) { h ^= ((uint32_t)r[i] >> (8u * k)) & 255u; h *= 16777619u; }
        }
        done += n;
    }
    return h;
}

/* ... and a burst, 6 s of silence (the tail ends in it) and a second burst: the same with the idle
 * skip as without it, the sway's phases and all (blocks of 16) */
static uint32_t rv_hash_twice(uint8_t design)
{
    static int32_t l[16], r[16];
    uint32_t h = 2166136261u, done = 0, total = RV_RATE * 11u, i, k;
    fm_reverb_init(&rv, design, RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
    fm_reverb_set(&rv, 70, 1000, 60);
    rv_seed = 9;
    while (done < total) {
        for (i = 0; i < 16u; i++) {
            uint32_t t = done + i;
            l[i] = r[i] = (t < RV_RATE / 4u || (t >= RV_RATE * 7u && t < RV_RATE * 7u + RV_RATE / 4u)) ? rv_noise() / 2 : 0;
        }
        fm_reverb_process_block(&rv, l, r, 16);
        for (i = 0; i < 16u; i++)
            for (k = 0; k < 4u; k++) {
                h ^= ((uint32_t)l[i] >> (8u * k)) & 255u; h *= 16777619u;
                h ^= ((uint32_t)r[i] >> (8u * k)) & 255u; h *= 16777619u;
            }
        done += 16u;
    }
    return h;
}

TT_TEST(reverb_output_through_a_silence_is_pinned)
{
    TT_CHECK_EQ(rv_hash_twice(FM_REVERB_FDN4), 0x02F6B3AEu);   /* (taken before the idle skip existed) */
    TT_CHECK_EQ(rv_hash_twice(FM_REVERB_PLATE), 0x4E66BEE9u);
}

TT_TEST(reverb_output_is_pinned)
{
    static const uint32_t blocks[3] = {16, 7, 256};
    static const uint32_t want[2][3] = {{0x225CC3A9u, 0x79C4AFD6u, 0x650B108Eu}, {0x6A55B65Du, 0xF5242E73u, 0xF869CE70u}};
    uint32_t d, b;
    for (d = 0; d < 2u; d++)
        for (b = 0; b < 3u; b++)
            TT_CHECK_EQ(rv_hash(RV_DESIGNS[d], blocks[b]), want[d][b]);
}

/* A reverb whose tail has ended (every line zero) and whose input is silent skips its loop: no cost while
 * nothing sounds. Its output is the same as running it (the pinned hashes include long silences), and it
 * wakes on the next sound */
TT_TEST(reverb_idles_once_its_tail_has_ended)
{
    uint32_t d, s, i;
    for (d = 0; d < 2u; d++) {
        int idle_early, idle_late, woke;
        fm_reverb_init(&rv, RV_DESIGNS[d], RV_RATE, rv_mem, sizeof rv_mem / sizeof rv_mem[0]);
        fm_reverb_set(&rv, 100, 1000, 60);
        rv_burst(&rv, 1);                         /* a burst and its first second */
        for (i = 0; i < 256u; i++) rv_l[i] = rv_r[i] = 0;
        fm_reverb_process_block(&rv, rv_l, rv_r, 256);
        idle_early = fm_reverb_idle(&rv);
        for (s = 0; s < 4u; s++) {                 /* 4 s more: the 1 s tail has ended */
            for (i = 0; i < RV_RATE; i++) rv_l[i] = rv_r[i] = 0;
            fm_reverb_process_block(&rv, rv_l, rv_r, RV_RATE);
        }
        for (i = 0; i < 256u; i++) rv_l[i] = rv_r[i] = 0;
        fm_reverb_process_block(&rv, rv_l, rv_r, 256);
        idle_late = fm_reverb_idle(&rv);
        rv_l[0] = rv_r[0] = 1 << 22;
        for (i = 1; i < 256u; i++) rv_l[i] = rv_r[i] = 0;
        fm_reverb_process_block(&rv, rv_l, rv_r, 256);
        woke = !fm_reverb_idle(&rv);
        TT_CHECK(!idle_early);
        TT_CHECK(idle_late);
        TT_CHECK(woke);
    }
}

/* ---- in the engine ---- */

static fm_engine rv_eng;
static int16_t rv_out[RV_RATE * 2u];
static int16_t rv_emem[24000];

/* A C4 at velocity 100 for 0.5 s, then 1.5 s after its release: the summed |samples| of the last second */
static uint32_t rv_play(int mem, int32_t mix, int chorus)
{
    uint32_t i, s;
    uint64_t tail = 0;
    if (fm_engine_init(&rv_eng, RV_RATE, 4) != FM_OK) return 0;
    if (mem && fm_engine_set_reverb_memory(&rv_eng, rv_emem, sizeof rv_emem / sizeof rv_emem[0]) != FM_OK) return 0;
    fm_engine_set_param(&rv_eng, FM_PARAM_CHORUS_MODE, chorus);
    fm_engine_set_param(&rv_eng, FM_PARAM_AMP_RELEASE_MS, 50);
    fm_engine_set_param(&rv_eng, FM_PARAM_REVERB_MIX, mix);
    fm_engine_note_on(&rv_eng, 60, 100);
    fm_engine_render(&rv_eng, rv_out, RV_RATE / 2u);
    fm_engine_note_off(&rv_eng, 60);
    for (s = 0; s < 3u; s++) {
        fm_engine_render(&rv_eng, rv_out, RV_RATE / 2u);
        if (s)
            for (i = 0; i < RV_RATE; i++) tail += (uint32_t)(rv_out[i] < 0 ? -rv_out[i] : rv_out[i]);
    }
    return (uint32_t)(tail > 0xFFFFFFFFu ? 0xFFFFFFFFu : tail);
}

TT_TEST(reverb_params_are_ours_with_mix_off_by_default)
{
    const fm_param_info *m = fm_param_info_get(FM_PARAM_REVERB_MIX), *t = fm_param_info_get(FM_PARAM_REVERB_TIME_MS),
                        *o = fm_param_info_get(FM_PARAM_REVERB_TONE);
    TT_CHECK(m && t && o);
    TT_CHECK(m && m->min == 0 && m->max == 100 && m->def == 0);
    TT_CHECK(t && t->min == 300 && t->max == 10000 && t->def == 2500);
    TT_CHECK(o && o->min == 0 && o->max == 100 && o->def == 60);
    TT_CHECK(fm_engine_reverb_words(RV_RATE) > 0u);
    TT_CHECK_EQ(fm_engine_reverb_words(44100u), FM_REVERB_WORDS_44K);   /* the device's buffer, sized at build */
    TT_CHECK(fm_engine_reverb_words(RV_RATE) <= sizeof rv_emem / sizeof rv_emem[0]);
}

/* MIX 0, the default, renders byte for byte as with no reverb memory at all: the engine skips it */
TT_TEST(reverb_off_renders_as_without_it)
{
    static int16_t a[RV_RATE * 2u];
    uint32_t c;
    for (c = 0; c < 2u; c++) {
        rv_play(0, 0, (int)c);
        memcpy(a, rv_out, sizeof a);
        rv_play(1, 0, (int)c);
        TT_CHECK(memcmp(a, rv_out, sizeof a) == 0);
    }
}

/* With memory and MIX up the note rings on after its release (chorus on and off: off, the reverb makes
 * the mono mix stereo); with no memory MIX does nothing */
TT_TEST(reverb_on_rings_after_the_release_and_needs_its_memory)
{
    uint32_t c;
    for (c = 0; c < 2u; c++) {
        uint32_t dry = rv_play(1, 0, (int)c), wet = rv_play(1, 60, (int)c), none = rv_play(0, 60, (int)c);
        int differ = 0, i;
        printf("    chorus %u: the last second's |sum|: dry %u, MIX 60 %u, MIX 60 without memory %u\n", (unsigned)c,
               (unsigned)dry, (unsigned)wet, (unsigned)none);
        TT_CHECK(wet > dry * 4u + 1000u);
        TT_CHECK_EQ(none, dry);
        rv_play(1, 60, (int)c);
        for (i = 0; i < (int)RV_RATE; i++) differ |= rv_out[2 * i] != rv_out[2 * i + 1];
        TT_CHECK(differ);   /* stereo */
    }
}

/* All sound off (controller 120) empties the reverb too: no tail after it */
TT_TEST(reverb_all_sound_off_cuts_its_tail)
{
    uint32_t i, peak = 0;
    rv_play(1, 60, 0);
    fm_engine_controller(&rv_eng, 120, 0);
    fm_engine_render(&rv_eng, rv_out, RV_RATE / 2u);
    for (i = 0; i < RV_RATE; i++) peak |= (uint32_t)(rv_out[i] < 0 ? -rv_out[i] : rv_out[i]);
    TT_CHECK_EQ(peak, 0u);
}

static void run_reverb_tests(void)
{
    TT_RUN(reverb_tail_falls_60_db_in_its_time);
    TT_RUN(reverb_a_lower_tone_darkens_the_tail);
    TT_RUN(reverb_the_longest_tail_ends_in_silence);
    TT_RUN(reverb_the_sides_are_decorrelated);
    TT_RUN(reverb_full_scale_input_saturates_and_does_not_wrap);
    TT_RUN(reverb_without_memory_is_off_and_turning_on_starts_silent);
    TT_RUN(reverb_output_is_pinned);
    TT_RUN(reverb_output_through_a_silence_is_pinned);
    TT_RUN(reverb_idles_once_its_tail_has_ended);
    TT_RUN(reverb_params_are_ours_with_mix_off_by_default);
    TT_RUN(reverb_off_renders_as_without_it);
    TT_RUN(reverb_on_rings_after_the_release_and_needs_its_memory);
    TT_RUN(reverb_all_sound_off_cuts_its_tail);
}
