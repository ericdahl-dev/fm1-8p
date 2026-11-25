/* The playable module's logic (firmware/play.c): keys and MIDI into the engine, run on the desktop. */
#include "../firmware/play.h"
#include <stdlib.h>

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
/* The release tests' amp release, short and known (not the default patch's), and blocks well
 * past it */
#define PL_RELEASE_MS 50
#define PL_RELEASED PL_SECOND

static void pl_render_for(play *p, uint32_t blocks)
{
    while (blocks--)
        pl_render(p);
}

/* play_init with the amp release at PL_RELEASE_MS */
static int pl_init_short_release(play *p)
{
    if (play_init(p) != 0)
        return -1;
    return fm_engine_set_param(&p->engine, FM_PARAM_AMP_RELEASE_MS, PL_RELEASE_MS) == FM_OK ? 0 : -1;
}

TT_TEST(play_midi_note_off_releases_the_note)
{
    TT_CHECK_EQ(pl_init_short_release(&pl_a), 0);
    play_midi(&pl_a, 0x90, 60, 100);
    play_midi(&pl_a, 0x91, 64, 100);
    pl_render(&pl_a);
    play_midi(&pl_a, 0x80, 60, 0);
    pl_render_for(&pl_a, PL_RELEASED);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);     /* 64 still held */
}

TT_TEST(play_midi_sustain_pedal_holds_released_notes)
{
    TT_CHECK_EQ(pl_init_short_release(&pl_a), 0);
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
    pl_render_for(&pl_a, 8);                    /* the steal starts at once */
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
    TT_CHECK_EQ(pl_init_short_release(&pl_a), 0);
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
    TT_CHECK_EQ(pl_init_short_release(&pl_a), 0);
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

/* The module's output gain is the engine's output stage at FM_OUT_SHIFT_DEVICE (#342): play_render
 * adds the engine's samples as they are, and they already reach the stage's clamp at +-32767 (a
 * loud chord at full velocity), never wrapping. */
TT_TEST(play_output_is_the_engines_at_the_device_gain)
{
    uint32_t i, k, clamped = 0;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    /* a loud sound: wide open, full at once, every voice at full velocity */
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_FILTER_CUTOFF_CENTS, FM_FILTER_OPEN_CENTS);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_OSC2_LEVEL, 500);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_AMP_ATTACK_MS, 1);
    (void)fm_engine_set_param(&pl_a.engine, FM_PARAM_AMP_SUSTAIN, 1000);
    for (k = 0; k < PLAY_VOICES; k++)
        play_note(&pl_a, 40u + 7u * k, 127);
    for (k = 0; k < 40; k++) {
        pl_render(&pl_a);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            TT_CHECK_EQ(pl_out[i], pl_a.buf[i]);
            clamped |= pl_a.buf[i] == 32767 || pl_a.buf[i] == -32767;
        }
    }
    TT_CHECK_EQ(FM_OUT_SHIFT_DEVICE, 2);        /* x4 (+12 dB) until #320 picks */
    TT_CHECK(clamped);                          /* the device gain is in the engine's output */
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
    TT_CHECK_EQ(pl_init_short_release(&pl_a), 0);
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

/* Loading a patch (PRESETS) goes the way a knob does: queued, applied by the next render, at the
 * block's start; then every field the engine has a param for is the patch's. */
TT_TEST(play_load_patch_reaches_the_engine_at_the_next_render)
{
    uint32_t f;
    const patch *t = &PATCHES[0];
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_load_patch(&pl_a, 0);
    TT_CHECK(play_patch(&pl_a) == t);
    pl_render(&pl_a);
    for (f = 0; f < PATCH_FIELD_COUNT; f++)
        if (patch_param((patch_field)f) != PATCH_NONE)
            TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, (uint16_t)patch_param((patch_field)f)),
                        patch_value(t, (patch_field)f));
}

/* A loaded patch is the stored one: nothing is edited until a knob moves. */
TT_TEST(play_load_patch_is_not_edited)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_set_param(&pl_a, FM_PARAM_FILTER_RESONANCE, 500);
    TT_CHECK(play_edited(&pl_a));
    play_load_patch(&pl_a, 3);
    TT_CHECK(!play_edited(&pl_a));
}

