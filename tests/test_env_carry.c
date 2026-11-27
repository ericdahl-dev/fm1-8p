/*
 * A note-on never starts the filter envelope (ENV-1) or envelope 2 again
 * from 0: each attacks from the level it is at, whatever the amp is doing,
 * and both keep running on a voice that has gone idle (#396). As PG-8X
 * 2.0.13 plays it, measured 2026-11-26 (SELFNOISE 0, 48 kHz, one saw, the
 * envelope read from DCO-1's pitch at DCO ENV 64, where full is 1573.4
 * cents; attack 85, sustain 127, release 109, the VCA on gate so the amp
 * is idle at key-up). C3 held 4 s, a gap, C3 again:
 *   gap                        0.2 s    1 s      4 s     12 s    40 s
 *   the second note starts at  1543.1   1405.5   991.7   399.1   39.4
 *   the release alone, there   1531.6   1392.5   974.3   375.2   11.7
 * (a voice's first note reads 27.4 in that 43 ms window, the attack's own
 * rise). The same with the VCA on ENV-2 at release 0 or 96, with ENV-2 on
 * the pitch under the gate, and from any stage the key-up came in.
 *
 * Through the engine alone: the envelope on osc 1's pitch (1200 cents at
 * full), read from the output's period.
 */
#include "../engine/fm1_8p.h"

#define EC_RATE 48000u
#define EC_DEPTH 1200 /* cents at the envelope's full level */

static fm_engine ec_eng;
static int16_t ec_buf[EC_RATE / 10 * 2];

/* render `ms` of audio in blocks of at most 10 ms */
static void ec_run(uint32_t ms)
{
    while (ms) {
        uint32_t k = ms < 10 ? ms : 10;
        fm_engine_render(&ec_eng, ec_buf, EC_RATE / 1000u * k);
        ms -= k;
    }
}

static void ec_set(uint16_t id, int32_t v)
{
    TT_CHECK_EQ(fm_engine_set_param(&ec_eng, id, v), FM_OK);
}

/* `voices` voices, one saw, the amp gone 1 ms after key-up; the filter
 * envelope on osc 1's pitch: a 2 s attack to full, a 4 s release */
static void ec_init(uint8_t voices)
{
    TT_CHECK_EQ(fm_engine_init(&ec_eng, EC_RATE, voices), FM_OK);
    ec_set(FM_PARAM_OSC2_LEVEL, 0);
    ec_set(FM_PARAM_AMP_ATTACK_MS, 1);
    ec_set(FM_PARAM_AMP_SUSTAIN, 1000);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 1);
    ec_set(FM_PARAM_FILTER_ENV_ATTACK_MS, 2000);
    ec_set(FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 4000);
    ec_set(FM_PARAM_PITCH_ENV_OSC1_CENTS, EC_DEPTH);
    ec_set(FM_PARAM_PITCH_ENV_SOURCE, 0);
}

/* the pitch over the next 20 ms, in cents above `note` */
static double ec_cents(uint8_t note)
{
    double p;
    fm_engine_render(&ec_eng, ec_buf, EC_RATE / 50);
    p = period_from_crossings(ec_buf, EC_RATE / 50, 0);
    return p > 0 ? 1200.0 * log2(EC_RATE / p / note_hz(note)) : -1e9;
}

/* `note` struck at `velocity`; where the envelope has it 20..40 ms on */
static double ec_strike(uint8_t note, uint8_t velocity)
{
    fm_engine_note_on(&ec_eng, note, velocity);
    ec_run(20);
    return ec_cents(note);
}

/* `note` held `ms`, then let go */
static void ec_hold(uint8_t note, uint32_t ms)
{
    fm_engine_note_on(&ec_eng, note, 100);
    ec_run(ms);
    fm_engine_note_off(&ec_eng, note);
}

/* 1. C4 held past the attack, let go; half a second on its amp is long gone, and C4 again starts
 * from where the 4 s release is (about 0.88 of full), not from 0 */
TT_TEST(a_key_played_again_after_its_amp_has_ended_carries_the_filter_envelope_on)
{
    double first, again;
    ec_init(4);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    again = ec_strike(60, 100);
    if (!(again > 900.0)) printf("    first note %.1f cents, again %.1f\n", first, again);
    TT_CHECK(first < 100.0);
    TT_CHECK(again > 900.0 && again < EC_DEPTH);
}

