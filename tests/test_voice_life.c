/*
 * A voice's lifecycle through its own interface (engine/voice.h, #73): which
 * hold keeps it (free, releasing, pedal, held), which note it plays, the note
 * a steal left waiting on it, and its allocation age. The engine's allocation
 * policy (tests/test_poly.c) reads only these.
 */
#include "../engine/voice.h"

#define VL_RATE 48000u

static fm_env_settings vl_amp, vl_filter;
static fm_voice_mod vl_mod;
static fm_voice vl_voice;

/* An idle voice at 48 kHz: envelopes with a 10 ms release, the velocity
 * setting the level as the engine routes it. */
static fm_voice *vl_reset(void)
{
    memset(&vl_amp, 0, sizeof vl_amp);
    memset(&vl_filter, 0, sizeof vl_filter);
    memset(&vl_mod, 0, sizeof vl_mod);
    vl_amp.sustain = FM_ENV_FULL;
    vl_amp.release_samples = VL_RATE / 100u;
    fm_mod_routes_reset(&vl_mod.routes);
    fm_mod_set_route(&vl_mod.routes, FM_MOD_SRC_VEL, FM_MOD_LEVEL, FM_ENV_FULL);
    fm_voice_reset(&vl_voice, 0x9E3779B9u, fm_filter_rate_cents(VL_RATE), &vl_amp, &vl_filter);
    return &vl_voice;
}

/* 1. A reset voice is free and plays no note. */
TT_TEST(voice_reset_is_free_and_plays_no_note)
{
    fm_voice *v = vl_reset();
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_FREE);
    TT_CHECK(!fm_voice_plays(v, 60));
    TT_CHECK(!fm_voice_plays(v, 0));
}

/* Plays `note` at velocity 100 on `v`, from tick 0. */
static void vl_play(fm_voice *v, uint8_t note)
{
    fm_voice_note_on(v, &vl_mod, note, (int32_t)note << 16, FM_ENV_FULL / 2, 100, 0, VL_RATE, VL_RATE / 200u);
}

/* 2. A note on: its key holds the voice, which plays that note and no other. */
TT_TEST(voice_note_on_is_held_by_its_key)
{
    fm_voice *v = vl_reset();
    vl_play(v, 60);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_HELD);
    TT_CHECK(fm_voice_plays(v, 60));
    TT_CHECK(!fm_voice_plays(v, 61));
    TT_CHECK(fm_voice_key_holds(v, 60));
    TT_CHECK(!fm_voice_key_holds(v, 61));
}

/* 3. The key up releases the voice, or the pedal holds it if down; letting
 * the pedal go releases it. Either way it still plays the note, and the key
 * no longer holds it. */
TT_TEST(voice_key_up_releases_or_the_pedal_holds)
{
    fm_voice *v = vl_reset();
    vl_play(v, 60);
    fm_voice_key_up(v, 0);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_RELEASING);
    TT_CHECK(fm_voice_plays(v, 60));
    TT_CHECK(!fm_voice_key_holds(v, 60));

    v = vl_reset();
    vl_play(v, 60);
    fm_voice_key_up(v, 1);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_PEDAL);
    TT_CHECK(fm_voice_plays(v, 60));
    TT_CHECK(!fm_voice_key_holds(v, 60));
    fm_voice_release(v);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_RELEASING);
}

static int32_t vl_mix[256], vl_osc[256];

/* Renders `frames` samples of `v` (discarded), in chunks of up to 256. */
static void vl_render(fm_voice *v, uint32_t frames)
{
    uint32_t tick = 0;
    while (frames) {
        uint32_t n = frames < 256u ? frames : 256u;
        memset(vl_mix, 0, sizeof vl_mix);
        fm_voice_render_add(v, &vl_mod, vl_mix, vl_osc, n, VL_RATE, tick + 1u, 1u);
        frames -= n;
        tick++;
    }
}

/* 4. A released voice is free once its release has run out, and then plays
 * no note. */
