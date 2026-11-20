/*
 * A steal, as PG-8X 2.0.13 plays one (measured 2026-11-20, SELFNOISE 0): the
 * stolen voice starts its new note at once, from the levels its envelopes
 * are at, with no fade and no click. A released, a pedal-held and a held
 * voice alike. Through the engine's voices: which one plays a note, and
 * the levels of its envelopes.
 */
#include "../engine/fm1_8p.h"

#define ST_RATE 44100u

static fm_engine st_eng;
static int16_t st_buf[ST_RATE / 10 * 2];

/* render `ms` of audio in blocks of at most 100 ms */
static void st_run(uint32_t ms)
{
    while (ms) {
        uint32_t k = ms < 100 ? ms : 100;
        fm_engine_render(&st_eng, st_buf, ST_RATE / 1000u * k);
        ms -= k;
    }
}

/* four voices with slow envelopes: a 2 s attack to full, a 2 s release */
static void st_init(void)
{
    TT_CHECK_EQ(fm_engine_init(&st_eng, ST_RATE, 4), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_AMP_ATTACK_MS, 2000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_AMP_SUSTAIN, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_AMP_RELEASE_MS, 2000), FM_OK);
}

/* the voice sounding `note`, -1 if none */
static int st_voice(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < st_eng.voice_count; i++)
        if (fm_voice_active(&st_eng.voices[i]) && fm_voice_note(&st_eng.voices[i]) == note) return i;
    return -1;
}

/* the amp envelope's level of voice `v` */
static int32_t st_amp(int v)
{
    return v >= 0 ? fm_env_level(&st_eng.voices[v].amp_env) : -1;
}

/* 1. C4..D#4 held to full, C4 let go: E4 steals C4's releasing voice and starts at once from its level
 * (10 ms on, past what was the 3 ms fade) */
TT_TEST(a_steal_starts_from_the_released_voices_level)
{
    int v;
    int32_t was;
    st_init();
    fm_engine_note_on(&st_eng, 60, 100);
    fm_engine_note_on(&st_eng, 61, 100);
    fm_engine_note_on(&st_eng, 62, 100);
    fm_engine_note_on(&st_eng, 63, 100);
    st_run(2500);
    fm_engine_note_off(&st_eng, 60);
    st_run(50);
    v = st_voice(60);
    was = st_amp(v);
    TT_CHECK(v >= 0 && was > FM_ENV_FULL / 2);
    fm_engine_note_on(&st_eng, 64, 100);
    st_run(10);
    TT_CHECK_EQ(st_voice(64), v);
    if (st_amp(v) < was / 10 * 9) printf("    E4 10 ms after the steal at %ld, C4 was at %ld\n", (long)st_amp(v), (long)was);
    TT_CHECK(st_amp(v) >= was / 10 * 9);
}

/* the filter envelope's level of voice `v` */
static int32_t st_fenv(int v)
{
    return v >= 0 ? fm_env_level(&st_eng.voices[v].filter_env) : -1;
}

/* C4..D#4 held to full: the next key steals C4's voice, the oldest held */
static int st_full_chord(void)
{
    fm_engine_note_on(&st_eng, 60, 100);
    st_run(10);
    fm_engine_note_on(&st_eng, 61, 100);
    fm_engine_note_on(&st_eng, 62, 100);
    fm_engine_note_on(&st_eng, 63, 100);
    st_run(2500);
    return st_voice(60);
}

/* 2. every key held: E4 steals C4's held voice, the oldest, and starts at once from its level */
TT_TEST(a_steal_starts_from_the_held_voices_level)
{
    int v;
    int32_t was;
    st_init();
    v = st_full_chord();
    was = st_amp(v);
    TT_CHECK(v >= 0 && was > FM_ENV_FULL / 2);
    fm_engine_note_on(&st_eng, 64, 100);
    st_run(10);
    TT_CHECK_EQ(st_voice(64), v);
    if (st_amp(v) < was / 10 * 9) printf("    E4 10 ms after the steal at %ld, C4 was at %ld\n", (long)st_amp(v), (long)was);
    TT_CHECK(st_amp(v) >= was / 10 * 9);
}

/* 3. the filter envelope too: a 2 s filter attack to full, every key held; E4's filter envelope starts from
 * C4's level, not from 0 */
TT_TEST(a_steal_starts_from_the_voices_filter_envelope_level)
{
    int v;
    int32_t was;
    st_init();
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 2000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 4800), FM_OK);
    v = st_full_chord();
    was = st_fenv(v);
    TT_CHECK(v >= 0 && was > FM_ENV_FULL / 2);
    fm_engine_note_on(&st_eng, 64, 100);
    st_run(10);
    TT_CHECK_EQ(st_voice(64), v);
    if (st_fenv(v) < was / 10 * 9) printf("    E4's filter envelope 10 ms on at %ld, C4's was %ld\n", (long)st_fenv(v), (long)was);
    TT_CHECK(st_fenv(v) >= was / 10 * 9);
}

