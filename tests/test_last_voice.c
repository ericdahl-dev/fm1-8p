/*
 * A key played again gets its last voice back, as on PG-8X 2.0.13 (#164,
 * measured 2026-11-19): when that voice is idle and no other key has played
 * it since. Otherwise the usual rule picks: an unplayed or idle voice first
 * (the longest idle), then the oldest released, then the oldest held.
 * Through the engine's voices: which one plays each note.
 */
#include "../engine/fm1_8p.h"

#define LV_RATE 48000u

static fm_engine lv_eng;
static int16_t lv_buf[LV_RATE / 100 * 2];

/* render `ms` of audio in 10 ms blocks */
static void lv_run(uint32_t ms)
{
    while (ms) {
        uint32_t k = ms < 10 ? ms : 10;
        fm_engine_render(&lv_eng, lv_buf, LV_RATE / 1000u * k);
        ms -= k;
    }
}

/* `voices` voices with a 1 ms release: idle 50 ms after a note off */
static void lv_init(uint8_t voices)
{
    TT_CHECK_EQ(fm_engine_init(&lv_eng, LV_RATE, voices), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_AMP_RELEASE_MS, 1), FM_OK);
}

/* the voice sounding `note`, -1 if none */
static int lv_voice(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < lv_eng.voice_count; i++)
        if (fm_voice_active(&lv_eng.voices[i]) && fm_voice_note(&lv_eng.voices[i]) == note) return i;
    return -1;
}

/* `note` struck and let go; the voice it took, idle again */
static int lv_tap(uint8_t note)
{
    int v;
    fm_engine_note_on(&lv_eng, note, 100);
    lv_run(20);
    v = lv_voice(note);
    fm_engine_note_off(&lv_eng, note);
    lv_run(50);
    return v;
}

/* 1. C4, then D4 (an unplayed voice), then C4 again: C4's own voice, not the next unplayed one */
TT_TEST(a_key_played_again_gets_its_idle_voice_back)
{
    int c, d;
    lv_init(6);
    c = lv_tap(60);
    d = lv_tap(62);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 0);
    TT_CHECK(c >= 0 && d >= 0 && c != d);
    fm_engine_note_on(&lv_eng, 60, 100);
    if (lv_voice(60) != c) printf("    C4 again on voice %d, its last %d\n", lv_voice(60), c);
    TT_CHECK_EQ(lv_voice(60), c);
}

/* 2. two voices: C4, D4, then E4 plays C4's voice (the longest idle); C4 again takes D4's, the longest idle
 * now: its own voice has another key */
TT_TEST(a_key_whose_voice_another_key_played_takes_the_longest_idle)
{
    int c, d, e;
    lv_init(2);
    c = lv_tap(60);
    d = lv_tap(62);
    e = lv_tap(64);
    TT_CHECK_EQ(e, c);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), d);
}

/* 3. a new key still takes an unplayed voice before one a key has let go */
TT_TEST(a_new_key_takes_an_unplayed_voice_first)
{
    int c;
    lv_init(6);
    c = lv_tap(60);
    fm_engine_note_on(&lv_eng, 62, 100);
    TT_CHECK(lv_voice(62) >= 0);
    TT_CHECK(lv_voice(62) != c);
}

/* 4. with no idle voice the steal order holds: two voices, C4 let go on voice c, then D4 held (the other)
 * and E4 held (on c). C4 again steals D4's voice, the oldest held, not its last voice (E4 holds it) */
TT_TEST(a_key_whose_voice_is_held_steals_the_oldest_held)
{
    int c, d;
    lv_init(2);
    c = lv_tap(60);
    fm_engine_note_on(&lv_eng, 62, 100);
    lv_run(10);
    d = lv_voice(62);
    fm_engine_note_on(&lv_eng, 64, 100);
    lv_run(10);
    TT_CHECK_EQ(lv_voice(64), c);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), d);
    TT_CHECK_EQ(lv_voice(64), c);
}

/* 5. unison is untouched: every voice plays the key again, and a new key */
TT_TEST(unison_plays_every_voice_for_a_key_again)
{
    lv_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON), FM_OK);
    lv_tap(60);
    lv_tap(62);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 6);
}

/* 6. two voices, C4 and D4 held; E4 steals C4's voice. All let go (D4 first): C4 again takes D4's voice,
 * the longest idle, as before (its own has E4) */