TT_TEST(voice_release_runs_out_to_free)
{
    fm_voice *v = vl_reset();
    vl_play(v, 60);
    vl_render(v, VL_RATE / 100u);
    fm_voice_key_up(v, 0);
    vl_render(v, VL_RATE / 100u);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_RELEASING); /* a 10 ms tau: still sounding */
    vl_render(v, VL_RATE);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_FREE);
    TT_CHECK(!fm_voice_plays(v, 60));
}

/* 5. A note on for another note steals a sounding voice: it fades the old
 * note out, and the new note waits on it with its own peak, velocity and
 * key state (what the engine needs to move it to another voice). */
TT_TEST(voice_steal_fades_the_old_note_and_the_new_one_waits)
{
    fm_voice *v = vl_reset();
    fm_voice_waiting w;
    vl_play(v, 60);
    vl_render(v, VL_RATE / 100u);
    fm_voice_note_on(v, &vl_mod, 64, 64 << 16, FM_ENV_FULL / 3, 90, 0, VL_RATE, VL_RATE / 200u);
    TT_CHECK(fm_voice_fading(v, 60));
    TT_CHECK(!fm_voice_fading(v, 64));
    w = fm_voice_waiting_note(v);
    TT_CHECK_EQ(w.note, 64);
    TT_CHECK_EQ(w.velocity, 90);
    TT_CHECK_EQ(w.peak, FM_ENV_FULL / 3);
    TT_CHECK_EQ(w.key_down, 1);
    TT_CHECK_EQ(w.sustained, 0);
    fm_voice_key_up(v, 1);
    w = fm_voice_waiting_note(v);
    TT_CHECK_EQ(w.key_down, 0);
    TT_CHECK_EQ(w.sustained, 1);
    vl_render(v, VL_RATE / 100u); /* past the 5 ms fade: the new note plays */
    TT_CHECK(!fm_voice_fading(v, 60));
    TT_CHECK(fm_voice_plays(v, 64));
}

/* 6. Let go (a voice-mode switch): a held voice releases and belongs to no
 * note any more, so no note on or bend reaches it; a free voice stays free. */
TT_TEST(voice_let_go_releases_and_plays_no_note)
{
    fm_voice *v = vl_reset();
    vl_play(v, 60);
    fm_voice_let_go(v);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_RELEASING);
    TT_CHECK(!fm_voice_plays(v, 60));
    TT_CHECK(!fm_voice_key_holds(v, 60));

    v = vl_reset();
    vl_play(v, 60);
    fm_voice_key_up(v, 1);
    fm_voice_let_go(v);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_RELEASING);

    v = vl_reset();
    fm_voice_let_go(v);
    TT_CHECK_EQ(fm_voice_holding(v), FM_VOICE_FREE);
}

/* 7. A voice keeps the age the engine gives it (its event count at the last
 * note on or off), through notes and releases, until the next one. */
TT_TEST(voice_keeps_its_age)
{
    fm_voice *v = vl_reset();
    fm_voice_age(v, 7);
    TT_CHECK_EQ(fm_voice_stamp(v), 7u);
    vl_play(v, 60);
    fm_voice_key_up(v, 0);
    TT_CHECK_EQ(fm_voice_stamp(v), 7u);
    fm_voice_age(v, 0xFFFFFFFFu);
    TT_CHECK_EQ(fm_voice_stamp(v), 0xFFFFFFFFu);
}

static void run_voice_life_tests(void)
{
    TT_RUN(voice_reset_is_free_and_plays_no_note);
    TT_RUN(voice_note_on_is_held_by_its_key);
    TT_RUN(voice_key_up_releases_or_the_pedal_holds);
    TT_RUN(voice_release_runs_out_to_free);
    TT_RUN(voice_steal_fades_the_old_note_and_the_new_one_waits);
    TT_RUN(voice_let_go_releases_and_plays_no_note);
    TT_RUN(voice_keeps_its_age);
}
