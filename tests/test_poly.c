/* Polyphony tests: voices, stealing, sustain and bend (public API only). */
#include "../engine/fm1_8p.h"

#define PRATE 44100u
#define DFT_N 16384u

static fm_engine poly;
static int16_t pbuf[PRATE * 2 * 2]; /* up to 2 s of stereo */

/* Engine with `voices` voices and an organ-like envelope: 2 ms attack,
 * full sustain, 50 ms release. */
static void init_poly(uint8_t voices)
{
    fm_engine_init(&poly, PRATE, voices);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 2);
    fm_engine_set_param(&poly, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&poly, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 50);
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

static void run_poly_tests(void)
{
    TT_RUN(every_note_of_a_full_chord_sounds);
    TT_RUN(extra_note_steals_the_oldest_held_voice_and_sounds);
    TT_RUN(extra_note_steals_the_oldest_released_voice_before_held_ones);
    TT_RUN(no_click_when_a_voice_starts_is_stolen_or_ends);
    TT_RUN(stolen_voice_fades_then_new_note_attacks_from_silence);
}
