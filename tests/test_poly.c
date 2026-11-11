/* Polyphony tests: voices, stealing, sustain and bend (public API only). */
#include "../engine/fm1_8p.h"

#define PRATE 44100u
#define DFT_N 16384u

static fm_engine poly;
static int16_t pbuf[PRATE * 2 * 2]; /* up to 2 s of stereo */

/* Engine with `voices` voices and an organ-like envelope: 2 ms attack,
 * full sustain, and a release of tau 5 ms, so below -80 dB and idle in
 * 9.2 tau = 46 ms (the release is exponential). */
static void init_poly(uint8_t voices)
{
    fm_engine_init(&poly, PRATE, voices);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 2);
    fm_engine_set_param(&poly, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&poly, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 5);
}

/*
 * Level of `note`'s fundamental in DFT_N frames starting at frame `from`.
 * Notes 60..68 are used: a semitone apart (more than the DFT's main lobe)
 * and below the second harmonic of the lowest, so each reads alone.
 */
static double note_amp(const int16_t *stereo, size_t from, int note)
{
    return tone_amp(stereo + 2 * from, DFT_N, 0, note_hz(note), PRATE);
}

/* Level of one note played alone on an engine of `voices` voices. */
static double solo_amp(uint8_t voices, int note)
{
    init_poly(voices);
    fm_engine_note_on(&poly, (uint8_t)note, 100);
    fm_engine_render(&poly, pbuf, 2205 + DFT_N);
    return note_amp(pbuf, 2205, note);
}

TT_TEST(every_note_of_a_full_chord_sounds)
{
    static const uint8_t counts[] = { 4, 6, 8 };
    size_t c;
    int n;
    for (c = 0; c < sizeof counts; c++) {
        double ref = solo_amp(counts[c], 60);
        TT_CHECK(ref > 100);
        init_poly(counts[c]);
        for (n = 0; n < counts[c]; n++)
            fm_engine_note_on(&poly, (uint8_t)(60 + n), 100);
        fm_engine_render(&poly, pbuf, 2205 + DFT_N);
        for (n = 0; n < counts[c]; n++)
            TT_CHECK_NEAR(note_amp(pbuf, 2205, 60 + n) / ref, 1.0, 0.1);
    }
}

/* After the note-on that forces a steal: render, then check which of
 * notes 60..67 sound. `expect` holds 1 (sounds) or 0 (gone) per note. */
static int check_sounding(double ref, const int *expect)
{
    int n;
    fm_engine_render(&poly, pbuf, 2205 + DFT_N);
    for (n = 0; n < 8; n++) {
        double a = note_amp(pbuf, 2205, 60 + n) / ref;
        if (expect[n] ? a < 0.5 : a > 0.05) {
            printf("    note %d: level %.3f of solo, expected %s\n", 60 + n,
                   a, expect[n] ? "sounding" : "silent");
            return 0;
        }
    }
    return 1;
}