/* A load sounds the same whatever came before: a param the patch has no field for goes back to the
 * engine's default, so loading SOUNDTRACK after edits gives exactly the boot values. */
TT_TEST(play_load_patch_matches_a_fresh_boot)
{
    static play fresh;
    uint32_t i;
    TT_CHECK_EQ(play_init(&fresh), 0);
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_set_param(&pl_a, FM_PARAM_LFO_PW_PCT, 40);
    play_set_param(&pl_a, FM_PARAM_FILTER_RESONANCE, 900);
    play_load_patch(&pl_a, PATCH_SOUNDTRACK);
    for (i = 0; i < FM_PARAM_COUNT; i++)
        TT_CHECK_EQ(play_get_param(&pl_a, (uint16_t)i), play_get_param(&fresh, (uint16_t)i));
}

/* Notes sounding when a patch loads are let go (a note off each, so they release, not cut) at the
 * same block start as the new params: none is left hanging when its key comes up later. */
TT_TEST(play_load_patch_lets_sounding_notes_go)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_key_down(&pl_a, 5, 100);
    play_key_down(&pl_a, 9, 100);
    pl_render(&pl_a);
    play_load_patch(&pl_a, 0);
    pl_render_for(&pl_a, 2000);                 /* 11.6 s: past any patch's release */
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 0);
}

/* A key played right after a load, before the next render, starts on the new patch: the load's params
 * go in before the block's note events, so a voice mode change can't end the new note. */
TT_TEST(play_load_patch_then_a_key_plays_the_new_patch)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_set_param(&pl_a, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    pl_render(&pl_a);
    play_load_patch(&pl_a, 0);                  /* every factory patch is poly */
    play_key_down(&pl_a, 5, 100);
    pl_render(&pl_a);
    TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, FM_PARAM_VOICE_MODE), FM_VOICE_MODE_POLY);
    TT_CHECK_EQ(fm_engine_active_voices(&pl_a.engine), 1);
}

/* A load is one step for the audio interrupt: a render part way through it (play_load_begin done,
 * play_load_end not yet) applies none of the new values, and the render after applies all of them. */
TT_TEST(play_load_patch_is_one_step_for_the_audio_interrupt)
{
    static play before;
    uint32_t i;
    TT_CHECK_EQ(play_init(&before), 0);
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_set_param(&pl_a, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    pl_render(&pl_a);
    play_load_begin(&pl_a, 0);
    pl_render(&pl_a);                           /* the interrupt, part way through */
    for (i = 0; i < FM_PARAM_COUNT; i++)
        if (i != FM_PARAM_VOICE_MODE)
            TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, (uint16_t)i),
                        fm_engine_get_param(&before.engine, (uint16_t)i));
    TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, FM_PARAM_VOICE_MODE), FM_VOICE_MODE_UNISON);
    play_load_end(&pl_a);
    pl_render(&pl_a);
    for (i = 0; i < FM_PARAM_COUNT; i++)
        TT_CHECK_EQ(fm_engine_get_param(&pl_a.engine, (uint16_t)i), play_get_param(&pl_a, (uint16_t)i));
}

/* A load asks the ISR only for the params whose value changes: each one it sets can retime envelopes,
 * so a click that set them all could overrun the block. Loading the patch playing asks for none. */
TT_TEST(play_load_patch_asks_only_for_the_params_that_change)
{
    uint32_t w;
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_load_patch(&pl_a, PATCH_SOUNDTRACK);
    for (w = 0; w < PLAY_PARAM_WORDS; w++)
        TT_CHECK_EQ(pl_a.asked[w], 0);
    play_set_param(&pl_a, FM_PARAM_FILTER_RESONANCE, 900);
    pl_render(&pl_a);
    play_load_patch(&pl_a, PATCH_SOUNDTRACK);
    TT_CHECK_EQ(pl_a.asked[FM_PARAM_FILTER_RESONANCE >> 5], 1u << (FM_PARAM_FILTER_RESONANCE & 31u));
}

