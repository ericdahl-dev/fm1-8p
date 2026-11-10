/* The playable module's logic (firmware/play.c): keys and MIDI into the engine, run on the desktop. */
#include "../firmware/play.h"

static play pl_a;
static int32_t pl_out[2 * PLAY_BLOCK];

static void pl_render(play *p)
{
    uint32_t i;
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        pl_out[i] = 0;
    play_render(p, pl_out, PLAY_BLOCK);
}

static int pl_sound(void)
{
    uint32_t i;
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        if (pl_out[i] != 0)
            return 1;
    return 0;
}

TT_TEST(play_key_note_sounds_one_voice)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_note(&pl_a, 60, 100);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);
    TT_CHECK(pl_sound());
}

/* note() and midi() run in the main loop and render() in the audio interrupt: the engine takes no
 * locks, so events wait in a queue and the next render applies them. */
TT_TEST(play_note_reaches_the_engine_at_the_next_render)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_note(&pl_a, 60, 100);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);
}

/* MIDI from any source (USB, Bluetooth, the jack) comes through the host's midi(): every channel plays. */
TT_TEST(play_midi_note_on_any_channel_plays)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_midi(&pl_a, 0x90, 60, 100);
    play_midi(&pl_a, 0x9F, 64, 90);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 2);
    TT_CHECK(pl_sound());
}

/* blocks of PLAY_BLOCK frames for about one second */
#define PL_SECOND (PLAY_FS / PLAY_BLOCK + 1u)
/* blocks past the default patch's release, and a second more */
#define PL_RELEASED ((uint32_t)patch_value(&PATCHES[PATCH_SOUNDTRACK], PATCH_AMP_RELEASE_MS) \
                     * PLAY_FS / 1000u \
                     / PLAY_BLOCK + PL_SECOND)

static void pl_render_for(play *p, uint32_t blocks)
{
    while (blocks--)
        pl_render(p);
}

TT_TEST(play_midi_note_off_releases_the_note)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_midi(&pl_a, 0x90, 60, 100);
    play_midi(&pl_a, 0x91, 64, 100);
    pl_render(&pl_a);
    play_midi(&pl_a, 0x80, 60, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);     /* 64 still held */
}

TT_TEST(play_midi_sustain_pedal_holds_released_notes)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_midi(&pl_a, 0x90, 60, 100);
    play_midi(&pl_a, 0xB0, 64, 127);
    play_midi(&pl_a, 0x80, 60, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);     /* the pedal holds it */
    play_midi(&pl_a, 0xB0, 64, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
}

/* The same note with and without a full bend up: the pitch, so the sound, differs. */
TT_TEST(play_midi_pitch_bend_moves_the_pitch)
{
    static play bent;
    static int32_t flat[2 * PLAY_BLOCK];
    uint32_t i, differ = 0;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK_EQ(play_init(&bent), 0);
    play_midi(&pl_a, 0x90, 60, 100);
    play_midi(&bent, 0xE3, 0x7F, 0x7F);         /* 14-bit 16383: full up, any channel */
    play_midi(&bent, 0x90, 60, 100);
    pl_render_for(&pl_a, 4);
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        flat[i] = pl_out[i];
    pl_render_for(&bent, 4);
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        differ |= flat[i] != pl_out[i];
    TT_CHECK(differ);
}

TT_TEST(play_holds_play_voices_notes_at_once)
{
    uint32_t k;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    for (k = 0; k < PLAY_VOICES + 1u; k++)
        play_note(&pl_a, 48u + 2u * k, 100);
    pl_render_for(&pl_a, 8);                    /* past the stolen voice's fade */
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), PLAY_VOICES);
}

/* The default patch is the JX-8P's tone 17, SOUNDTRACK (firmware/patches.c): init sets every field
 * the engine has a param for, each one as the table has it (in the engine's range: none clamped). */
TT_TEST(play_init_plays_soundtrack)
{
    uint32_t f;
    const patch *s = &PATCHES[PATCH_SOUNDTRACK];
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK(play_patch(&pl_a) == s);
    TT_CHECK(strcmp(s->name, "SOUNDTRACK") == 0);
    for (f = 0; f < PATCH_FIELD_COUNT; f++)
        if (patch_param((patch_field)f) != PATCH_NONE)
            TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, (uint16_t)patch_param((patch_field)f)),
                        patch_value(s, (patch_field)f));
    TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, FM_PARAM_CHORUS_MODE), FM_CHORUS_SLOW);
}

static int32_t pl_peak(play *p, uint32_t blocks)
{
    int32_t peak = 0;
    uint32_t i;
    while (blocks--) {
        pl_render(p);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t a = pl_out[i] < 0 ? -pl_out[i] : pl_out[i];
            if (a > peak)
                peak = a;
        }
    }
    return peak;
}