TT_TEST(extra_note_steals_the_oldest_held_voice_and_sounds)
{
    static const int first[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
    static const int second[8] = { 0, 0, 1, 1, 1, 1, 1, 1 };
    double ref = solo_amp(6, 60);
    int n;
    init_poly(6);
    for (n = 0; n < 6; n++) {
        fm_engine_note_on(&poly, (uint8_t)(60 + n), 100);
        fm_engine_render(&poly, pbuf, 441);
    }
    fm_engine_note_on(&poly, 66, 100);
    TT_CHECK(check_sounding(ref, first));
    /* the next oldest is now 61, not the voice that just took 66 */
    fm_engine_note_on(&poly, 67, 100);
    TT_CHECK(check_sounding(ref, second));
}

TT_TEST(extra_note_steals_the_oldest_released_voice_before_held_ones)
{
    static const int expect[8] = { 1, 1, 0, 1, 1, 1, 1, 0 };
    double ref = solo_amp(6, 60);
    int n;
    init_poly(6);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 3000);
    for (n = 0; n < 6; n++)
        fm_engine_note_on(&poly, (uint8_t)(60 + n), 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_off(&poly, 62); /* released first: the one to steal */
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_off(&poly, 64); /* still releasing, keeps sounding */
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_on(&poly, 66, 100);
    TT_CHECK(check_sounding(ref, expect));
}

/* Largest sample-to-sample change over frames [from, to), both channels. */
static int max_jump(const int16_t *stereo, size_t from, size_t to)
{
    int jump = 0;
    size_t i;
    for (i = (from + 1) * 2; i < to * 2; i++) {
        int d = stereo[i] - stereo[i - 2];
        if (d < 0) d = -d;
        if (d > jump) jump = d;
    }
    return jump;
}

/*
 * Click threshold, in 16-bit LSB per sample (about -48 dBFS). Notes 0..4
 * (8 to 10 Hz) have periods over 4000 samples, so on a 4-voice engine
 * (voice peak 4096) their saws change by under 2 LSB per sample, and a
 * 5 ms attack or a release adds at most 19 per voice. Every voice here
 * starts at phase 0 and no saw wraps inside the 4000 frames checked.
 * Cutting or restarting a voice anywhere else in its cycle jumps by
 * hundreds or thousands of LSB.
 */
#define CLICK_LSB 128

TT_TEST(no_click_when_a_voice_starts_is_stolen_or_ends)
{
    int n;
    init_poly(4);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 5);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 20);
    for (n = 0; n < 4; n++) { /* starts */
        fm_engine_note_on(&poly, (uint8_t)n, 127);
        fm_engine_render(&poly, pbuf + 2 * 100 * n, 100);
    }
    fm_engine_render(&poly, pbuf + 2 * 400, 1100);
    TT_CHECK(peak_abs(pbuf, 1000, 1500) > 4000);
    fm_engine_note_on(&poly, 4, 127); /* steals note 0's voice */
    fm_engine_render(&poly, pbuf + 2 * 1500, 1500);
    fm_engine_note_off(&poly, 1); /* ends */
    fm_engine_note_off(&poly, 2);
    fm_engine_render(&poly, pbuf + 2 * 3000, 1000);
    printf("    largest jump: %d LSB\n", max_jump(pbuf, 0, 4000));
    TT_CHECK(max_jump(pbuf, 0, 4000) <= CLICK_LSB);
}

/*
 * A stolen voice fades out fast, then its new note starts from silence
 * with its own attack, rather than carrying on at the old note's level.
 */
TT_TEST(stolen_voice_fades_then_new_note_attacks_from_silence)
{
    int full, early;
    init_poly(1);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 200);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, PRATE / 2); /* 60 at full level */
    full = peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2);
    TT_CHECK(full > 10000);
    fm_engine_note_on(&poly, 62, 100); /* steals the only voice */
    fm_engine_render(&poly, pbuf, PRATE / 2);
    /* 20 ms later: about 3 ms of fade, then 17 ms of a 200 ms attack */
    early = peak_abs(pbuf, 882 - 176, 882);
    printf("    20 ms after the steal: %.3f of full\n", (double)early / full);
    TT_CHECK((double)early / full < 0.15);
    TT_CHECK((double)peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2) / full > 0.95);
}

/*
 * A note released while its stolen voice is still fading still sounds:
 * it starts after the fade, holds for as long as it waited (3 ms), then
 * releases, and does not stick. With the pedal down it is held.
 */
TT_TEST(note_released_during_a_steal_fade_still_sounds)
{
    size_t i;
    int loud_after = 0;
    init_poly(1);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_on(&poly, 62, 100); /* steal */
    fm_engine_note_off(&poly, 62);     /* before the 3 ms fade ends */
    fm_engine_render(&poly, pbuf, 441);
    /* after the 132-sample fade: a 2 ms attack, then a 50 ms release */
    TT_CHECK(peak_abs(pbuf, 132, 441) > 5000);
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    for (i = 2 * 2205; i < 2 * (PRATE / 10); i++)
        if (pbuf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);

    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_controller(&poly, 64, 127);
    fm_engine_note_on(&poly, 62, 100); /* steal */
    fm_engine_note_off(&poly, 62);     /* held by the pedal */
    fm_engine_render(&poly, pbuf, PRATE / 2);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 1);
    TT_CHECK(peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2) > 10000);
    fm_engine_controller(&poly, 64, 0);
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
}

/*
 * A note pressed again while a steal is still fading it out takes its
 * voice back (so the same pitch is never on two voices), and the note
 * that was waiting for that voice moves to another one.
 */
