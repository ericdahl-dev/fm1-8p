/*
 * Portamento (#62), as PG-8X 2.0.13's (measured 2026-11-18, three passes): a glide is exponential in pitch,
 * the distance left falling by 1/e every tau, the same tau for any interval, up or down; each voice glides
 * from the last note played (also one let go). portamento_ms is that tau; 0 is off. Through the public
 * interface: the voices' sounding pitch (Q16.16 semitones) as the engine sets it.
 */
#include "../engine/fm1_8p.h"

#define PT_RATE 48000u
#define PT_E 2.718281828459045

static fm_engine pt_eng;
static int16_t pt_buf[PT_RATE / 10 * 2];

/* render `ms` of audio (in 1 ms blocks) */
static void pt_run(double ms)
{
    uint32_t n = (uint32_t)(ms * PT_RATE / 1000.0), k;
    while (n) {
        k = n < PT_RATE / 1000u ? n : PT_RATE / 1000u;
        fm_engine_render(&pt_eng, pt_buf, k);
        n -= k;
    }
}

/* the sounding pitch of the voice playing `note`, semitones (-1000 if none) */
static double pt_pitch(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < pt_eng.voice_count; i++)
        if (fm_voice_active(&pt_eng.voices[i]) && fm_voice_note(&pt_eng.voices[i]) == note)
            return pt_eng.voices[i].pitch / 65536.0;
    return -1000.0;
}

/* tau 50 ms, C3 then C4: the octave left falls as exp(-t / 50 ms), within a control tick's worth */
TT_TEST(porta_glides_exponentially_with_its_tau)
{
    double t, left, want;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 50), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(100);
    fm_engine_note_off(&pt_eng, 48);
    fm_engine_note_on(&pt_eng, 60, 100);
    for (t = 10; t <= 200; t += 10) {
        pt_run(10);
        left = 60.0 - pt_pitch(60);
        want = 12.0 * exp(-t / 50.0);
        if (fabs(left - want) > 0.02 + want * 0.02)
            printf("    at %.0f ms: %.3f st left, want %.3f\n", t, left, want);
        TT_CHECK(fabs(left - want) <= 0.02 + want * 0.02);
    }
}

/* a glide down ends on its note exactly, and stops (no voice left moving), at a short tau and a long one */
static void pt_down_lands(int32_t tau_ms)
{
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, tau_ms), FM_OK);
    fm_engine_note_on(&pt_eng, 72, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, 72);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(tau_ms * 14.0);                      /* 12 octave-tau's to under 1/1000 st, and some */
    if (pt_pitch(60) != 60.0)
        printf("    tau %d ms: at %.6f st after 14 tau\n", (int)tau_ms, pt_pitch(60));
    TT_CHECK(pt_pitch(60) == 60.0);
    TT_CHECK_EQ(pt_eng.porta_moving, 0);
}
TT_TEST(porta_down_lands_on_its_note)
{
    pt_down_lands(50);
    pt_down_lands(2000);
}

/* the semitones still to go `ms` after `to` follows `from` at tau `tau_ms` (from let go first) */
static double pt_left(uint8_t from, uint8_t to, int32_t tau_ms, double ms, uint32_t block)
{
    uint32_t n, k;
    if (fm_engine_init(&pt_eng, PT_RATE, 6) != FM_OK ||
        fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, tau_ms) != FM_OK)
        return 1000.0;
    fm_engine_note_on(&pt_eng, from, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, from);
    fm_engine_note_on(&pt_eng, to, 100);
    n = (uint32_t)(ms * PT_RATE / 1000.0);
    while (n) {
        k = n < block ? n : block;
        fm_engine_render(&pt_eng, pt_buf, k);
        n -= k;
    }
    return (double)to - pt_pitch(to);
}

