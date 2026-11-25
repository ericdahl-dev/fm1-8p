/*
 * A voice's first note starts its DCOs at PG-8X 2.0.13's phases (#154,
 * measured 2026-11-19, SELFNOISE 0): voice k (1..6) starts each DCO at
 * a + (b + w) r cycles of the saw's fundamental, r the DCO's pitch over the
 * note's (2^(tune / 12)), w the wave's term (square 0.733, PG-8X's pulse
 * 0.738, saw 0). b holds the note-on's place in PG-8X's render host block
 * as a fixed 224 samples (+0.0643). Later notes on the voice run on from
 * where it is. Voices 7 and 8 (PG-8X has 6), noise and unison start at 0.
 */
#include "../engine/fm1_8p.h"

#define STP_RATE 48000u

static fm_engine stp_eng;
static int16_t stp_buf[STP_RATE / 100 * 2];

/* #154's table: a (cycles) and b (cycles per unit r, the 224-sample offset in), DCO-1 then DCO-2 */
static const double stp_a[6][2] = { { .9431, .5642 }, { .9204, .6101 }, { .8906, .9133 },
                                   { .2738, .3171 }, { .7965, .3508 }, { .6933, .6661 } };
static const double stp_b[6][2] = { { .9559, .9545 }, { .9615, .9603 }, { .9611, .9601 },
                                   { .9625, .9615 }, { .9695, .9685 }, { .9685, .9677 } };

static void stp_run(uint32_t ms)
{
    while (ms) {
        uint32_t k = ms < 10 ? ms : 10;
        fm_engine_render(&stp_eng, stp_buf, STP_RATE / 1000u * k);
        ms -= k;
    }
}

static void stp_init(uint8_t voices)
{
    TT_CHECK_EQ(fm_engine_init(&stp_eng, STP_RATE, voices), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_AMP_RELEASE_MS, 1), FM_OK);
}

/* the voice playing `note`, -1 if none */
static int stp_voice(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < stp_eng.voice_count; i++)
        if (fm_voice_active(&stp_eng.voices[i]) && fm_voice_note(&stp_eng.voices[i]) == note) return i;
    return -1;
}

/* oscillator `d`'s phase on voice `v`, cycles */
static double stp_phase(int v, int d)
{
    return v < 0 ? -1.0 : stp_eng.voices[v].osc.osc[d].phase / 4294967296.0;
}

/* distance of `x` from `want` (cycles, mod 1) */
static double stp_off(double x, double want)
{
    double d = x - want;
    d -= (double)(long)d;
    if (d > 0.5) d -= 1.0;
    if (d < -0.5) d += 1.0;
    return d < 0 ? -d : d;
}

/* PG-8X voice k's start phase for DCO d at r, plus wave term w */
static double stp_want(int k, int d, double r, double w)
{
    return stp_a[k][d] + (stp_b[k][d] + w) * r;
}

/* 1. six new keys, one each 50 ms: voice k's DCOs start at PG-8X voice k's phases; DCO-1 16', DCO-2 a fifth
 * above 8' (r 0.5 and 2^(7/12)), both saws */
TT_TEST(each_voices_first_note_starts_at_pg8xs_phases)
{
    int k;
    double r2 = 1.4983070768766815;
    stp_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_OCTAVE, -1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_COARSE_SEMIS, 7), FM_OK);
    for (k = 0; k < 6; k++) {
        uint8_t note = (uint8_t)(48 + 2 * k);
        int v;
        fm_engine_note_on(&stp_eng, note, 100);
        v = stp_voice(note);
        TT_CHECK_EQ(v, k);
        if (stp_off(stp_phase(v, 0), stp_want(k, 0, 0.5, 0)) > 1e-4 || stp_off(stp_phase(v, 1), stp_want(k, 1, r2, 0)) > 1e-4)
            printf("    voice %d: %.4f %.4f, want %.4f %.4f\n", k + 1, stp_phase(v, 0), stp_phase(v, 1),
                   stp_want(k, 0, 0.5, 0) - (long)stp_want(k, 0, 0.5, 0), stp_want(k, 1, r2, 0) - (long)stp_want(k, 1, r2, 0));
        TT_CHECK(stp_off(stp_phase(v, 0), stp_want(k, 0, 0.5, 0)) < 1e-4);
        TT_CHECK(stp_off(stp_phase(v, 1), stp_want(k, 1, r2, 0)) < 1e-4);
        stp_run(50);
    }
}

/* 2. the wave's term: DCO-1 square 4' (r 2) and DCO-2 PG-8X's pulse (spike) 8' with FINE +50 cents; noise
 * stays at 0 */