/* C4 held 2.5 s, let go, and struck again `gap_ms` on, with the amp's release `amp_release_ms`:
 * where the envelope has the second note 20..40 ms on */
static double ec_again_after(uint32_t gap_ms, int32_t amp_release_ms)
{
    ec_init(4);
    ec_set(FM_PARAM_AMP_RELEASE_MS, amp_release_ms);
    ec_hold(60, 2500);
    ec_run(gap_ms);
    return ec_strike(60, 100);
}

/* 2. the release runs on while the voice is idle: 3 s into the 4 s release the note starts where a
 * voice whose amp still sounds has it (about 0.47 of full), well under the start half a second in */
TT_TEST(the_release_runs_on_while_the_voice_is_idle)
{
    double idle = ec_again_after(3000, 1), early, sounding;
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 1); /* the one note: its voice was idle */
    early = ec_again_after(500, 1);
    sounding = ec_again_after(3000, 20000);
    if (fabs(idle - sounding) > 3.0) printf("    3 s on: idle voice %.1f cents, sounding voice %.1f; 0.5 s on %.1f\n", idle, sounding, early);
    TT_CHECK(sounding > 500.0 && sounding < 700.0);
    TT_CHECK_NEAR(idle, sounding, 3.0);
    TT_CHECK(idle < early - 300.0);
}

/* the voice sounding `note`, -1 if none */
static int ec_voice(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < ec_eng.voice_count; i++)
        if (fm_voice_active(&ec_eng.voices[i]) && fm_voice_note(&ec_eng.voices[i]) == note) return i;
    return -1;
}

/* 3. once the release has ended the note starts as a voice's first note does (PG-8X: release 64, gap
 * 10 s, 26.7 cents against a first note's 27.4) */
TT_TEST(after_the_release_has_ended_a_note_starts_as_a_voices_first)
{
    double first, again;
    ec_init(4);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 100);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(3000);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0) printf("    first note %.2f cents, again %.2f\n", first, again);
    TT_CHECK_NEAR(again, first, 1.0);
}

/* 4. a different key taking a played, idle voice carries on too. PG-8X: six keys fill its six voices,
 * then B3, C#3 and D#3 start 156.2, 156.0 and 155.8 cents over a first note, where the releases of
 * voices 1, 2 and 3 are (155.8, 156.0, 156.1; their neighbors' 215.4 and 113.6). Here two voices: C4
 * held, D4 tapped, then E4 takes C4's voice, the longest idle */
TT_TEST(a_different_key_on_a_played_idle_voice_carries_on)
{
    int c;
    double e;
    ec_init(2);
    fm_engine_note_on(&ec_eng, 60, 100);
    ec_run(2500);
    c = ec_voice(60);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(200);
    ec_hold(62, 20);
    ec_run(280);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    e = ec_strike(64, 100);
    TT_CHECK(c >= 0);
    TT_CHECK_EQ(ec_voice(64), c);
    if (!(e > 900.0)) printf("    E4 on C4's voice starts at %.1f cents\n", e);
    TT_CHECK(e > 900.0 && e < EC_DEPTH);
}

/* 5. envelope 2 under the gate (the VCA on GATE, so envelope 2 runs apart from the amp) carries on as
 * the filter envelope does: PG-8X reads the same 1543.1 / 1405.5 / 991.7 with ENV-2 on the pitch */
TT_TEST(envelope_2_under_the_gate_carries_on)
{
    double first, again;
    ec_init(4);
    ec_set(FM_PARAM_AMP_GATE, 1);
    ec_set(FM_PARAM_AMP_ATTACK_MS, 2000);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 4000);
    ec_set(FM_PARAM_PITCH_ENV_SOURCE, 1);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    again = ec_strike(60, 100);
    if (!(again > 900.0)) printf("    first note %.1f cents, again %.1f\n", first, again);
    TT_CHECK(first < 100.0);
    TT_CHECK(again > 900.0 && again < EC_DEPTH);
}

/* 5b. envelope 2's release under the gate runs on at its own rate while the voice is idle: with the
 * filter envelope's times (a 2 s attack, a 4 s release) it has a note 3 s on where the filter
 * envelope has it on a voice whose amp still sounds */