/* 4. a voice the pedal holds: C4 let go under the pedal, E4 steals it and starts at once from its level */
TT_TEST(a_steal_starts_from_the_pedal_held_voices_level)
{
    int v;
    int32_t was;
    st_init();
    v = st_full_chord();
    fm_engine_controller(&st_eng, 64, 127);
    fm_engine_note_off(&st_eng, 60);
    st_run(20);
    was = st_amp(v);
    TT_CHECK(v >= 0 && was > FM_ENV_FULL / 2);
    fm_engine_note_on(&st_eng, 64, 100);
    st_run(10);
    TT_CHECK_EQ(st_voice(64), v);
    if (st_amp(v) < was / 10 * 9) printf("    E4 10 ms after the steal at %ld, C4 was at %ld\n", (long)st_amp(v), (long)was);
    TT_CHECK(st_amp(v) >= was / 10 * 9);
}

/* the largest step between neighbor samples (left channel) in st_buf's frames [a, b) */
static int st_step(uint32_t a, uint32_t b)
{
    int m = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++) {
        int d = st_buf[2 * i] - st_buf[2 * (i - 1)];
        if (d < 0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

/* the largest sample (left channel, absolute) in st_buf's frames [a, b) */
static int st_peak(uint32_t a, uint32_t b)
{
    int m = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        int x = st_buf[2 * i] < 0 ? -st_buf[2 * i] : st_buf[2 * i];
        if (x > m) m = x;
    }
    return m;
}

/* 5. no click and no dip: one voice holding C4 at full, E4 steals it. The tone is filtered down to near its
 * fundamental, so the steady sound's sample steps are small and a jump would stand out: the largest step in
 * the 5 ms after the steal is within a quarter of the steady sound's, before (C4) or after (E4). And the
 * level doesn't dip: each 4 ms (about a period) of the first 20 ms peaks at 0.8 of C4's or more */
TT_TEST(a_steal_does_not_click)
{
    int before, at, after, low, peak;
    uint32_t w;
    TT_CHECK_EQ(fm_engine_init(&st_eng, ST_RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_AMP_SUSTAIN, 1000), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 7200), FM_OK);
    fm_engine_note_on(&st_eng, 60, 100);
    st_run(500);
    fm_engine_render(&st_eng, st_buf, ST_RATE / 20);
    before = st_step(0, ST_RATE / 20);
    peak = st_peak(0, ST_RATE / 20);
    fm_engine_note_on(&st_eng, 64, 100);
    fm_engine_render(&st_eng, st_buf, ST_RATE / 10);
    at = st_step(0, ST_RATE / 200);
    after = st_step(ST_RATE / 20, ST_RATE / 10);
    low = peak;
    for (w = 0; w < ST_RATE / 50; w += ST_RATE / 250) {
        int p = st_peak(w, w + ST_RATE / 250);
        if (p < low) low = p;
    }
    printf("    largest step: %d at the steal, %d before, %d after; lowest 4 ms peak %d of %d\n", at, before, after,
           low, peak);
    TT_CHECK(peak > 2000);
    TT_CHECK(at <= before * 5 / 4 || at <= after * 5 / 4);
    TT_CHECK(low >= peak / 10 * 8);
}

/* 6. the LFO starts again on a steal, as PG-8X 2.0.13's does (measured 2026-11-20: a stolen voice's vibrato,
 * at +-211 cents, drops to +-17 for the new note's first 0.3 s and builds back over its delay). One voice,
 * a 2 Hz LFO at 1200 cents with a 200 ms delay: C4 held 1 s, in full vibrato, more than a semitone off; E4
 * steals it, and 50 ms on
 * the LFO adds nothing to its pitch */
TT_TEST(a_steal_starts_the_lfo_delay_again)
{
    int32_t was;
    TT_CHECK_EQ(fm_engine_init(&st_eng, ST_RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_LFO_PITCH_CENTS, 1200), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&st_eng, FM_PARAM_LFO_DELAY_MS, 200), FM_OK);
    fm_engine_note_on(&st_eng, 60, 100);
    st_run(1000);
    was = fm_mod_get(&st_eng.voices[0].mod, FM_MOD_PITCH1);
    while (was > -(1 << 16) && was < (1 << 16)) { /* past a semitone either way */
        st_run(10);
        was = fm_mod_get(&st_eng.voices[0].mod, FM_MOD_PITCH1);
    }
    fm_engine_note_on(&st_eng, 64, 100);
    st_run(50);
    if (fm_mod_get(&st_eng.voices[0].mod, FM_MOD_PITCH1) != 0)
        printf("    50 ms after the steal the LFO adds %ld (was %ld)\n",
               (long)fm_mod_get(&st_eng.voices[0].mod, FM_MOD_PITCH1), (long)was);
    TT_CHECK_EQ(fm_mod_get(&st_eng.voices[0].mod, FM_MOD_PITCH1), 0);
}

static void run_steal_tests(void)
{
    TT_RUN(a_steal_starts_from_the_released_voices_level);
    TT_RUN(a_steal_starts_from_the_held_voices_level);
    TT_RUN(a_steal_starts_from_the_voices_filter_envelope_level);
    TT_RUN(a_steal_starts_from_the_pedal_held_voices_level);
    TT_RUN(a_steal_does_not_click);
    TT_RUN(a_steal_starts_the_lfo_delay_again);
}
