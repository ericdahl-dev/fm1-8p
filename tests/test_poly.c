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

static void run_poly_tests(void)
{
    TT_RUN(every_note_of_a_full_chord_sounds);
}