TT_TEST(envelope_2_under_the_gate_releases_at_its_rate_while_idle)
{
    double sounding = ec_again_after(3000, 20000), gated;
    ec_init(4);
    ec_set(FM_PARAM_AMP_GATE, 1);
    ec_set(FM_PARAM_AMP_ATTACK_MS, 2000);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 4000);
    ec_set(FM_PARAM_PITCH_ENV_SOURCE, 1);
    ec_hold(60, 2500);
    ec_run(3000);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    gated = ec_strike(60, 100);
    if (fabs(gated - sounding) > 3.0) printf("    3 s on: envelope 2 on an idle voice %.1f cents, the filter envelope on a sounding one %.1f\n", gated, sounding);
    TT_CHECK(sounding > 500.0 && sounding < 700.0);
    TT_CHECK_NEAR(gated, sounding, 3.0);
}

/* C4 at `first` held 2.5 s, let go, and C4 at `second` half a second on, the envelope's depth on
 * DYNAMICS 3 */
static double ec_velocities(uint8_t first, uint8_t second)
{
    ec_init(4);
    ec_set(FM_PARAM_PITCH_ENV_DYNA, 3);
    fm_engine_note_on(&ec_eng, 60, first);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    return ec_strike(60, second);
}

/* 6. the level carried is the envelope's own, before the velocity's share of the depth: the new
 * note's velocity sets the depth at once. PG-8X (DCO DYNA 3, gap 1 s): 127 after 50 starts at 1368.1
 * cents and 127 after 127 at 1368.3; 50 after 127 at 278.1, the release's 1355.4 at velocity 50's
 * depth (311.6 of 1531.5) */
TT_TEST(the_new_notes_velocity_sets_the_depth_of_the_level_carried)
{
    double loud = ec_velocities(127, 127), after_soft = ec_velocities(50, 127), soft = ec_velocities(127, 50);
    if (fabs(loud - after_soft) > 1.0 || !(soft < loud / 2))
        printf("    127 after 127: %.1f cents; 127 after 50: %.1f; 50 after 127: %.1f\n", loud, after_soft, soft);
    TT_CHECK(loud > 900.0);
    TT_CHECK_NEAR(after_soft, loud, 1.0);
    TT_CHECK(soft > 100.0 && soft < loud / 2);
}

/* one voice, the envelope's key follow at 780 per mille (its times x 0.115 four octaves over C2):
 * `old` held 2.5 s and let go, `next` struck 1 s on */
static double ec_follow(uint8_t old, uint8_t next)
{
    ec_init(1);
    ec_set(FM_PARAM_FILTER_ENV_KEY_FOLLOW, 780);
    ec_hold(old, 2500);
    ec_run(1000);
    return ec_strike(next, 100);
}

/* 7. with key follow the release runs on at the times of the key that played it, not the next key's.
 * PG-8X (ENV-1 KEY 96, as test 4): B3, C#3 and D#3 start 64.7, 52.7 and 39.8 cents over a first note,
 * where C3's, D3's and E3's own releases are (66.0, 52.0, 39.6). C3's 2.3 s release is at 0.65 a second
 * on, C6's 0.46 s one at 0.11 */
TT_TEST(an_idle_release_runs_at_the_times_of_the_key_that_played_it)
{
    double low_then_high = ec_follow(48, 84), high_then_low = ec_follow(84, 48);
    if (!(low_then_high > 700.0 && high_then_low > 100.0 && high_then_low < 300.0))
        printf("    C6 after C3: %.1f cents; C3 after C6: %.1f\n", low_then_high, high_then_low);
    TT_CHECK(low_then_high > 700.0);
    TT_CHECK(high_then_low > 100.0 && high_then_low < 300.0);
}

/* 8. all sound off still silences the envelopes: the next note starts as a voice's first (our choice;
 * PG-8X is not measured there) */
TT_TEST(all_sound_off_starts_the_envelopes_from_0)
{
    double first, again;
    ec_init(4);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_controller(&ec_eng, 120, 0);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0) printf("    first note %.2f cents, after all sound off %.2f\n", first, again);
    TT_CHECK_NEAR(again, first, 1.0);
}

/* 9. unison: a key played after the stack has gone idle carries on, on every voice (PG-8X's unison is
 * not measured; the same rule as poly) */