TT_TEST(note_pressed_again_during_its_steal_fade_takes_its_voice_back)
{
    static const int expect[8] = { 1, 0, 1, 0, 0, 0, 0, 0 };
    double ref;
    int full, early;
    /* one voice: 60 never drops out, it carries on from where it was */
    init_poly(1);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 200);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, PRATE / 2);
    full = peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2);
    fm_engine_note_on(&poly, 62, 100); /* steals 60's voice */
    fm_engine_render(&poly, pbuf, 22);  /* 0.5 ms into the 3 ms fade */
    fm_engine_note_on(&poly, 60, 100); /* 60 again */
    fm_engine_render(&poly, pbuf, 882);
    early = peak_abs(pbuf, 882 - 176, 882);
    TT_CHECK((double)early / full > 0.7);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 1);

    /* two voices: 62, which was waiting for 60's voice, steals 61's */
    ref = solo_amp(2, 60);
    init_poly(2);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_note_on(&poly, 61, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_on(&poly, 62, 100); /* steals 60's voice (oldest) */
    fm_engine_note_on(&poly, 60, 100);
    TT_CHECK(check_sounding(ref, expect));
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 2);
}

TT_TEST(released_note_goes_silent_and_its_voice_idle)
{
    size_t i;
    int loud_after = 0;
    init_poly(4);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_note_on(&poly, 64, 100);
    fm_engine_render(&poly, pbuf, 441);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 2);
    fm_engine_note_off(&poly, 60);
    fm_engine_render(&poly, pbuf, 441); /* 10 ms of a 50 ms release */
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 2);
    fm_engine_note_off(&poly, 64);
    fm_engine_render(&poly, pbuf, 2205 + 441);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    for (i = 2 * 2205; i < 2 * (2205 + 441); i++)
        if (pbuf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

/* A repeated note reuses its own voice instead of taking another. */
TT_TEST(repeated_note_reuses_its_voice)
{
    init_poly(4);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_on(&poly, 60, 100); /* still held */
    fm_engine_render(&poly, pbuf, 441);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 1);
    fm_engine_note_off(&poly, 60);
    fm_engine_render(&poly, pbuf, 220);
    fm_engine_note_on(&poly, 60, 100); /* while releasing */
    fm_engine_render(&poly, pbuf, 441);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 1);
    TT_CHECK(peak_abs(pbuf, 220, 441) > 3000);
}

TT_TEST(sustain_pedal_holds_released_notes_until_it_comes_up)
{
    static const int held[8] = { 1, 0, 1, 0, 1, 0, 0, 0 };
    static const int after[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
    double ref = solo_amp(4, 60);
    init_poly(4);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_controller(&poly, 64, 127); /* pedal down */
    fm_engine_note_on(&poly, 62, 100);
    fm_engine_note_on(&poly, 64, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_note_off(&poly, 60); /* held before the pedal went down */
    fm_engine_note_off(&poly, 62); /* pressed while it was down */
    /* 64 stays held by its key */
    fm_engine_render(&poly, pbuf, PRATE / 2); /* 10 x the release time */
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 3);
    TT_CHECK(check_sounding(ref, held));
    fm_engine_controller(&poly, 64, 0); /* pedal up */
    TT_CHECK(check_sounding(ref, after));
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 1);
}

/* Pitch, in cents relative to `note`, of what the engine plays now. */
static double cents_from(int note)
{
    double period;
    fm_engine_render(&poly, pbuf, PRATE / 2);
    period = period_from_crossings(pbuf + 2 * 4410, PRATE / 2 - 4410, 0);
    return 1200.0 * log2((PRATE / note_hz(note)) / period);
}

