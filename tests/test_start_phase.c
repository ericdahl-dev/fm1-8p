/*
 * Free-running DCOs (#89, #154), as PG-8X 2.0.13's run (measured 2026-11-25 at SELFNOISE 0, two 8' saws
 * locked, C4 rotating through its voices): every DCO keeps advancing at its own increment, idle or not, and a
 * note-on never resets it. At init voice k's DCO-1 starts at a per-voice phase (#154's DCO-1 intercepts for
 * voices 1..6, 0 past them) and DCO-2 0.616 cycle after it on every voice: PG-8X's DCO2 - DCO1 at lock reads
 * 0.615..0.617 on all 18 notes, and every note on every voice plays at one level (a spread of 0.01 dB).
 */
#include "../engine/fm1_8p.h"
#include <math.h>

#define STP_RATE 48000u
#define STP_BLOCK (STP_RATE / 100u) /* stp_buf's frames */

static fm_engine stp_eng;
static int16_t stp_buf[STP_BLOCK * 2];

/* each voice's DCO-1 phase at init (cycles): #154's fitted DCO-1 intercepts, voices 1..6; 7 and 8 at 0 */
static const double stp_init_a[8] = { .9431, .9204, .8906, .2738, .7965, .6933, 0, 0 };
#define STP_OFFSET 0.616 /* DCO2 - DCO1, cycles */

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
    return stp_eng.voices[v].osc.osc[d].phase / 4294967296.0;
}

/* DCO2 - DCO1 on voice `v`, cycles 0..1 */
static double stp_rel(int v)
{
    return (uint32_t)(stp_eng.voices[v].osc.osc[1].phase - stp_eng.voices[v].osc.osc[0].phase) / 4294967296.0;
}

/* distance of `x` from `want` (cycles, mod 1) */
static double stp_off(double x, double want)
{
    double d = x - want;
    d -= floor(d);
    return d > 0.5 ? 1.0 - d : d;
}

/* two 8' saws at full, locked: the same pitch, no detune, no chorus */
static void stp_lock(void)
{
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SAW), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_WAVE, FM_WAVE_SAW), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_OCTAVE, 0), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_OCTAVE, 0), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_COARSE_SEMIS, 0), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_FINE_CENTS, 0), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC1_LEVEL, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_LEVEL, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_CHORUS_MODE, 0), FM_OK);
}

/*
 * C4 again on the next voice: a repeated key gets its last voice back (#164), so a voice-mode switch to
 * unison and back first clears every voice's last note; C4 then takes the oldest idle voice, which rotates.
 */
static int stp_c4_next(void)
{
    if (fm_engine_set_param(&stp_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON) != FM_OK ||
        fm_engine_set_param(&stp_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY) != FM_OK)
        return -1;
    fm_engine_note_on(&stp_eng, 60, 127);
    return stp_voice(60);
}

/* 1. two saws locked: C4 rotating through all six voices plays at one level on each (PG-8X: 0.01 dB) */
TT_TEST(two_saws_locked_play_one_level_on_every_voice)
{
    double db[6], lo = 1e9, hi = -1e9;
    int k, n, seen = 0;
    stp_init(6);
    stp_lock();
    stp_run(50);
    for (k = 0; k < 6; k++) {
        double sum = 0;
        int v = stp_c4_next();
        TT_CHECK(v >= 0);
        if (v < 0) return;
        seen |= 1 << v;
        stp_run(100); /* past the attack */
        for (n = 0; n < 20; n++) { /* 200 ms held */
            uint32_t i;
            fm_engine_render(&stp_eng, stp_buf, STP_BLOCK);
            for (i = 0; i < STP_BLOCK; i++) sum += (double)stp_buf[2 * i] * stp_buf[2 * i];
        }
        db[v] = 10.0 * log10(sum / (20.0 * STP_BLOCK) + 1e-9);
        fm_engine_note_off(&stp_eng, 60);
        stp_run(500);
        TT_CHECK_EQ(fm_engine_active_voices(&stp_eng), 0);
    }
    TT_CHECK_EQ(seen, 0x3F);
    for (k = 0; k < 6; k++) {
        if (db[k] < lo) lo = db[k];
        if (db[k] > hi) hi = db[k];
    }
    printf("    locked saws, C4 on voices 1-6: %.3f %.3f %.3f %.3f %.3f %.3f dB, spread %.3f dB\n", db[0], db[1],
           db[2], db[3], db[4], db[5], hi - lo);
    TT_CHECK(hi - lo < 0.1);
}