TT_TEST(a_key_whose_voice_was_stolen_takes_the_longest_idle)
{
    int c, d;
    lv_init(2);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    fm_engine_note_on(&lv_eng, 62, 100);
    lv_run(10);
    c = lv_voice(60);
    d = lv_voice(62);
    fm_engine_note_on(&lv_eng, 64, 100);
    lv_run(30);
    TT_CHECK_EQ(lv_voice(64), c);
    fm_engine_note_off(&lv_eng, 60);
    fm_engine_note_off(&lv_eng, 62);
    lv_run(10);
    fm_engine_note_off(&lv_eng, 64);
    lv_run(50);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 0);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), d);
}

/* 7. portamento: the voice a key gets back glides from the last note played, as any note does. Tau 1 s:
 * C4, then E4, then C4 again on its own voice, at E4's pitch or just below */
TT_TEST(a_voice_got_back_glides_from_the_last_note_played)
{
    int c;
    double p;
    lv_init(6);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_PORTAMENTO_MS, 1000), FM_OK);
    c = lv_tap(60);
    lv_tap(64);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(1);
    TT_CHECK_EQ(lv_voice(60), c);
    p = c >= 0 ? lv_eng.voices[c].pitch / 65536.0 : -1000.0;
    if (!(p > 63.9 && p <= 64.0)) printf("    C4 again at %.3f st 1 ms on, want 64 or just below\n", p);
    TT_CHECK(p > 63.9 && p <= 64.0);
}

/* 8. a key's voice still sounding keeps its path: released, the key again takes it back (find_note). Stolen
 * (#313: the new note starts at once), the voice is the stealing key's now, and the key again steals by the
 * usual rule, the oldest held */
TT_TEST(a_key_whose_voice_still_sounds_keeps_its_path)
{
    int c, d;
    lv_init(2);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    c = lv_voice(60);
    fm_engine_note_off(&lv_eng, 60);
    fm_engine_note_on(&lv_eng, 62, 100); /* the other voice */
    fm_engine_note_on(&lv_eng, 60, 100); /* C4 still releasing */
    TT_CHECK_EQ(lv_voice(60), c);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 2);
    lv_init(2);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    fm_engine_note_on(&lv_eng, 62, 100);
    lv_run(10);
    c = lv_voice(60);
    d = lv_voice(62);
    fm_engine_note_on(&lv_eng, 64, 100); /* steals C4's voice, the oldest held */
    TT_CHECK_EQ(lv_voice(64), c);
    lv_run(1);
    fm_engine_note_on(&lv_eng, 60, 100); /* C4 again: D4's voice is now the oldest held */
    TT_CHECK_EQ(lv_voice(60), d);
    TT_CHECK_EQ(lv_voice(64), c);
}

/* 9. a steal makes the stealing key the voice's last note (#313: it sounds there at once), and all sound off
 * keeps it. Two voices, C4 and D4 held; E4 steals C4's voice; CC 120, keys up. E4 again gets that voice
 * back; C4, whose voice was taken, takes the other (D4's), the longest idle */
TT_TEST(a_stolen_voice_is_the_stealing_keys_last_voice)
{
    int c, d;
    lv_init(2);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    fm_engine_note_on(&lv_eng, 62, 100);
    lv_run(10);
    c = lv_voice(60);
    d = lv_voice(62);
    fm_engine_note_on(&lv_eng, 64, 100);
    TT_CHECK_EQ(lv_voice(64), c);
    lv_run(1);
    fm_engine_controller(&lv_eng, 120, 0);
    fm_engine_note_off(&lv_eng, 60);
    fm_engine_note_off(&lv_eng, 62);
    fm_engine_note_off(&lv_eng, 64);
    lv_run(10);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 0);
    TT_CHECK(c >= 0);
    if (c >= 0) TT_CHECK_EQ(fm_voice_note(&lv_eng.voices[c]), 64);
    fm_engine_note_on(&lv_eng, 64, 100);
    TT_CHECK_EQ(lv_voice(64), c);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), d);
}

/* 10. a mode switch right after a steal clears the voice's note; all sound off after it keeps it cleared
 * (Codex P2 on #164, round 2; there is no steal fade since #313) */