/* PG-8X: one tau for any interval, up or down: after 1 tau each has 1/e of its interval left (2%) */
TT_TEST(porta_one_tau_for_any_interval)
{
    static const uint8_t from[4] = { 53, 48, 36, 72 };
    int k;
    for (k = 0; k < 4; k++) {
        double iv = 60.0 - from[k], left = pt_left(from[k], 60, 80, 80, 48);
        if (fabs(left - iv / PT_E) > fabs(iv) * 0.02)
            printf("    from %u: %.3f st left after 1 tau, want %.3f\n", from[k], left, iv / PT_E);
        TT_CHECK(fabs(left - iv / PT_E) <= fabs(iv) * 0.02);
    }
}

/* a long tau is as exact: 2 s, after 1 tau (2%) */
TT_TEST(porta_long_tau_is_exact)
{
    double left = pt_left(48, 60, 2000, 2000, 48);
    if (fabs(left - 12.0 / PT_E) > 12.0 * 0.02) printf("    %.3f st left, want %.3f\n", left, 12.0 / PT_E);
    TT_CHECK(fabs(left - 12.0 / PT_E) <= 12.0 * 0.02);
}

/* the glide does not depend on the caller's block size */
TT_TEST(porta_does_not_depend_on_block_size)
{
    double a = pt_left(48, 60, 50, 37, 48), b = pt_left(48, 60, 50, 37, 1), c = pt_left(48, 60, 50, 37, 256);
    TT_CHECK(a == b);
    TT_CHECK(a == c);
}

/* 0 is off: the note is at its pitch at once */
TT_TEST(porta_zero_is_off)
{
    TT_CHECK(pt_left(48, 60, 0, 1, 48) == 0.0);
}

/* the first note after init has nothing to glide from */
TT_TEST(porta_first_note_does_not_glide)
{
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 200), FM_OK);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(1);
    TT_CHECK(pt_pitch(60) == 60.0);
}

/* setting portamento to 0 mid-glide puts the note on its pitch */
TT_TEST(porta_set_to_zero_mid_glide_lands)
{
    double left = pt_left(48, 60, 500, 50, 48);
    TT_CHECK(left > 5.0);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 0), FM_OK);
    pt_run(1);
    TT_CHECK(pt_pitch(60) == 60.0);
    TT_CHECK_EQ(pt_eng.porta_moving, 0);
}

/* each note glides from the last one played, even in the same instant (#62: Eric's "last note
 * played"): C3, let go, then E4 and G4 together: E4 glides from C3, G4 from E4 */
TT_TEST(porta_each_note_glides_from_the_last_played)
{
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, 48);
    fm_engine_note_on(&pt_eng, 64, 100);
    fm_engine_note_on(&pt_eng, 67, 100);
    TT_CHECK(fabs(pt_pitch(64) - 48.0) < 0.01);
    TT_CHECK(fabs(pt_pitch(67) - 64.0) < 0.01);
}

/* a voice that has gone quiet stops gliding: a 10 s glide, its note let go with a short release; once the
 * voice is idle, nothing glides (the engine stops splitting its chunks at every control tick for it) */
TT_TEST(porta_stops_on_a_voice_gone_quiet)
{
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_AMP_RELEASE_MS, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 10000), FM_OK);
    fm_engine_note_on(&pt_eng, 36, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, 36);
    fm_engine_note_on(&pt_eng, 96, 100);
    pt_run(50);
    fm_engine_note_off(&pt_eng, 96);
    pt_run(200);
    TT_CHECK_EQ(fm_engine_active_voices(&pt_eng), 0);
    TT_CHECK_EQ(pt_eng.porta_moving, 0);
}

/* unison: a note played mid-glide glides on from where the voices are, with no jump (they are the last
 * note played, sounding) */
TT_TEST(porta_unison_mid_glide_goes_on_from_where_it_is)
{
    int32_t before;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(20);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(200);
    before = pt_eng.voices[0].pitch;
    fm_engine_note_on(&pt_eng, 67, 100);        /* (C4 still held) */
    if (pt_eng.voices[0].pitch != before)
        printf("    G4 on: %.3f st -> %.3f st\n", before / 65536.0, pt_eng.voices[0].pitch / 65536.0);
    TT_CHECK_EQ(pt_eng.voices[0].pitch, before);
    pt_run(200);
    before = pt_eng.voices[0].pitch;
    fm_engine_note_off(&pt_eng, 67);            /* the legato return to C4, too */
    TT_CHECK_EQ(pt_eng.voices[0].pitch, before);
}