TT_TEST(pitch_bend_shifts_pitch_by_its_range)
{
    init_poly(4);
    TT_CHECK_EQ(fm_engine_get_param(&poly, FM_PARAM_BEND_RANGE), 2);
    fm_engine_note_on(&poly, 69, 100);
    TT_CHECK_NEAR(cents_from(69), 0, 1);
    fm_engine_pitch_bend(&poly, 0); /* full down: -2 semitones */
    TT_CHECK_NEAR(cents_from(69), -200, 1);
    fm_engine_pitch_bend(&poly, 12288); /* halfway up: +1 semitone */
    TT_CHECK_NEAR(cents_from(69), 100, 1);
    fm_engine_pitch_bend(&poly, 16383); /* full up: 8191/8192 of 2 */
    TT_CHECK_NEAR(cents_from(69), 200.0 * 8191 / 8192, 1);
    fm_engine_pitch_bend(&poly, 8192 + 1234); /* between semitones */
    TT_CHECK_NEAR(cents_from(69), 200.0 * 1234 / 8192, 1);
    /* new notes start bent, too */
    fm_engine_note_off(&poly, 69);
    fm_engine_controller(&poly, 120, 0);
    fm_engine_note_on(&poly, 57, 100);
    TT_CHECK_NEAR(cents_from(57), 200.0 * 1234 / 8192, 1);
    /* a wider range, and back to center */
    fm_engine_set_param(&poly, FM_PARAM_BEND_RANGE, 12);
    fm_engine_pitch_bend(&poly, 0);
    TT_CHECK_NEAR(cents_from(57), -1200, 1);
    fm_engine_pitch_bend(&poly, 8192);
    TT_CHECK_NEAR(cents_from(57), 0, 1);
    /* out-of-range values are ignored */
    fm_engine_pitch_bend(&poly, 16384);
    TT_CHECK_NEAR(cents_from(57), 0, 1);
}

/*
 * Every voice on at max velocity, full sustain and no attack, in
 * octaves: all saws start together and wrap together, so their peaks
 * line up, the worst case for the sum. No sample may reach full scale.
 */
TT_TEST(full_chord_at_max_velocity_never_clips)
{
    uint8_t voices;
    for (voices = 1; voices <= FM_MAX_VOICES; voices++) {
        size_t i;
        int clipped = 0, n;
        init_poly(voices);
        fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 0);
        for (n = 0; n < voices; n++)
            fm_engine_note_on(&poly, (uint8_t)(24 + 12 * n), 127);
        fm_engine_render(&poly, pbuf, PRATE);
        for (i = 0; i < 2 * PRATE; i++)
            if (pbuf[i] >= 32767 || pbuf[i] <= -32767) clipped = 1;
        TT_CHECK(!clipped);
        /* headroom left for filter resonance and chorus: -6 dBFS */
        TT_CHECK(peak_abs(pbuf, 0, PRATE) <= 16384);
        TT_CHECK(peak_abs(pbuf, 0, PRATE) > 16384 * 9 / 10);
    }
}

/*
 * Plays a pseudo-random stream of note ons and offs, pedal and bend
 * (fixed seed): `steps` events, `step` frames apart, each gap rendered in
 * calls of at most `block` frames. Returns the frames rendered.
 */
static size_t play_random(int16_t *out, uint32_t steps, uint32_t step,
                          uint32_t block)
{
    uint32_t seed = 12345, i;
    size_t done = 0;
    for (i = 0; i < steps; i++) {
        uint32_t r, left = step;
        seed = seed * 1103515245u + 12345u;
        r = seed >> 8;
        switch (r % 8) {
        case 0: case 1: case 2:
            fm_engine_note_on(&poly, (uint8_t)(36 + (r >> 4) % 48), 127);
            break;
        case 3: case 4:
            fm_engine_note_off(&poly, (uint8_t)(36 + (r >> 4) % 48));
            break;
        case 5:
            fm_engine_controller(&poly, 64, (uint8_t)((r >> 4) % 128));
            break;
        case 6:
            fm_engine_pitch_bend(&poly, (uint16_t)((r >> 4) % 16384));
            break;
        default:
            break;
        }
        while (left) {
            uint32_t m = left < block ? left : block;
            fm_engine_render(&poly, out + 2 * done, m);
            done += m;
            left -= m;
        }
    }
    return done;
}

