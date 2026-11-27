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

static void run_env_carry_tests(void)
{
    TT_RUN(a_key_played_again_after_its_amp_has_ended_carries_the_filter_envelope_on);
    TT_RUN(the_release_runs_on_while_the_voice_is_idle);
}