TT_TEST(the_wave_adds_its_term_and_noise_starts_at_zero)
{
    double r2 = 1.0293022366434921; /* 2^(50 / 1200) */
    stp_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SQUARE), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_OCTAVE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_WAVE, FM_WAVE_SPIKE), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_FINE_CENTS, 50), FM_OK);
    fm_engine_note_on(&stp_eng, 60, 100);
    TT_CHECK(stp_off(stp_phase(0, 0), stp_want(0, 0, 2.0, 0.733)) < 1e-4);
    TT_CHECK(stp_off(stp_phase(0, 1), stp_want(0, 1, r2, 0.738)) < 1e-4);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_WAVE, FM_WAVE_NOISE), FM_OK);
    fm_engine_note_on(&stp_eng, 62, 100);
    TT_CHECK_EQ(stp_voice(62), 1);
    TT_CHECK(stp_off(stp_phase(1, 0), stp_want(1, 0, 2.0, 0.733)) < 1e-4);
    TT_CHECK_EQ(stp_eng.voices[1].osc.osc[1].phase, 0);
}

/* 3. voices 7 and 8 have no PG-8X voice: they start at 0 */
TT_TEST(voices_past_pg8xs_six_start_at_zero)
{
    int k;
    stp_init(8);
    for (k = 0; k < 8; k++) fm_engine_note_on(&stp_eng, (uint8_t)(48 + k), 100);
    TT_CHECK_EQ(stp_voice(54), 6);
    TT_CHECK_EQ(stp_voice(55), 7);
    TT_CHECK_EQ(stp_eng.voices[6].osc.osc[0].phase, 0);
    TT_CHECK_EQ(stp_eng.voices[6].osc.osc[1].phase, 0);
    TT_CHECK_EQ(stp_eng.voices[7].osc.osc[0].phase, 0);
    TT_CHECK_EQ(stp_eng.voices[7].osc.osc[1].phase, 0);
}

/* 4. unison keeps phase 0 (PG-8X's unison start is not measured: its voices out of phase would also change
 * the unison's level and the pitch it reads as) and marks its voices played: back in poly, they run on */
TT_TEST(unison_keeps_phase_zero_and_marks_its_voices_played)
{
    int k;
    stp_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON), FM_OK);
    fm_engine_note_on(&stp_eng, 60, 100);
    for (k = 0; k < 6; k++) {
        TT_CHECK_EQ(stp_eng.voices[k].osc.osc[0].phase, 0);
        TT_CHECK_EQ(stp_eng.voices[k].osc.osc[1].phase, 0);
        TT_CHECK(!fm_voice_fresh(&stp_eng.voices[k]));
    }
}

/* A score that reaches every note path: new keys, a steal, a take back during its fade, the same key again
 * (released and idle), a retrigger while it sounds, all sound off, unison and back. FNV-1a of the output. */
static uint32_t stp_score(const fm_start_table *first, const fm_start_table *after)
{
    static const uint8_t keys[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
    uint32_t h = 2166136261u;
    int k, n;
    if (fm_engine_init(&stp_eng, STP_RATE, 4) != FM_OK) return 0;
#ifndef STP_BASE
    if (first) stp_eng.start = first;
#else
    (void)first;
#endif
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_WAVE, FM_WAVE_SQUARE);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_COARSE_SEMIS, 7);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_AMP_RELEASE_MS, 20);
#define STP_STEP(ms) do { int s_; for (s_ = 0; s_ < (ms); s_++) { \
        fm_engine_render(&stp_eng, stp_buf, STP_RATE / 1000u); \
        for (n = 0; n < (int)(2 * STP_RATE / 1000u); n++) h = (h ^ (uint16_t)stp_buf[n]) * 16777619u; } } while (0)
    for (k = 0; k < 4; k++) {                   /* every voice's first note */
        fm_engine_note_on(&stp_eng, keys[k], 100);
        STP_STEP(30);
    }
#ifndef STP_BASE
    if (after) stp_eng.start = after;
#else
    (void)after;
#endif
    fm_engine_note_on(&stp_eng, keys[4], 100);   /* steals the oldest held */
    STP_STEP(1);
    fm_engine_note_on(&stp_eng, keys[0], 100);   /* takes it back in the fade */
    STP_STEP(40);
    for (k = 0; k < 5; k++) fm_engine_note_off(&stp_eng, keys[k]);
    STP_STEP(200);
    fm_engine_note_on(&stp_eng, keys[1], 90);    /* its idle voice again */
    STP_STEP(30);
    fm_engine_note_on(&stp_eng, keys[1], 120);   /* again while it sounds */
    STP_STEP(30);
    fm_engine_controller(&stp_eng, 120, 0);
    fm_engine_note_on(&stp_eng, keys[5], 100);
    STP_STEP(30);
    fm_engine_note_off(&stp_eng, keys[1]);
    fm_engine_note_off(&stp_eng, keys[5]);
    STP_STEP(100);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_note_on(&stp_eng, keys[6], 100);
    STP_STEP(30);
    fm_engine_note_on(&stp_eng, keys[7], 100);   /* a unison retrigger */
    STP_STEP(30);
    fm_engine_note_off(&stp_eng, keys[7]);
    fm_engine_note_off(&stp_eng, keys[6]);
    STP_STEP(100);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY);
    fm_engine_note_on(&stp_eng, keys[2], 100);
    STP_STEP(50);
#undef STP_STEP
    return h;
}

#define STP_BASE_HASH 0x5ed5f11du

/* 5. the phases are set once a voice's life: swapping the table after every voice's first note changes
 * nothing that follows (a steal, a take back, the same key again, a retrigger, all sound off, unison) */