/* #349: the param asks' |= split by the interrupt (handoff_test.h, gap "asked" 1). The main loop has read
 * asked[] with X's bit still on; a block applies X and clears it; the main loop writes X's bit back with
 * Y's, so the next block sets X again, to the same value. That second set must not change the sound:
 * the render is the same, sample for sample, as when the block falls after the |= */
static play pl_b;
static int32_t pl_ra[2 * PLAY_BLOCK], pl_rb[2 * PLAY_BLOCK];
static void pl_into(play *p, int32_t *o)
{
    uint32_t i;
    for (i = 0; i < 2 * PLAY_BLOCK; i++)
        o[i] = 0;
    play_render(p, o, PLAY_BLOCK);
}
static uint32_t pl_gap_hits;
static void pl_gap(const char *where, uint32_t n)
{
    (void)n;
    if (strcmp(where, "asked") == 0 && pl_gap_hits++ == 0)
        pl_into(&pl_a, pl_ra);
}
/* blocks rendered before the sets (past PL_SECOND: the notes let go that many blocks before), and the
 * first block after them that differs (0: none) */
static uint32_t pl_race(uint32_t patch, uint16_t x, uint32_t before, int32_t *worst, int32_t *peak)
{
    const fm_param_info *in = fm_param_info_get(x);
    uint16_t y = (uint16_t)((x ^ 1u) < FM_PARAM_COUNT ? x ^ 1u : x - 1u);   /* the same word of asked[] */
    int32_t v;
    uint32_t k, i, first = 0;
    play *ps[2] = { &pl_a, &pl_b };
    *worst = *peak = 0;
    for (k = 0; k < 2u; k++) {
        (void)play_init(ps[k]);
        play_load_patch(ps[k], patch);
        play_note(ps[k], 48, 110);
        play_note(ps[k], 55, 100);
        play_note(ps[k], 64, 90);
        for (i = 0; i < before; i++) {
            if (i + PL_SECOND == before) {               /* the release */
                play_note(ps[k], 48, 0);
                play_note(ps[k], 55, 0);
                play_note(ps[k], 64, 0);
            }
            pl_into(ps[k], k ? pl_rb : pl_ra);
        }
    }
    v = play_get_param(&pl_a, x) < in->max ? in->max : in->min;
    play_set_param(&pl_b, x, v);                            /* the block after the |= */
    pl_into(&pl_b, pl_rb);
    play_set_param(&pl_b, y, play_get_param(&pl_b, y));
    play_set_param(&pl_a, x, v);                            /* the block inside it */
    pl_gap_hits = 0;
    handoff_gap = pl_gap;
    play_set_param(&pl_a, y, play_get_param(&pl_a, y));
    handoff_gap = 0;
    if (pl_gap_hits != 1u)                                  /* (the block did fall inside the |=) */
        return 0xFFFFFFFFu;
    for (i = 0; i < 2 * PLAY_BLOCK; i++)                   /* (that block: the same either way) */
        if (pl_ra[i] != pl_rb[i] && !first)
            first = 1;
    for (k = 1; k <= PL_SECOND; k++) {
        pl_into(&pl_a, pl_ra);
        pl_into(&pl_b, pl_rb);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t d = pl_ra[i] - pl_rb[i], m = pl_rb[i] < 0 ? -pl_rb[i] : pl_rb[i];
            d = d < 0 ? -d : d;
            if (m > *peak)
                *peak = m;
            if (d > *worst)
                *worst = d;
            if (d && !first)
                first = k + 1u;
        }
    }
    return first;
}
/* 1 for a patch whose oscillators or voices play in a mode of their own (sync, cross-mod, ring, noise,
 * unison): the cut sweep's patches */
static int pl_patch_has_a_mode(uint32_t t)
{
    (void)play_init(&pl_a);
    play_load_patch(&pl_a, t);
    return play_get_param(&pl_a, FM_PARAM_OSC_SYNC) || play_get_param(&pl_a, FM_PARAM_OSC_XMOD) ||
           play_get_param(&pl_a, FM_PARAM_OSC_RING) || play_get_param(&pl_a, FM_PARAM_OSC1_WAVE) == FM_WAVE_NOISE ||
           play_get_param(&pl_a, FM_PARAM_OSC2_WAVE) == FM_WAVE_NOISE ||
           play_get_param(&pl_a, FM_PARAM_VOICE_MODE) != FM_VOICE_MODE_POLY;
}
/* The full sweep (every patch, three moments) takes about a minute: CI runs it (TT_LONG=1, the build-test
 * jobs). A plain `make test` runs every param on the patches with a mode of their own, at the attack. */