/* 2. DCO2 - DCO1 is 0.616 cycle on every voice, at init and at every note-on after it */
TT_TEST(dco2_runs_0_616_cycle_after_dco1_on_every_voice)
{
    int k;
    double worst = 0;
    stp_init(8);
    for (k = 0; k < 8; k++) {
        TT_CHECK(stp_off(stp_phase(k, 0), stp_init_a[k]) < 1e-4);
        TT_CHECK(stp_off(stp_rel(k), STP_OFFSET) < 0.002);
    }
    stp_lock();
    stp_run(50);
    for (k = 0; k < 16; k++) {
        int v = stp_c4_next();
        TT_CHECK(v >= 0);
        if (v < 0) return;
        if (stp_off(stp_rel(v), STP_OFFSET) > worst) worst = stp_off(stp_rel(v), STP_OFFSET);
        TT_CHECK(stp_off(stp_rel(v), STP_OFFSET) < 0.002);
        stp_run(100);
        fm_engine_note_off(&stp_eng, 60);
        stp_run(137);
    }
    printf("    DCO2 - DCO1 at 16 note-ons over 8 voices: within %.6f cycle of %.3f\n", worst, STP_OFFSET);
}

/* 3. a voice runs on while idle: its phase moves by its increment x the frames, and a note-on never resets
 * it; a voice that never played keeps its phase from init (its increment is 0) */
TT_TEST(an_idle_voice_runs_on_and_a_note_on_keeps_its_phase)
{
    static const uint32_t blocks[5] = { 1, 37, 256, STP_BLOCK, 129 };
    uint32_t p0, p1, i0, i1, left = 23456, b = 0, never0, never1;
    stp_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_LEVEL, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_FINE_CENTS, 13), FM_OK);
    never0 = stp_eng.voices[5].osc.osc[0].phase;
    never1 = stp_eng.voices[5].osc.osc[1].phase;
    fm_engine_note_on(&stp_eng, 57, 100);
    TT_CHECK_EQ(stp_voice(57), 0);
    stp_run(100);
    fm_engine_note_off(&stp_eng, 57);
    stp_run(100);
    TT_CHECK(!fm_voice_active(&stp_eng.voices[0]));
    p0 = stp_eng.voices[0].osc.osc[0].phase;
    p1 = stp_eng.voices[0].osc.osc[1].phase;
    i0 = stp_eng.voices[0].osc.osc[0].inc;
    i1 = stp_eng.voices[0].osc.osc[1].inc;
    TT_CHECK(i0 != 0 && i1 != i0);
    while (left) { /* idle, at odd block sizes */
        uint32_t n = blocks[b++ % 5];
        n = n < left ? n : left;
        fm_engine_render(&stp_eng, stp_buf, n);
        left -= n;
    }
    TT_CHECK_EQ(stp_eng.voices[0].osc.osc[0].phase, p0 + i0 * 23456u);
    TT_CHECK_EQ(stp_eng.voices[0].osc.osc[1].phase, p1 + i1 * 23456u);
    fm_engine_note_on(&stp_eng, 57, 100); /* its voice again: on from where it is */
    TT_CHECK_EQ(stp_voice(57), 0);
    TT_CHECK_EQ(stp_eng.voices[0].osc.osc[0].phase, p0 + i0 * 23456u);
    TT_CHECK_EQ(stp_eng.voices[0].osc.osc[1].phase, p1 + i1 * 23456u);
    TT_CHECK_EQ(stp_eng.voices[5].osc.osc[0].phase, never0);
    TT_CHECK_EQ(stp_eng.voices[5].osc.osc[1].phase, never1);
}