/* a stealing note keeps its glide when the stolen key comes back: 2 voices, C3 then C4 held; C5 steals
 * C3's voice (gliding from C4); C3 struck again 2 ms on steals C4's voice, the oldest held, and C5 glides on
 * where it is (Codex P3-1 on #301) */
TT_TEST(porta_stealing_note_keeps_its_glide_when_the_stolen_key_comes_back)
{
    double p;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 2), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(10);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(10);
    fm_engine_note_on(&pt_eng, 72, 100);
    pt_run(2);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(30);
    p = pt_pitch(72);
    if (!(p > 60.0 && p < 61.0)) printf("    C5 at %.3f st 30 ms on, want just above 60\n", p);
    TT_CHECK(p > 60.0 && p < 61.0);
}

/* by design (#62 "from the last note played"): a note struck again mid-glide glides from itself, so it
 * lands at once. C3, then C4 with tau 1 s; 300 ms on, C4 let go and struck again: it is on C4 */
TT_TEST(porta_same_note_again_mid_glide_lands)
{
    double p;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(20);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(300);
    TT_CHECK(pt_pitch(60) < 58.0);
    fm_engine_note_off(&pt_eng, 60);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(1);
    p = pt_pitch(60);
    if (p != 60.0) printf("    C4 again at %.3f st\n", p);
    TT_CHECK(p == 60.0);
}

/* a note that steals a voice glides from the last note played: 2 voices, C3 and C4 held; C5 steals C3's
 * voice and 10 ms on is just above C4 (it glides from C4) */
TT_TEST(porta_stealing_note_glides_from_the_last_note)
{
    double p;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 2), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(10);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(10);
    fm_engine_note_on(&pt_eng, 72, 100);
    pt_run(10);
    p = pt_pitch(72);
    if (!(p > 60.0 && p < 61.0)) printf("    C5 at %.3f st 10 ms on, want just above 60\n", p);
    TT_CHECK(p > 60.0 && p < 61.0);
}

/* the sequencer's notes and the keys share one last note played (#62): a key's C3, then a sequencer
 * step's C4 glides from it, and a key's E4 after that from C4 */
TT_TEST(porta_sequencer_and_keys_share_the_last_note)
{
    seq_event ev;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, 48);
    memset(&ev, 0, sizeof ev);
    ev.kind = SEQ_EV_NOTE;
    ev.note = 60;
    ev.vel = 100;
    seq_apply_event(&pt_eng, &ev, 0);
    TT_CHECK(fabs(pt_pitch(60) - 48.0) < 0.01);
    pt_run(20);
    fm_engine_note_on(&pt_eng, 64, 100);
    TT_CHECK(fabs(pt_pitch(64) - 60.0) < 0.01);
}

static void run_porta_tests(void)
{
    TT_RUN(porta_glides_exponentially_with_its_tau);
    TT_RUN(porta_down_lands_on_its_note);
    TT_RUN(porta_one_tau_for_any_interval);
    TT_RUN(porta_long_tau_is_exact);
    TT_RUN(porta_does_not_depend_on_block_size);
    TT_RUN(porta_zero_is_off);
    TT_RUN(porta_first_note_does_not_glide);
    TT_RUN(porta_set_to_zero_mid_glide_lands);
    TT_RUN(porta_each_note_glides_from_the_last_played);
    TT_RUN(porta_stops_on_a_voice_gone_quiet);
    TT_RUN(porta_unison_mid_glide_goes_on_from_where_it_is);
    TT_RUN(porta_stealing_note_keeps_its_glide_when_the_stolen_key_comes_back);
    TT_RUN(porta_same_note_again_mid_glide_lands);
    TT_RUN(porta_stealing_note_glides_from_the_last_note);
    TT_RUN(porta_sequencer_and_keys_share_the_last_note);
}