TT_TEST(play_param_set_again_by_the_asks_race_changes_nothing)
{
    static const uint32_t BEFORE[] = { 2, 40, PL_SECOND + 4u };   /* attack, decay; released 4 blocks */
    const char *e = getenv("TT_LONG");
    int full = e && *e && strcmp(e, "0") != 0;
    uint32_t b, n = 0, t, races = 0;
    uint16_t x;
    int32_t worst, peak;
    for (t = 0; t < PATCH_COUNT; t++) {                     /* every factory patch: its envelopes, modes */
        if (!full && !pl_patch_has_a_mode(t))
            continue;
        for (b = 0; b < (full ? sizeof BEFORE / sizeof BEFORE[0] : 1u); b++)
            for (x = 0; x < FM_PARAM_COUNT; x++) {
                races++;
                uint32_t f = pl_race(t, x, BEFORE[b], &worst, &peak);
                if (f) {
                    n++;
                    printf("    asks race: patch %u, %s after %u blocks: differs from block %u, worst %d of peak %d\n",
                           (unsigned)t + 1u, fm_param_info_get(x)->name, (unsigned)BEFORE[b], (unsigned)f,
                           (int)worst, (int)peak);
                }
            }
    }
    printf("    asks race: %u races (%s)\n", (unsigned)races, full ? "TT_LONG: every patch, three moments"
                                                                   : "patches with a mode, the attack");
    TT_CHECK_EQ(n, 0u);
}

/* #349, kind A: the interrupt between an event's entry and the head that publishes it (gap "put" 1)
 * sees no event yet; the next block plays it, once */
static void pl_gap_block(const char *where, uint32_t n)
{
    (void)n;
    if (strcmp(where, "put") == 0 && pl_gap_hits++ == 0)
        pl_into(&pl_a, pl_ra);
}
TT_TEST(play_event_put_with_the_interrupt_inside_plays_once_next_block)
{
    TT_CHECK_EQ(play_init(&pl_a), 0);
    play_load_patch(&pl_a, PATCH_SOUNDTRACK);
    pl_into(&pl_a, pl_ra);
    pl_gap_hits = 0;
    handoff_gap = pl_gap_block;
    play_note(&pl_a, 60, 100);
    handoff_gap = 0;
    TT_CHECK_EQ(pl_gap_hits, 1u);
    TT_CHECK_EQ(pl_a.sounding, 0u);                         /* (that block: not yet) */
    pl_into(&pl_a, pl_ra);
    TT_CHECK_EQ(pl_a.sounding, 1u);                         /* the next: the note */
    pl_into(&pl_a, pl_ra);
    TT_CHECK_EQ(pl_a.sounding, 1u);                         /* and once */
    TT_CHECK_EQ(pl_a.head, pl_a.tail);
}

static void run_play_tests(void)
{
    TT_RUN(play_event_put_with_the_interrupt_inside_plays_once_next_block);
    TT_RUN(play_param_set_again_by_the_asks_race_changes_nothing);
    TT_RUN(play_load_patch_asks_only_for_the_params_that_change);
    TT_RUN(play_load_patch_is_one_step_for_the_audio_interrupt);
    TT_RUN(play_load_patch_then_a_key_plays_the_new_patch);
    TT_RUN(play_load_patch_lets_sounding_notes_go);
    TT_RUN(play_load_patch_matches_a_fresh_boot);
    TT_RUN(play_load_patch_is_not_edited);
    TT_RUN(play_load_patch_reaches_the_engine_at_the_next_render);
    TT_RUN(play_keys_play_from_the_octave);
    TT_RUN(play_octave_leds_follow_the_octave);
    TT_RUN(play_output_is_the_engines_at_the_device_gain);
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