TT_TEST(a_unison_note_on_idle_voices_carries_on)
{
    double first, again;
    ec_init(4);
    ec_set(FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    ec_set(FM_PARAM_UNISON_DETUNE_CENTS, 0);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    again = ec_strike(62, 100);
    if (!(again > 900.0)) printf("    unison: first note %.1f cents, D4 after C4 %.1f\n", first, again);
    TT_CHECK(first < 100.0);
    TT_CHECK(again > 900.0 && again < EC_DEPTH);
}

/* 10. what an idle voice costs: while its envelope releases, a count that never reaches
 * FM_ENV_LAG_MAX (caught up within a block); once it has ended, and on a voice not played yet,
 * nothing: no count is kept */
TT_TEST(an_idle_voices_envelope_is_caught_up_within_a_block_and_then_costs_nothing)
{
    const fm_env *f, *f2, *unplayed;
    uint32_t i, counted = 0;
    ec_init(4);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 100);
    ec_hold(60, 300);
    f = &ec_eng.voices[ec_voice(60) < 0 ? 0 : ec_voice(60)].filter_env;
    f2 = &ec_eng.voices[ec_voice(60) < 0 ? 0 : ec_voice(60)].env2; /* without the gate: never started */
    unplayed = &ec_eng.voices[3].filter_env;
    ec_run(50);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    TT_CHECK_EQ(f->stage, FM_ENV_RELEASE);
    for (i = 0; i < 40; i++) { /* 280 ms in 7 ms blocks: the release runs out */
        fm_engine_render(&ec_eng, ec_buf, 336);
        TT_CHECK(f->lag < FM_ENV_LAG_MAX);
        TT_CHECK_EQ(f2->lag, 0u);
        counted += f->lag != 0;
    }
    TT_CHECK(counted > 0);
    ec_run(1500);
    TT_CHECK_EQ(f->stage, FM_ENV_IDLE);
    for (i = 0; i < 40; i++) {
        fm_engine_render(&ec_eng, ec_buf, 336);
        TT_CHECK_EQ(f->lag, 0u);
        TT_CHECK_EQ(f2->lag, 0u);
        TT_CHECK_EQ(unplayed->lag, 0u);
    }
    TT_CHECK_EQ(unplayed->stage, FM_ENV_IDLE);
}

/* 11. a patch load leaves the voices idle, not silenced (firmware/play.c: all notes off, then the
 * params), so an idle voice's release takes the new patch's release time from the level it is at:
 * no jump, and no level left standing. A 50 ms release set 200 ms into the old 4 s one: a second on
 * the note starts as a voice's first; left alone it starts from 0.74 */
TT_TEST(a_new_release_time_reaches_an_idle_voices_release)
{
    double first, kept, again;
    ec_init(4);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(1200);
    kept = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(200);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 50);
    ec_run(1000);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0) printf("    first note %.2f cents; old release kept %.1f; new release %.2f\n", first, kept, again);
    TT_CHECK(kept > 800.0);
    TT_CHECK_NEAR(again, first, 1.0);
}

static int16_t ec_take[2][EC_RATE / 50 * 2];

/* C4 held 300 ms, let go, struck again 29 ms on, all rendered `block` frames a call; the second
 * note's first 20 ms into `out`. With `to_cutoff` the envelope moves the cutoff too, so the voice
 * takes the render loop that runs the filter */
static void ec_blocks(uint32_t block, int to_cutoff, int16_t *out)
{
    uint32_t i;
    ec_init(4);
    ec_set(FM_PARAM_FILTER_ENV_ATTACK_MS, 50);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 20);
    if (to_cutoff) {
        ec_set(FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
        ec_set(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 3600);
    }
    fm_engine_note_on(&ec_eng, 60, 100);
    for (i = 0; i < EC_RATE * 3 / 10; i += block) fm_engine_render(&ec_eng, ec_buf, block);
    fm_engine_note_off(&ec_eng, 60);
    for (i = 0; i < 22 * 64; i += block) fm_engine_render(&ec_eng, ec_buf, block); /* 29 ms */
    fm_engine_note_on(&ec_eng, 60, 100);
    for (i = 0; i < EC_RATE / 50; i += block) fm_engine_render(&ec_eng, out + 2 * i, block);
}

/* 12. the level carried does not depend on the caller's block size: the envelopes pass the whole of
 * the chunk the amp ends in, not only the samples the voice still sounded for. The amp ends about
 * 10 ms into a 20 ms release, so a chunk's worth of samples lost would show */