/* The FM-1's sensed keys give a velocity: a softer press plays quieter. */
TT_TEST(play_key_velocity_sets_the_level)
{
    static play soft;
    int32_t loud_peak, soft_peak;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK_EQ(play_init(&soft), 0);
    play_note(&pl_a, 60, 127);
    play_note(&soft, 60, 30);
    loud_peak = pl_peak(&pl_a, 20);
    soft_peak = pl_peak(&soft, 20);
    TT_CHECK(soft_peak > 0);
    TT_CHECK(soft_peak * 3 < loud_peak * 2);    /* well under: not just a rounding step */
}

/* The screen shows the last note played, from the keys or MIDI; a note off leaves it. */
TT_TEST(play_remembers_the_last_note_played)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK_EQ(play_last_note(&pl_a), -1);     /* none yet */
    play_note(&pl_a, 60, 100);
    TT_CHECK_EQ(play_last_note(&pl_a), 60);
    play_midi(&pl_a, 0x92, 67, 90);
    TT_CHECK_EQ(play_last_note(&pl_a), 67);
    play_midi(&pl_a, 0x82, 67, 0);
    play_note(&pl_a, 60, 0);
    play_midi(&pl_a, 0x90, 72, 0);              /* velocity 0: a note off too */
    TT_CHECK_EQ(play_last_note(&pl_a), 67);
}

/* A data byte past 127 is not MIDI: the message is ignored, not folded onto another note. */
TT_TEST(play_ignores_data_bytes_past_127)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_note(&pl_a, 200, 100);
    play_midi(&pl_a, 0x90, 60, 128);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
    TT_CHECK_EQ(play_last_note(&pl_a), -1);
}

/* More events between two blocks than the queue holds: some are lost, so a lost note off could hang a
 * note. Instead every note is let go at the next block. */
TT_TEST(play_queue_overflow_never_leaves_a_note_hanging)
{
    uint32_t i;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    for (i = 0; i < PLAY_QUEUE; i++)
        play_midi(&pl_a, 0xB0, 1, 0);           /* fill the queue (CC 1: no effect yet) */
    play_note(&pl_a, 60, 100);                  /* lost */
    pl_render(&pl_a);
    play_note(&pl_a, 62, 100);
    for (i = 0; i < PLAY_QUEUE; i++)
        play_midi(&pl_a, 0xB0, 1, 0);
    play_note(&pl_a, 62, 0);                    /* the note off is lost */
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
    play_note(&pl_a, 64, 100);                  /* and it plays on afterwards */
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);
}

/* The screen's name for a note: MIDI 60 is C4; none is "--". */
TT_TEST(play_note_name_spells_the_note)
{
    char s[PLAY_NOTE_NAME];
    play_note_name(60, s);
    TT_CHECK(strcmp(s, "C4") == 0);
    play_note_name(61, s);
    TT_CHECK(strcmp(s, "C#4") == 0);
    play_note_name(0, s);
    TT_CHECK(strcmp(s, "C-1") == 0);
    play_note_name(127, s);
    TT_CHECK(strcmp(s, "G9") == 0);
    play_note_name(-1, s);
    TT_CHECK(strcmp(s, "--") == 0);
}

/* The overflow's recovery lets the pedal up and centers the bend too: a lost pedal-up or bend
 * center would otherwise hold every later note, or leave it out of tune. */
TT_TEST(play_queue_overflow_lets_the_pedal_up_and_centers_the_bend)
{
    static play ref;
    static int32_t ref_out[2 * PLAY_BLOCK];
    uint32_t i, same = 1;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK_EQ(play_init(&ref), 0);
    play_midi(&pl_a, 0xB0, 64, 127);            /* pedal down */
    play_midi(&pl_a, 0xE0, 0x7F, 0x7F);         /* bend full up */
    pl_render(&pl_a);
    for (i = 0; i < PLAY_QUEUE + 1u; i++)
        play_midi(&pl_a, 0xB0, 1, 0);           /* overflow: the pedal-up and the center are lost */
    pl_render(&pl_a);
    play_note(&pl_a, 60, 100);
    pl_render(&pl_a);
    play_note(&pl_a, 60, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);     /* not held by a stuck pedal */
    /* in tune: the same note as on an engine never bent */
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_midi(&pl_a, 0xE0, 0x7F, 0x7F);
    pl_render(&pl_a);
    for (i = 0; i < PLAY_QUEUE + 1u; i++)
        play_midi(&pl_a, 0xB0, 1, 0);
    pl_render(&pl_a);
    play_note(&pl_a, 60, 100);
    pl_render(&pl_a);
    play_render(&ref, ref_out, PLAY_BLOCK);     /* the same blocks before the note: the chorus agrees */
    play_render(&ref, ref_out, PLAY_BLOCK);
    play_note(&ref, 60, 100);
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        ref_out[i] = 0;
    play_render(&ref, ref_out, PLAY_BLOCK);
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        same &= ref_out[i] == pl_out[i];
    TT_CHECK(same);
}

