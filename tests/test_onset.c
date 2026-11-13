/*
 * The onset thump (#146), through the public interface. PG-8X 2.0.13
 * gives each of its voices, the first time it plays after the plugin
 * starts, a DC step that follows the voice's amp level, through an
 * overdamped highpass (5.4 Hz, Q 0.42): a pulse that peaks about 2 ms
 * after note on and is gone in about 300 ms. Measured with both
 * oscillators at MIX 0 (the pulse alone): a step of -30.6 dBFS where
 * the init tone (DCO-1 saw, filter open) plays at -23.6 dBFS RMS, so
 * 0.452 of that tone's RMS. Ours plays the same tone at 0.559 of a voice
 * at full level (6 voices: -26.6 dBFS), so the step is 0.252 of a voice.
 */
#include "../engine/fm1_8p.h"

#define ON_RATE 48000u
#define ON_VOICES 6

static fm_engine on_eng;
static int16_t on_buf[ON_RATE * 2 * 2]; /* up to 2 s of stereo */

/* The init tone, both oscillators off, with PG-8X's thump on (the
 * converter turns it on for every JX-8P tone): what plays is the thump
 * alone. */
static void on_silent_tone(void)
{
    TT_CHECK_EQ(fm_engine_init(&on_eng, ON_RATE, ON_VOICES), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&on_eng, FM_PARAM_ONSET_THUMP, 1), FM_OK);
    fm_engine_set_param(&on_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&on_eng, FM_PARAM_OSC2_LEVEL, 0);
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_ATTACK_MS, 1);
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_SUSTAIN, 1000);
}

/*
 * The step in output counts: 0.252 of a voice, at 1/12 of full scale a
 * voice (6 voices: 1/6, and -12 dB of headroom, +6 dB of makeup).
 */
#define ON_STEP (0.252 / 12 * 32768)

TT_TEST(a_voices_first_note_thumps_with_its_oscillators_off)
{
    int peak;
    on_silent_tone();
    fm_engine_note_on(&on_eng, 84, 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 10);
    peak = peak_abs(on_buf, 0, ON_RATE / 10);
    printf("    thump peak %d counts (step %.0f)\n", peak, ON_STEP);
    /* PG-8X's pulse peaks at 0.885 of its step; within 1 dB of that */
    TT_CHECK(peak > ON_STEP * 0.885 * 0.891 && peak < ON_STEP * 0.885 * 1.122);
}

static int16_t on_buf2[ON_RATE * 2 / 2];

TT_TEST(the_thump_does_not_depend_on_block_size)
{
    uint32_t done = 0, n;
    on_silent_tone();
    fm_engine_note_on(&on_eng, 84, 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 2);
    on_silent_tone();
    fm_engine_note_on(&on_eng, 84, 127);
    for (n = 1; done < ON_RATE / 2; n = n % 97 + 13) {  /* odd blocks */
        if (n > ON_RATE / 2 - done) n = ON_RATE / 2 - done;
        fm_engine_render(&on_eng, on_buf2 + 2 * done, n);
        done += n;
    }
    TT_CHECK(memcmp(on_buf, on_buf2, sizeof on_buf2) == 0);
}

/* PG-8X: the note-off window peaks at -66.2 dBFS, the note on at -31.6 */
TT_TEST(note_off_adds_no_thump_of_its_own)
{
    int peak;
    on_silent_tone();
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_RELEASE_MS, 1);
    fm_engine_note_on(&on_eng, 84, 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 2);
    fm_engine_note_off(&on_eng, 84);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 2);
    peak = peak_abs(on_buf, 0, ON_RATE / 2);
    printf("    after note off: peak %d counts\n", peak);
    TT_CHECK(peak < ON_STEP * 0.02);
}

/*
 * The thump goes through the highpass like the voices: PG-8X's peak at
 * HPF positions 2 and 3 is 6.5 and 12.3 dB under position 1's (flat),
 * our highpass steps 2 and 3. Within 2 dB.
 */
TT_TEST(the_highpass_step_shapes_the_thump_like_pg8x)
{
    static const double under_db[4] = { 0, 0, 6.5, 12.3 };
    int flat = 0, step;
    for (step = 0; step < 4; step += step ? 1 : 2) {
        int peak;
        double db;
        on_silent_tone();
        fm_engine_set_param(&on_eng, FM_PARAM_HIGHPASS_STEP, step);
        fm_engine_note_on(&on_eng, 84, 127);
        fm_engine_render(&on_eng, on_buf, ON_RATE / 10);
        peak = peak_abs(on_buf, 0, ON_RATE / 10);
        if (!step) {
            flat = peak;
            continue;
        }
        db = 20 * log10((double)flat / peak);
        printf("    highpass step %d: thump %.1f dB under flat (PG-8X %.1f)\n", step, db, under_db[step]);
        TT_CHECK(fabs(db - under_db[step]) < 2.0);
    }
}