TT_TEST(random_playing_stays_bounded_and_ends_silent)
{
    static const uint32_t rates[] = { 8000, 44100, 192000 };
    static const uint8_t counts[] = { 1, 4, 6, 8 };
    size_t r, c, i, n;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (c = 0; c < sizeof counts; c++) {
            int clipped = 0, loud_after = 0;
            TT_CHECK_EQ(fm_engine_init(&poly, rates[r], counts[c]), FM_OK);
            fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 0);
            /* tau 2 ms: idle (-80 dB) in 18.4 ms, inside the 50 ms below */
            fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 2);
            fm_engine_set_param(&poly, FM_PARAM_BEND_RANGE, 24);
            n = play_random(pbuf, 400, 100, 100);
            for (i = 0; i < 2 * n; i++)
                if (pbuf[i] >= 32767 || pbuf[i] <= -32767) clipped = 1;
            TT_CHECK(!clipped);
            TT_CHECK(peak_abs(pbuf, 0, n) <= 16384);
            fm_engine_controller(&poly, 64, 0);
            fm_engine_controller(&poly, 123, 0);
            fm_engine_render(&poly, pbuf, rates[r] / 20); /* 50 ms */
            TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
            fm_engine_render(&poly, pbuf, 256);
            for (i = 0; i < 2 * 256; i++)
                if (pbuf[i] != 0) loud_after = 1;
            TT_CHECK(!loud_after);
        }
    }
}

/* Steals, pedal and bend give the same output whatever the block size. */
TT_TEST(polyphonic_output_does_not_depend_on_block_size)
{
    static int16_t whole[2 * 400 * 64], blocked[2 * 400 * 64];
    static const uint32_t sizes[] = { 1, 7, 256 };
    size_t k;
    init_poly(4);
    play_random(whole, 400, 64, 64);
    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        init_poly(4);
        play_random(blocked, 400, 64, sizes[k]);
        TT_CHECK(memcmp(whole, blocked, sizeof whole) == 0);
    }
}

/* Renders frames [from, to) of `out` (stereo), `block` frames per call. */
static void render_blocks(int16_t *out, uint32_t from, uint32_t to, uint32_t block)
{
    while (from < to) {
        uint32_t n = to - from < block ? to - from : block;
        fm_engine_render(&poly, out + 2 * from, n);
        from += n;
    }
}

/*
 * One voice: a held note, a steal whose key is up before the new note
 * starts (its release queued for after the steal's fade), a release so
 * short it ends inside a chunk, then another note on the same voice,
 * whose oscillators carry on from where the last note left them.
 */
static void play_queued_release(int16_t *out, uint32_t block)
{
    init_poly(1);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 0);
    fm_engine_note_on(&poly, 60, 100);
    render_blocks(out, 0, 1000, block);
    fm_engine_note_on(&poly, 64, 100); /* steals the voice */
    fm_engine_note_off(&poly, 64);     /* before it starts: queued */
    render_blocks(out, 1000, 3000, block);
    fm_engine_note_on(&poly, 67, 100);
    render_blocks(out, 3000, 4096, block);
}

/*
 * A queued release that ends the note inside a chunk sounds the same,
 * and leaves the oscillators where they would be, whatever the block
 * size: the next note on the voice is the same too.
 */
TT_TEST(queued_release_ending_mid_chunk_does_not_depend_on_block_size)
{
    static int16_t whole[2 * 4096], single[2 * 4096];
    play_queued_release(whole, 64);
    play_queued_release(single, 1);
    TT_CHECK(memcmp(whole, single, sizeof whole) == 0);
}

static void run_poly_tests(void)
{
    TT_RUN(every_note_of_a_full_chord_sounds);
    TT_RUN(extra_note_steals_the_oldest_held_voice_and_sounds);
    TT_RUN(extra_note_steals_the_oldest_released_voice_before_held_ones);
    TT_RUN(no_click_when_a_voice_starts_is_stolen_or_ends);
    TT_RUN(stolen_voice_fades_then_new_note_attacks_from_silence);
    TT_RUN(note_released_during_a_steal_fade_still_sounds);
    TT_RUN(note_pressed_again_during_its_steal_fade_takes_its_voice_back);
    TT_RUN(released_note_goes_silent_and_its_voice_idle);
    TT_RUN(repeated_note_reuses_its_voice);
    TT_RUN(sustain_pedal_holds_released_notes_until_it_comes_up);
    TT_RUN(pitch_bend_shifts_pitch_by_its_range);
    TT_RUN(full_chord_at_max_velocity_never_clips);
    TT_RUN(random_playing_stays_bounded_and_ends_silent);
    TT_RUN(polyphonic_output_does_not_depend_on_block_size);
    TT_RUN(queued_release_ending_mid_chunk_does_not_depend_on_block_size);
}