TT_TEST(start_phases_are_set_once_a_voice_life)
{
    uint32_t kept = stp_score(0, 0), swapped = stp_score(0, &tt_start_zero);
    if (kept != swapped) printf("    table kept %08x, swapped after the first notes %08x\n", kept, swapped);
    TT_CHECK_EQ(kept, swapped);
}

/* 6. with every table entry 0 the mechanism is neutral: the score renders bit for bit as before #154
 * (its FNV-1a at 2de60a2, #309 with main 80b8d9b: the base) */
TT_TEST(an_all_zero_table_renders_as_before)
{
    uint32_t h = stp_score(&tt_start_zero, 0);
    if (h != STP_BASE_HASH) printf("    all-zero table %08x, base %08x\n", h, STP_BASE_HASH);
    TT_CHECK_EQ(h, STP_BASE_HASH);
}

/* 7. no click at a note's start on PG-8X's phases (Codex P3 on #310): a lone note, attack 0, on each voice 1..6
 * and each wave (both DCOs at full, DCO-2 a fifth up). Its amp onset eases in over 8 samples (env.c RAMP_SEGS x
 * RAMP_SAMPLES, #274): the first sample within 16 LSB of silence, and no step in those 8 over STP_CLICK_LSB
 * (after them the level is up and a square's or spike's own edge steps by hundreds, as from any phase) */
#define STP_CLICK_LSB 128
#define STP_EASE 8
TT_TEST(a_first_note_on_pg8xs_phases_eases_in_without_a_click)
{
    static const uint8_t waves[4] = { FM_WAVE_SAW, FM_WAVE_PULSE, FM_WAVE_SQUARE, FM_WAVE_SPIKE };
    int w, k, n, worst = 0, first = 0;
    for (w = 0; w < 4; w++) {
        stp_init(6);
        TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_AMP_ATTACK_MS, 0), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_WAVE, waves[w]), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_WAVE, waves[w]), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_LEVEL, 1000), FM_OK);
        TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_COARSE_SEMIS, 7), FM_OK);
        stp_run(50); /* past the start-up glides */
        for (k = 0; k < 6; k++) {
            int prev = 0;
            uint8_t note = (uint8_t)(48 + 2 * k);
            fm_engine_note_on(&stp_eng, note, 127);
            TT_CHECK_EQ(stp_voice(note), k);
            fm_engine_render(&stp_eng, stp_buf, STP_EASE);
            if (abs(stp_buf[0]) > first) first = abs(stp_buf[0]);
            for (n = 0; n < STP_EASE; n++) {
                int d = abs(stp_buf[2 * n] - prev);
                if (d > worst) worst = d;
                prev = stp_buf[2 * n];
            }
            fm_engine_note_off(&stp_eng, note);
            stp_run(50);
        }
    }
    printf("    a first note's onset, voices 1-6, 4 waves: first sample at most %d LSB, largest step %d\n", first, worst);
    TT_CHECK(first <= 16);
    TT_CHECK(worst <= STP_CLICK_LSB);
}

/* 8. a DCO tuned down by less than an octave: DCO-1 COARSE -5 (r 2^(-5/12)) and DCO-2 FINE -50 cents
 * (r 2^(-50/1200)) take the next octave down and its semitone (FW review on #310) */
TT_TEST(a_dco_tuned_down_within_an_octave_starts_at_pg8xs_phase)
{
    double r1 = 0.7491535384383408, r2 = 0.9715319411536059;
    stp_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_COARSE_SEMIS, -5), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_FINE_CENTS, -50), FM_OK);
    fm_engine_note_on(&stp_eng, 60, 100);
    if (stp_off(stp_phase(0, 0), stp_want(0, 0, r1, 0)) > 1e-4 || stp_off(stp_phase(0, 1), stp_want(0, 1, r2, 0)) > 1e-4)
        printf("    %.4f %.4f, want %.4f %.4f\n", stp_phase(0, 0), stp_phase(0, 1),
               stp_want(0, 0, r1, 0) - (long)stp_want(0, 0, r1, 0), stp_want(0, 1, r2, 0) - (long)stp_want(0, 1, r2, 0));
    TT_CHECK(stp_off(stp_phase(0, 0), stp_want(0, 0, r1, 0)) < 1e-4);
    TT_CHECK(stp_off(stp_phase(0, 1), stp_want(0, 1, r2, 0)) < 1e-4);
}

static void run_start_phase_tests(void)
{
    TT_RUN(each_voices_first_note_starts_at_pg8xs_phases);
    TT_RUN(the_wave_adds_its_term_and_noise_starts_at_zero);
    TT_RUN(voices_past_pg8xs_six_start_at_zero);
    TT_RUN(unison_keeps_phase_zero_and_marks_its_voices_played);
    TT_RUN(start_phases_are_set_once_a_voice_life);
    TT_RUN(an_all_zero_table_renders_as_before);
    TT_RUN(a_first_note_on_pg8xs_phases_eases_in_without_a_click);
    TT_RUN(a_dco_tuned_down_within_an_octave_starts_at_pg8xs_phase);
}
