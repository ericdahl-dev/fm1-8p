/* Unison mode tests: every voice on one note, detune, mono note priority
 * (public API only). Shares init_poly, note_amp and the buffers with
 * test_poly.c. */

/* Engine with `voices` voices in unison mode, detune `cents`, and the
 * organ-like envelope of init_poly. */
static void init_unison(uint8_t voices, int32_t cents)
{
    init_poly(voices);
    fm_engine_set_param(&poly, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&poly, FM_PARAM_UNISON_DETUNE_CENTS, cents);
}

/*
 * One note in unison sounds on every voice, and on no more than the
 * engine has: with no detune the voices start in phase, so the note is
 * exactly voice_count times as loud as the same note in poly mode.
 */
TT_TEST(unison_note_uses_every_voice_and_no_more)
{
    static const uint8_t counts[] = { 1, 2, 4, 6, 8 };
    size_t c;
    for (c = 0; c < sizeof counts; c++) {
        double ref = solo_amp(counts[c], 60);
        init_unison(counts[c], 0);
        fm_engine_note_on(&poly, 60, 100);
        TT_CHECK_EQ(fm_engine_active_voices(&poly), counts[c]);
        fm_engine_render(&poly, pbuf, 2205 + DFT_N);
        TT_CHECK_NEAR(note_amp(pbuf, 2205, 60) / ref, counts[c], 0.05);
    }
}

/*
 * Detune spreads the voices evenly over the set number of cents, lowest
 * to highest, centered on the note: voice i of N sits at
 * cents * (i / (N - 1) - 1/2). Note 93 (1760 Hz) keeps even 8 voices over
 * 100 cents (14 cents, 15 Hz apart) outside each other's DFT main lobe.
 * The detune is changed while the note is held, so it applies at once.
 */
TT_TEST(detune_spreads_voice_pitches_by_the_set_amount)
{
    static const uint8_t counts[] = { 2, 4, 8 };
    static const int32_t spreads[] = { 100, 60, 100 };
    size_t c;
    for (c = 0; c < sizeof counts; c++) {
        uint8_t n = counts[c], i;
        double ref = solo_amp(n, 93);
        init_unison(n, 0);
        fm_engine_note_on(&poly, 93, 100);
        fm_engine_render(&poly, pbuf, 2205);
        fm_engine_set_param(&poly, FM_PARAM_UNISON_DETUNE_CENTS, spreads[c]);
        fm_engine_render(&poly, pbuf, 2205 + DFT_N);
        for (i = 0; i < n; i++) {
            double cents = spreads[c] * ((double)i / (n - 1) - 0.5);
            double hz = note_hz(93) * pow(2.0, cents / 1200.0);
            double a = tone_amp(pbuf + 2 * 2205, DFT_N, 0, hz, PRATE) / ref;
            if (a < 0.85 || a > 1.15)
                printf("    %u voices, voice %u at %+.1f cents: %.3f of solo\n",
                       n, i, cents, a);
            TT_CHECK_NEAR(a, 1.0, 0.15);
        }
        /* nothing left at the undetuned pitch between the voices */
        if (n % 2 == 0)
            TT_CHECK(note_amp(pbuf, 2205, 93) / ref < 0.3);
    }
}

static void run_unison_tests(void)
{
    TT_RUN(unison_note_uses_every_voice_and_no_more);
    TT_RUN(detune_spreads_voice_pitches_by_the_set_amount);
}