TT_TEST(the_level_carried_does_not_depend_on_the_block_size)
{
    uint32_t i, differ;
    int to_cutoff;
    for (to_cutoff = 0; to_cutoff < 2; to_cutoff++) {
        ec_blocks(1, to_cutoff, ec_take[0]);
        ec_blocks(64, to_cutoff, ec_take[1]);
        for (i = 0, differ = 0; i < EC_RATE / 50 * 2; i++) differ += ec_take[0][i] != ec_take[1][i];
        TT_CHECK(peak_abs(ec_take[0], 0, EC_RATE / 50) > 1000);
        if (differ) printf("    %s: %u samples differ\n", to_cutoff ? "with the filter" : "plain", (unsigned)differ);
        TT_CHECK_EQ(differ, 0u);
    }
}

/* 13. a patch load (all notes off, fm_engine_patch_loaded, the new params) does not carry the last
 * patch's envelope into the new patch's notes: a voice idle at the load starts its next note as a
 * voice's first. (PG-8X through a parameter step by the host, 2026-11-26, carries on: 1308.7 cents
 * against a first note's 27.4; a program change itself could not be rendered. A load starting from 0
 * is our choice, as before #396.) */
TT_TEST(a_patch_load_starts_an_idle_voices_envelopes_from_0)
{
    double first, again;
    ec_init(4);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(200);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    ec_set(FM_PARAM_FILTER_ENV_RELEASE_MS, 6000);
    ec_run(500);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0) printf("    first note %.2f cents, after a patch load %.2f\n", first, again);
    TT_CHECK_NEAR(again, first, 1.0);
}

/* 14. a voice still sounding at the load keeps its release (the envelope stays on the pitch while
 * the note dies away), and its next note, once it has gone idle, starts from 0 too */
TT_TEST(a_patch_load_lets_a_sounding_voice_release_and_then_start_from_0)
{
    double first, releasing, again;
    ec_init(4);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 300);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    ec_run(100);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 1);
    releasing = ec_cents(60);
    ec_run(4000);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0 || !(releasing > 1000.0))
        printf("    first note %.2f cents; releasing through the load %.1f; its next note %.2f\n", first, releasing, again);
    TT_CHECK(releasing > 1000.0);
    TT_CHECK_NEAR(again, first, 1.0);
}

/* 15. a voice still sounding at the load and struck again while it sounds carries on, as any note
 * struck again does; and a later note on it, after another release, is not started from 0 by the
 * load long past */
TT_TEST(a_patch_load_does_not_reach_a_note_struck_while_its_voice_still_sounds)
{
    double again, later;
    ec_init(4);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 20000);
    ec_hold(60, 2500);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    ec_run(500);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 1);
    again = ec_strike(60, 100);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 1);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    later = ec_strike(60, 100);
    if (!(again > 900.0 && later > 900.0)) printf("    struck while sounding %.1f cents; a note after its next release %.1f\n", again, later);
    TT_CHECK(again > 900.0 && again < EC_DEPTH);
    TT_CHECK(later > 900.0 && later < EC_DEPTH);
}

/* 16. the level carried is on the pitch from the note's first sample, not from its first control
 * tick (up to 31 samples on): at the note-on, at every tick phase, osc 1's pitch sum is already the
 * carried level's depth (0.88 of 1200 cents; Q16.16 octaves), and the next tick leaves it there */
TT_TEST(a_carried_pitch_envelope_is_on_the_pitch_from_the_first_sample)
{
    uint32_t phase;
    for (phase = 0; phase < 32; phase += 5) {
        int v;
        int32_t at_on, after;
        ec_init(4);
        ec_set(FM_PARAM_PITCH_ENV_OSC2_CENTS, EC_DEPTH / 2);
        ec_hold(60, 2500);
        ec_run(500);
        fm_engine_render(&ec_eng, ec_buf, phase + 1);
        fm_engine_note_on(&ec_eng, 60, 100);
        v = ec_voice(60);
        TT_CHECK(v >= 0);
        if (v < 0) return;
        at_on = fm_mod_get(&ec_eng.voices[v].mod, FM_MOD_PITCH1);
        TT_CHECK_EQ(fm_mod_get(&ec_eng.voices[v].mod, FM_MOD_PITCH2), at_on / 2); /* osc 2's, at half the depth */
        fm_engine_render(&ec_eng, ec_buf, 64);
        after = fm_mod_get(&ec_eng.voices[v].mod, FM_MOD_PITCH1);
        if (!(at_on > 55000 && after - at_on < 600))
            printf("    tick phase %u: pitch sum %d at the note-on, %d 64 samples on\n", (unsigned)phase, (int)at_on, (int)after);
        TT_CHECK(at_on > 55000 && at_on < 65536);
        TT_CHECK(after >= at_on && after - at_on < 600);
    }
}