/*
 * Only a voice's first note after init thumps (PG-8X: a voice played
 * before, -83.7 dB; one not yet played, the whole thump; six at once
 * +15.2 dB). Which voice a note takes is our own rule (docs/voices.md),
 * so this plays every voice first.
 */
TT_TEST(each_voice_thumps_once_after_init)
{
    int one, six, again;
    uint8_t k;
    on_silent_tone();
    fm_engine_note_on(&on_eng, 84, 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 10);
    one = peak_abs(on_buf, 0, ON_RATE / 10);
    on_silent_tone();
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_RELEASE_MS, 1);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_on(&on_eng, (uint8_t)(84 + k), 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 2);
    six = peak_abs(on_buf, 0, ON_RATE / 10);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_off(&on_eng, (uint8_t)(84 + k));
    fm_engine_render(&on_eng, on_buf, ON_RATE);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_on(&on_eng, (uint8_t)(60 + k), 127);
    fm_engine_render(&on_eng, on_buf, ON_RATE / 10);
    again = peak_abs(on_buf, 0, ON_RATE / 10);
    printf("    one voice %d, six %d counts (%.1f dB), six again %d\n",
           one, six, 20 * log10((double)six / one), again);
    TT_CHECK(fabs(20 * log10((double)six / one) - 15.2) < 1.0);
    TT_CHECK(again <= 1);
}

/* A first note held until the pulse is over (where the thump rests),
 * then released: its voice does not thump when it plays again, softer
 * (a part followed again would step down to the new level). */
TT_TEST(a_voice_released_after_its_pulse_is_over_does_not_thump_again)
{
    int again;
    uint8_t k;
    uint32_t i;
    on_silent_tone();
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_RELEASE_MS, 1);
    fm_engine_set_param(&on_eng, FM_PARAM_AMP_VELOCITY, 100);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_on(&on_eng, (uint8_t)(84 + k), 127);
    fm_engine_render(&on_eng, on_buf, 2 * ON_RATE);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_off(&on_eng, (uint8_t)(84 + k));
    fm_engine_render(&on_eng, on_buf, 100);
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_on(&on_eng, (uint8_t)(60 + k), 40);
    for (i = 0; i < ON_RATE / 10; i += 32)   /* small blocks, as the firmware renders */
        fm_engine_render(&on_eng, on_buf + 2 * i, 32);
    again = peak_abs(on_buf, 0, ON_RATE / 10);
    printf("    played again after its pulse: %d counts\n", again);
    TT_CHECK(again <= 1);
}

/* The pulse ends: with the oscillators off, a held chord's output is
 * exactly 0 again (no DC left, PG-8X: +6.19e-07 over 0.5..1 s after) */
TT_TEST(the_thump_comes_back_to_exactly_zero)
{
    uint8_t k;
    size_t i, nonzero = 0;
    on_silent_tone();
    for (k = 0; k < ON_VOICES; k++) fm_engine_note_on(&on_eng, (uint8_t)(84 + k), 127);
    fm_engine_render(&on_eng, on_buf, 2 * ON_RATE);
    for (i = 2 * ON_RATE * 3 / 2; i < 2 * 2 * ON_RATE; i++) nonzero += on_buf[i] != 0;
    printf("    nonzero samples 1.5..2 s: %zu (last %d)\n", nonzero, on_buf[2 * 2 * ON_RATE - 1]);
    TT_CHECK(nonzero == 0);
}

static void run_onset_tests(void)
{
    TT_RUN(the_thump_comes_back_to_exactly_zero);
    TT_RUN(a_voice_released_after_its_pulse_is_over_does_not_thump_again);
    TT_RUN(each_voice_thumps_once_after_init);
    TT_RUN(the_highpass_step_shapes_the_thump_like_pg8x);
    TT_RUN(note_off_adds_no_thump_of_its_own);
    TT_RUN(the_thump_does_not_depend_on_block_size);
    TT_RUN(a_voices_first_note_thumps_with_its_oscillators_off);
}