/* `frames` of output in blocks of `block` (1..STP_BLOCK) into FNV-1a hash `h` */
static uint32_t stp_hash_run(uint32_t frames, uint32_t block, uint32_t h)
{
    while (frames) {
        uint32_t n = frames < block ? frames : block, i;
        fm_engine_render(&stp_eng, stp_buf, n);
        for (i = 0; i < 2 * n; i++) h = (h ^ (uint16_t)stp_buf[i]) * 16777619u;
        frames -= n;
    }
    return h;
}

/* Notes with idle gaps between, sync on for the second half (osc2 restarting while idle), in blocks of `block` */
static uint32_t stp_gaps(uint32_t block)
{
    uint32_t h = 2166136261u;
    int k;
    if (fm_engine_init(&stp_eng, STP_RATE, 4) != FM_OK) return 0;
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_LEVEL, 1000);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_COARSE_SEMIS, 7);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC2_FINE_CENTS, 9);
    (void)fm_engine_set_param(&stp_eng, FM_PARAM_AMP_RELEASE_MS, 20);
    for (k = 0; k < 12; k++) {
        uint8_t note = (uint8_t)(48 + 5 * (k % 3));
        if (k == 6) (void)fm_engine_set_param(&stp_eng, FM_PARAM_OSC_SYNC, 1);
        fm_engine_note_on(&stp_eng, note, 100);
        h = stp_hash_run(4801, block, h);
        fm_engine_note_off(&stp_eng, note);
        h = stp_hash_run(4801u + 113u * (uint32_t)k, block, h);
    }
    return h;
}

/* 4. the idle runs are the same at any block size, with sync too */
TT_TEST(idle_runs_do_not_depend_on_block_size)
{
    uint32_t a = stp_gaps(STP_BLOCK), b = stp_gaps(37), c = stp_gaps(1);
    if (a != b || a != c) printf("    blocks %u %08x, 37 %08x, 1 %08x\n", STP_BLOCK, a, b, c);
    TT_CHECK_EQ(a, b);
    TT_CHECK_EQ(a, c);
}

/* 5. no click at a note's start from wherever its DCOs run (Codex P3 on #310): a lone note, attack 0, on each
 * voice 1..6 twice (from init, then from where it ran to) and each wave (both DCOs at full, DCO-2 a fifth up).
 * Its amp onset eases in over 8 samples (env.c RAMP_SEGS x RAMP_SAMPLES, #274): the first sample within 16 LSB
 * of silence, and no step in those 8 over STP_CLICK_LSB (after them the level is up and a square's or spike's
 * own edge steps by hundreds, as from any phase) */
#define STP_CLICK_LSB 128
#define STP_EASE 8
TT_TEST(a_note_from_a_free_running_phase_eases_in_without_a_click)
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
        for (k = 0; k < 12; k++) {
            int prev = 0;
            uint8_t note = (uint8_t)(48 + 2 * k);
            fm_engine_note_on(&stp_eng, note, 127);
            TT_CHECK_EQ(stp_voice(note), k % 6);
            fm_engine_render(&stp_eng, stp_buf, STP_EASE);
            if (abs(stp_buf[0]) > first) first = abs(stp_buf[0]);
            for (n = 0; n < STP_EASE; n++) {
                int d = abs(stp_buf[2 * n] - prev);
                if (d > worst) worst = d;
                prev = stp_buf[2 * n];
            }
            fm_engine_note_off(&stp_eng, note);
            stp_run((uint32_t)(50 + 7 * k));
        }
    }
    printf("    a note's onset, voices 1-6 twice, 4 waves: first sample at most %d LSB, largest step %d\n", first,
           worst);
    TT_CHECK(first <= 16);
    TT_CHECK(worst <= STP_CLICK_LSB);
}

static void run_start_phase_tests(void)
{
    TT_RUN(two_saws_locked_play_one_level_on_every_voice);
    TT_RUN(dco2_runs_0_616_cycle_after_dco1_on_every_voice);
    TT_RUN(an_idle_voice_runs_on_and_a_note_on_keeps_its_phase);
    TT_RUN(idle_runs_do_not_depend_on_block_size);
    TT_RUN(a_note_from_a_free_running_phase_eases_in_without_a_click);
}