/* 17. the same on a unison stack gone idle */
TT_TEST(a_carried_pitch_envelope_is_on_a_unison_notes_pitch_from_the_first_sample)
{
    int32_t at_on;
    ec_init(4);
    ec_set(FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    ec_set(FM_PARAM_PITCH_ENV_OSC2_CENTS, EC_DEPTH / 2);
    ec_hold(60, 2500);
    ec_run(500);
    fm_engine_note_on(&ec_eng, 62, 100);
    at_on = fm_mod_get(&ec_eng.voices[0].mod, FM_MOD_PITCH1);
    TT_CHECK_EQ(fm_mod_get(&ec_eng.voices[0].mod, FM_MOD_PITCH2), at_on / 2);
    if (!(at_on > 55000)) printf("    unison: pitch sum %d at the note-on\n", (int)at_on);
    TT_CHECK(at_on > 55000 && at_on < 65536);
}

/* 18. the velocity's share of the depth (DYNAMICS) is in the pitch sum from the first sample too: a
 * note at 50 after one at 127 starts where its first control tick leaves it, well under the level
 * carried at full depth */
TT_TEST(a_carried_pitch_envelopes_first_sample_has_the_new_velocitys_depth)
{
    int v;
    int32_t at_on, after;
    ec_init(4);
    ec_set(FM_PARAM_PITCH_ENV_DYNA, 3);
    fm_engine_note_on(&ec_eng, 60, 127);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(500);
    fm_engine_render(&ec_eng, ec_buf, 8);
    fm_engine_note_on(&ec_eng, 60, 50);
    v = ec_voice(60);
    TT_CHECK(v >= 0);
    if (v < 0) return;
    at_on = fm_mod_get(&ec_eng.voices[v].mod, FM_MOD_PITCH1);
    fm_engine_render(&ec_eng, ec_buf, 64);
    after = fm_mod_get(&ec_eng.voices[v].mod, FM_MOD_PITCH1);
    if (!(at_on > 2000 && at_on < 28000 && after >= at_on && after - at_on < 300))
        printf("    50 after 127: pitch sum %d at the note-on, %d 64 samples on\n", (int)at_on, (int)after);
    TT_CHECK(at_on > 2000 && at_on < 28000);
    TT_CHECK(after >= at_on && after - at_on < 300);
}

/* 19. envelope 2 under the gate goes back to 0 at a patch load too (test 13's twin) */
TT_TEST(a_patch_load_starts_an_idle_voices_envelope_2_from_0)
{
    double first, again;
    ec_init(4);
    ec_set(FM_PARAM_AMP_GATE, 1);
    ec_set(FM_PARAM_AMP_ATTACK_MS, 2000);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 4000);
    ec_set(FM_PARAM_PITCH_ENV_SOURCE, 1);
    first = ec_strike(60, 100);
    ec_run(2500);
    fm_engine_note_off(&ec_eng, 60);
    ec_run(200);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    ec_run(500);
    again = ec_strike(60, 100);
    if (fabs(again - first) > 1.0) printf("    envelope 2: first note %.2f cents, after a patch load %.2f\n", first, again);
    TT_CHECK_NEAR(again, first, 1.0);
}

/* a note of `ms`, a patch load while it sounds (`gated`: envelope 2 under the gate on the pitch; else
 * the filter envelope, the amp's release 300 ms), a wait past the amp's end, then `note` struck 7
 * samples after a control tick: osc 1's pitch sum at that note-on */