/* A note the full queue dropped never sounds, so the screen doesn't show it as played. */
TT_TEST(play_last_note_skips_a_note_the_full_queue_dropped)
{
    uint32_t i;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_note(&pl_a, 60, 100);
    for (i = 0; i < PLAY_QUEUE; i++)
        play_midi(&pl_a, 0xB0, 1, 0);
    play_note(&pl_a, 72, 100);                  /* dropped */
    TT_CHECK_EQ(play_last_note(&pl_a), 60);
}

/* The module's output gain: the engine's sound PLAY_GAIN_SHIFT doublings (+12 dB) louder, and
 * past full scale it saturates instead of wrapping. */
TT_TEST(play_output_gain_is_12_db_and_saturates)
{
    uint32_t i, k, louder = 0, clipped = 0;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    /* a loud sound: wide open, full at once, every voice at full velocity */
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_FILTER_CUTOFF_CENTS, FM_FILTER_OPEN_CENTS);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_OSC_MIX, 500);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_AMP_ATTACK_MS, 1);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_AMP_SUSTAIN, 1000);
    for (k = 0; k < PLAY_VOICES; k++)
        play_note(&pl_a, 40u + 7u * k, 127);
    for (k = 0; k < 40; k++) {
        pl_render(&pl_a);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t want = (int32_t)pl_a.buf[i] * (1 << PLAY_GAIN_SHIFT);
            want = want > 32767 ? 32767 : want < -32767 ? -32767 : want;
            TT_CHECK_EQ(pl_out[i], want);
            louder |= pl_out[i] != pl_a.buf[i];
            clipped |= pl_out[i] == 32767 || pl_out[i] == -32767;
        }
    }
    TT_CHECK_EQ(PLAY_GAIN_SHIFT, 2);
    TT_CHECK(louder);
    TT_CHECK(clipped);                          /* the saturation was reached, not skipped */
}

/* The OCT- / OCT+ LEDs say where the octave is: OCT- lit below 0, OCT+ above, both dark at 0. */
TT_TEST(play_octave_leds_follow_the_octave)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_DOWN), 0);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_UP), 0);
    play_octave(&pl_a, -1);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_DOWN), 1);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_UP), 0);
    play_octave(&pl_a, +1);
    play_octave(&pl_a, +1);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_DOWN), 0);
    TT_CHECK_EQ(play_oct_led(&pl_a, PLAY_OCT_UP), 1);
}

/* Key k plays F3 (53) + k, an octave a step; a key let go stops the note it started, even after
 * the octave moved. An unsensed key (velocity 0) plays at 100, as the host's keys do. */
TT_TEST(play_keys_play_from_the_octave)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_key_down(&pl_a, 0, 90);                /* F3 */
    TT_CHECK_EQ(play_last_note(&pl_a), 53);
    play_octave(&pl_a, +1);
    play_key_down(&pl_a, 7, 0);                 /* C5: 53 + 12 + 7 */
    TT_CHECK_EQ(play_last_note(&pl_a), 72);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 2);
    play_octave(&pl_a, -2);                     /* moved while held */
    play_key_up(&pl_a, 7);                     /* let go: stops the C5 it started */
    play_key_up(&pl_a, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
}

static void run_play_tests(void)
{
    TT_RUN(play_keys_play_from_the_octave);
    TT_RUN(play_octave_leds_follow_the_octave);
    TT_RUN(play_output_gain_is_12_db_and_saturates);
    TT_RUN(play_last_note_skips_a_note_the_full_queue_dropped);
    TT_RUN(play_queue_overflow_lets_the_pedal_up_and_centers_the_bend);
    TT_RUN(play_note_name_spells_the_note);
    TT_RUN(play_queue_overflow_never_leaves_a_note_hanging);
    TT_RUN(play_ignores_data_bytes_past_127);
    TT_RUN(play_remembers_the_last_note_played);
    TT_RUN(play_key_velocity_sets_the_level);
    TT_RUN(play_init_plays_soundtrack);
    TT_RUN(play_holds_play_voices_notes_at_once);
    TT_RUN(play_midi_pitch_bend_moves_the_pitch);
    TT_RUN(play_midi_sustain_pedal_holds_released_notes);
    TT_RUN(play_midi_note_off_releases_the_note);
    TT_RUN(play_midi_note_on_any_channel_plays);
    TT_RUN(play_note_reaches_the_engine_at_the_next_render);
    TT_RUN(play_key_note_sounds_one_voice);
}