TT_TEST(all_sound_off_after_a_mode_switch_keeps_a_stolen_voice_without_a_note)
{
    int c;
    lv_init(2);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    fm_engine_note_on(&lv_eng, 62, 100);
    lv_run(10);
    c = lv_voice(60);
    fm_engine_note_on(&lv_eng, 64, 100); /* steals C4's voice */
    lv_run(1);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON), FM_OK);
    fm_engine_controller(&lv_eng, 120, 0);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY), FM_OK);
    TT_CHECK(c >= 0);
    if (c >= 0) TT_CHECK_EQ(fm_voice_note(&lv_eng.voices[c]), FM_VOICE_NO_NOTE);
}

/* 11. the event count wrapping to 0 on a note off still leaves the voice its key's (Codex P3 on #164). Two
 * voices: D4 held, C4 on the other; D4 let go, then C4, whose note off is stamped 0. C4 again gets its voice
 * back, though D4's is the longer idle */
TT_TEST(a_key_gets_its_voice_back_across_the_event_count_wrap)
{
    int c;
    lv_init(2);
    lv_eng.stamp = 0xFFFFFFFCu;
    fm_engine_note_on(&lv_eng, 62, 100);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    c = lv_voice(60);
    fm_engine_note_off(&lv_eng, 62);
    fm_engine_note_off(&lv_eng, 60);
    lv_run(50);
    TT_CHECK(c >= 0);
    if (c >= 0) TT_CHECK_EQ(fm_voice_stamp(&lv_eng.voices[c]), 0);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), c);
}

/* 12. a mode switch starts clean: unison's note, left on idle voices, is no voice's last note in poly (Codex
 * P2 on #164, round 3). Two voices: C4 in unison, let go, idle; in poly C4 takes the longest idle voice
 * (the first, let go first), not the newest */
TT_TEST(a_mode_switch_clears_every_voices_last_note)
{
    uint8_t i;
    lv_init(2);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON), FM_OK);
    lv_tap(60);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 0);
    TT_CHECK_EQ(fm_engine_set_param(&lv_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY), FM_OK);
    for (i = 0; i < lv_eng.voice_count; i++) TT_CHECK_EQ(fm_voice_note(&lv_eng.voices[i]), FM_VOICE_NO_NOTE);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), 0);
}

/* 13. our choice (PG-8X not measured here): all sound off on a plain sounding voice keeps its last note. Two
 * voices: C4 held on voice c, D4 on the other and let go; CC 120; C4 again gets voice c back, though D4's
 * voice is the longer idle (Codex P3 on #309) */
TT_TEST(all_sound_off_on_a_sounding_voice_keeps_its_last_note)
{
    int c;
    lv_init(2);
    fm_engine_note_on(&lv_eng, 62, 100);
    fm_engine_note_off(&lv_eng, 62);
    fm_engine_note_on(&lv_eng, 60, 100);
    lv_run(10);
    c = lv_voice(60);
    fm_engine_controller(&lv_eng, 120, 0);
    fm_engine_note_off(&lv_eng, 60);
    lv_run(10);
    TT_CHECK_EQ(fm_engine_active_voices(&lv_eng), 0);
    TT_CHECK(c >= 0);
    if (c >= 0) TT_CHECK_EQ(fm_voice_note(&lv_eng.voices[c]), 60);
    fm_engine_note_on(&lv_eng, 60, 100);
    TT_CHECK_EQ(lv_voice(60), c);
}

static void run_last_voice_tests(void)
{
    TT_RUN(a_key_played_again_gets_its_idle_voice_back);
    TT_RUN(a_key_whose_voice_another_key_played_takes_the_longest_idle);
    TT_RUN(a_new_key_takes_an_unplayed_voice_first);
    TT_RUN(a_key_whose_voice_is_held_steals_the_oldest_held);
    TT_RUN(unison_plays_every_voice_for_a_key_again);
    TT_RUN(a_key_whose_voice_was_stolen_takes_the_longest_idle);
    TT_RUN(a_voice_got_back_glides_from_the_last_note_played);
    TT_RUN(a_key_whose_voice_still_sounds_keeps_its_path);
    TT_RUN(a_stolen_voice_is_the_stealing_keys_last_voice);
    TT_RUN(all_sound_off_after_a_mode_switch_keeps_a_stolen_voice_without_a_note);
    TT_RUN(a_key_gets_its_voice_back_across_the_event_count_wrap);
    TT_RUN(a_mode_switch_clears_every_voices_last_note);
    TT_RUN(all_sound_off_on_a_sounding_voice_keeps_its_last_note);
}