static int32_t ec_sum_after_load(int gated, int unison, uint8_t note)
{
    int32_t sum = -1;
    uint8_t i;
    ec_init(4);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 300);
    if (gated) {
        ec_set(FM_PARAM_AMP_GATE, 1);
        ec_set(FM_PARAM_AMP_ATTACK_MS, 2000);
        ec_set(FM_PARAM_AMP_RELEASE_MS, 4000);
        ec_set(FM_PARAM_PITCH_ENV_SOURCE, 1);
    }
    if (unison) ec_set(FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_note_on(&ec_eng, 60, 100);
    ec_run(2500);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    ec_run(4000);
    if (fm_engine_active_voices(&ec_eng) != 0) return -2;
    fm_engine_render(&ec_eng, ec_buf, 7);
    fm_engine_note_on(&ec_eng, note, 100);
    for (i = 0; i < ec_eng.voice_count; i++)
        if (fm_voice_active(&ec_eng.voices[i])) sum = fm_mod_get(&ec_eng.voices[i].mod, FM_MOD_PITCH1);
    return sum;
}

/* 20. a voice a load found sounding, idle since: its next note has nothing of the last patch's
 * envelope on its pitch, from the first sample (not only once the envelopes are back at 0 and a
 * control tick has passed). Poly and unison, the filter envelope and envelope 2 under the gate */
TT_TEST(a_note_after_a_patch_load_has_no_old_envelope_on_its_pitch)
{
    int gated, unison;
    for (gated = 0; gated < 2; gated++)
        for (unison = 0; unison < 2; unison++) {
            int32_t sum = ec_sum_after_load(gated, unison, unison ? 62 : 60);
            if (sum != 0) printf("    %s, %s: pitch sum %d at the note-on after a load\n", gated ? "envelope 2 under the gate" : "filter envelope", unison ? "unison" : "poly", (int)sum);
            TT_CHECK_EQ(sum, 0);
        }
}

/* 21. a second load on a voice the first one marked, idle by now, leaves no mark standing */
TT_TEST(a_second_patch_load_leaves_no_mark_on_an_idle_voice)
{
    ec_init(1);
    ec_set(FM_PARAM_AMP_RELEASE_MS, 300);
    fm_engine_note_on(&ec_eng, 60, 100);
    ec_run(500);
    fm_engine_controller(&ec_eng, 123, 0);
    fm_engine_patch_loaded(&ec_eng);
    TT_CHECK_EQ(ec_eng.voices[0].new_patch, 1);
    ec_run(4000);
    TT_CHECK_EQ(fm_engine_active_voices(&ec_eng), 0);
    fm_engine_patch_loaded(&ec_eng);
    TT_CHECK_EQ(ec_eng.voices[0].new_patch, 0);
}

static void run_env_carry_tests(void)
{
    TT_RUN(a_key_played_again_after_its_amp_has_ended_carries_the_filter_envelope_on);
    TT_RUN(the_release_runs_on_while_the_voice_is_idle);
    TT_RUN(after_the_release_has_ended_a_note_starts_as_a_voices_first);
    TT_RUN(a_different_key_on_a_played_idle_voice_carries_on);
    TT_RUN(envelope_2_under_the_gate_carries_on);
    TT_RUN(envelope_2_under_the_gate_releases_at_its_rate_while_idle);
    TT_RUN(the_new_notes_velocity_sets_the_depth_of_the_level_carried);
    TT_RUN(an_idle_release_runs_at_the_times_of_the_key_that_played_it);
    TT_RUN(all_sound_off_starts_the_envelopes_from_0);
    TT_RUN(a_unison_note_on_idle_voices_carries_on);
    TT_RUN(an_idle_voices_envelope_is_caught_up_within_a_block_and_then_costs_nothing);
    TT_RUN(a_new_release_time_reaches_an_idle_voices_release);
    TT_RUN(the_level_carried_does_not_depend_on_the_block_size);
    TT_RUN(a_patch_load_starts_an_idle_voices_envelopes_from_0);
    TT_RUN(a_patch_load_lets_a_sounding_voice_release_and_then_start_from_0);
    TT_RUN(a_patch_load_does_not_reach_a_note_struck_while_its_voice_still_sounds);
    TT_RUN(a_carried_pitch_envelope_is_on_the_pitch_from_the_first_sample);
    TT_RUN(a_carried_pitch_envelope_is_on_a_unison_notes_pitch_from_the_first_sample);
    TT_RUN(a_carried_pitch_envelopes_first_sample_has_the_new_velocitys_depth);
    TT_RUN(a_patch_load_starts_an_idle_voices_envelope_2_from_0);
    TT_RUN(a_note_after_a_patch_load_has_no_old_envelope_on_its_pitch);
    TT_RUN(a_second_patch_load_leaves_no_mark_on_an_idle_voice);
}
