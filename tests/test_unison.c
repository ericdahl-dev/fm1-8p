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

/*
 * Mono note rule in unison: last note wins. A new note moves every voice
 * to its pitch and retriggers the envelope from its current level (no
 * steal fade, no drop). Releasing it goes back to the newest note still
 * held, legato: the pitch changes, the envelope carries on. Releasing the
 * last held note releases the voices.
 */
TT_TEST(new_note_retriggers_and_release_returns_legato_to_held_note)
{
    int sustain, dip, rise, after;
    init_unison(4, 0);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 100);
    fm_engine_set_param(&poly, FM_PARAM_AMP_DECAY_MS, 100);
    fm_engine_set_param(&poly, FM_PARAM_AMP_SUSTAIN, 300);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, PRATE / 2);
    sustain = peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2);
    TT_CHECK(sustain > 3000);

    fm_engine_note_on(&poly, 64, 100); /* 60 still held */
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);
    fm_engine_render(&poly, pbuf, PRATE / 2);
    dip = peak_abs(pbuf, 0, 353);    /* first 8 ms: no fade out */
    rise = peak_abs(pbuf, 2646, 3087); /* 60..70 ms: back near full */
    printf("    sustain %d, first 8 ms %d, at 60 ms %d\n", sustain, dip, rise);
    TT_CHECK(dip >= sustain * 95 / 100);
    TT_CHECK(rise > sustain * 2);
    TT_CHECK_NEAR(cents_from(64), 0, 2);

    fm_engine_note_off(&poly, 64); /* back to 60, which is still held */
    fm_engine_render(&poly, pbuf, PRATE / 5);
    after = peak_abs(pbuf, 0, PRATE / 5);
    TT_CHECK(after <= sustain * 105 / 100); /* no retrigger */
    TT_CHECK(after >= sustain * 95 / 100);  /* and no release */
    TT_CHECK_NEAR(cents_from(60), 0, 2);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);

    fm_engine_note_off(&poly, 60); /* the last held note */
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
}

/*
 * Unison note changes are click-free: a start, a retrigger to a new note,
 * a legato return and the final release, on 4 detuned voices. Notes 0..4
 * as in no_click_when_a_voice_starts_is_stolen_or_ends: no saw wraps in
 * the 4000 frames, so any jump over CLICK_LSB is the engine's. The four
 * voices start in phase, so together they reach full scale (16384), four
 * times one poly voice: the attack is 10 ms rather than 5 to keep the
 * ramp itself (16384 / 441 per sample) as gentle as in the poly test.
 */
TT_TEST(unison_note_changes_do_not_click)
{
    init_unison(4, 10);
    fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 10);
    fm_engine_set_param(&poly, FM_PARAM_AMP_DECAY_MS, 50);
    fm_engine_set_param(&poly, FM_PARAM_AMP_SUSTAIN, 500);
    fm_engine_set_param(&poly, FM_PARAM_AMP_RELEASE_MS, 20);
    fm_engine_note_on(&poly, 0, 127);
    fm_engine_render(&poly, pbuf, 1000);
    fm_engine_note_on(&poly, 4, 127); /* retrigger, new pitch */
    fm_engine_render(&poly, pbuf + 2 * 1000, 1000);
    TT_CHECK(peak_abs(pbuf, 1000, 2000) > 4000);
    fm_engine_note_off(&poly, 4); /* legato back to 0 */
    fm_engine_render(&poly, pbuf + 2 * 2000, 1000);
    fm_engine_note_off(&poly, 0); /* release */
    fm_engine_render(&poly, pbuf + 2 * 3000, 1000);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    printf("    largest jump: %d LSB\n", max_jump(pbuf, 0, 4000));
    TT_CHECK(max_jump(pbuf, 0, 4000) <= CLICK_LSB);
}

/*
 * A unison note at full velocity peaks no higher than a full chord: every
 * voice at 1 / voice_count, -6 dB. With no detune all saws line up (the
 * worst case, reaching the -6 dBFS ceiling); with detune they drift in
 * and out of phase but can only reach the same ceiling. A retrigger to
 * another note on the way must not push past it either.
 */
TT_TEST(unison_at_full_velocity_never_clips)
{
    static const int32_t spreads[] = { 0, 7, 100 };
    uint8_t voices;
    size_t d;
    for (voices = 1; voices <= FM_MAX_VOICES; voices++) {
        for (d = 0; d < sizeof spreads / sizeof spreads[0]; d++) {
            size_t i;
            int clipped = 0;
            init_unison(voices, spreads[d]);
            fm_engine_set_param(&poly, FM_PARAM_AMP_ATTACK_MS, 0);
            fm_engine_note_on(&poly, 36, 127);
            fm_engine_render(&poly, pbuf, PRATE / 2);
            fm_engine_note_on(&poly, 48, 127);
            fm_engine_render(&poly, pbuf + PRATE, PRATE / 2);
            for (i = 0; i < 2 * PRATE; i++)
                if (pbuf[i] >= 32767 || pbuf[i] <= -32767) clipped = 1;
            TT_CHECK(!clipped);
            TT_CHECK(peak_abs(pbuf, 0, PRATE) <= 16384);
            if (spreads[d] == 0)
                TT_CHECK(peak_abs(pbuf, 0, PRATE) > 16384 * 9 / 10);
        }
    }
}

/*
 * Switching modes releases every note, and poly mode then allocates as
 * usual: one in-tune voice per note (a bend retunes without bringing
 * back the unison detune). Back in unison, keys from before the switch
 * are forgotten, so releasing a new note cannot fall back to one of them
 * and leave it stuck.
 */
TT_TEST(switching_back_to_poly_restores_normal_allocation)
{
    static const int chord[8] = { 1, 1, 1, 1, 0, 0, 0, 0 };
    double ref = solo_amp(4, 60);
    int n;
    init_unison(4, 100);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, pbuf, 441);
    fm_engine_set_param(&poly, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY);
    fm_engine_render(&poly, pbuf, PRATE / 10); /* 2 x the release */
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    fm_engine_note_off(&poly, 60); /* its key, after the switch */

    for (n = 0; n < 4; n++)
        fm_engine_note_on(&poly, (uint8_t)(60 + n), 100);
    fm_engine_pitch_bend(&poly, 8192); /* center: retunes every voice */
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);
    TT_CHECK(check_sounding(ref, chord));
    fm_engine_note_off(&poly, 61); /* releases 61 alone */
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 3);

    /* back to unison while 60, 62 and 63 are held */
    fm_engine_set_param(&poly, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
    fm_engine_note_on(&poly, 64, 100);
    fm_engine_render(&poly, pbuf, 441);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);
    fm_engine_note_off(&poly, 64); /* nothing from before to go back to */
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
}

/*
 * The sustain pedal holds the unison note once the last key is up, and a
 * new note while it holds retriggers as usual. All notes off and all
 * sound off forget the held keys too, so no later release falls back to
 * a key from before.
 */
TT_TEST(unison_pedal_and_all_notes_off)
{
    static const uint8_t ccs[] = { 123, 120 };
    size_t c;
    init_unison(4, 0);
    fm_engine_controller(&poly, 64, 127);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_note_off(&poly, 60);
    fm_engine_render(&poly, pbuf, PRATE / 2);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);
    TT_CHECK(peak_abs(pbuf, PRATE / 2 - 441, PRATE / 2) > 10000);
    fm_engine_note_on(&poly, 64, 100);
    fm_engine_note_off(&poly, 64);
    fm_engine_render(&poly, pbuf, PRATE / 2);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 4);
    fm_engine_controller(&poly, 64, 0);
    fm_engine_render(&poly, pbuf, PRATE / 10);
    TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);

    for (c = 0; c < sizeof ccs; c++) {
        fm_engine_note_on(&poly, 60, 100);
        fm_engine_render(&poly, pbuf, 441);
        fm_engine_controller(&poly, ccs[c], 0);
        fm_engine_render(&poly, pbuf, PRATE / 10);
        TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
        fm_engine_note_on(&poly, 64, 100);
        fm_engine_note_off(&poly, 64); /* 60's key is forgotten */
        fm_engine_render(&poly, pbuf, PRATE / 10);
        TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);
        fm_engine_note_off(&poly, 60);
    }
}

/*
 * Pseudo-random playing that switches between poly and unison, with
 * pedal, bend and detune changes (fixed seed): after every event at most
 * voice_count voices sound, the output stays under the -6 dBFS ceiling,
 * all notes off ends in silence, and the block size does not matter.
 * Returns the frames rendered into `out`.
 */
static size_t play_random_modes(int16_t *out, uint8_t voices, uint32_t block,
                                int *too_many)
{
    uint32_t seed = 777, i;
    size_t done = 0;
    for (i = 0; i < 400; i++) {
        uint32_t r, left = 100;
        seed = seed * 1103515245u + 12345u;
        r = seed >> 8;
        switch (r % 10) {
        case 0: case 1: case 2:
            fm_engine_note_on(&poly, (uint8_t)(36 + (r >> 4) % 24), 127);
            break;
        case 3: case 4:
            fm_engine_note_off(&poly, (uint8_t)(36 + (r >> 4) % 24));
            break;
        case 5:
            fm_engine_controller(&poly, 64, (uint8_t)((r >> 4) % 128));
            break;
        case 6:
            fm_engine_pitch_bend(&poly, (uint16_t)((r >> 4) % 16384));
            break;
        case 7:
            fm_engine_set_param(&poly, FM_PARAM_UNISON_DETUNE_CENTS,
                                (int32_t)((r >> 4) % 101));
            break;
        case 8:
            if ((r >> 4) % 4 == 0)
                fm_engine_set_param(&poly, FM_PARAM_VOICE_MODE,
                                    (int32_t)((r >> 6) % 2));
            break;
        default:
            break;
        }
        if (fm_engine_active_voices(&poly) > voices) *too_many = 1;
        while (left) {
            uint32_t m = left < block ? left : block;
            fm_engine_render(&poly, out + 2 * done, m);
            done += m;
            left -= m;
        }
    }
    return done;
}

TT_TEST(random_unison_playing_stays_within_its_voices_and_bounds)
{
    static int16_t whole[2 * 400 * 100], blocked[2 * 400 * 100];
    static const uint8_t counts[] = { 1, 4, 6, 8 };
    size_t c, i, n;
    for (c = 0; c < sizeof counts; c++) {
        int too_many = 0, clipped = 0;
        init_unison(counts[c], 20);
        fm_engine_set_param(&poly, FM_PARAM_BEND_RANGE, 24);
        n = play_random_modes(whole, counts[c], 100, &too_many);
        TT_CHECK(!too_many);
        for (i = 0; i < 2 * n; i++)
            if (whole[i] >= 32767 || whole[i] <= -32767) clipped = 1;
        TT_CHECK(!clipped);
        TT_CHECK(peak_abs(whole, 0, n) <= 16384);
        fm_engine_controller(&poly, 64, 0);
        fm_engine_controller(&poly, 123, 0);
        fm_engine_render(&poly, pbuf, PRATE / 10);
        TT_CHECK_EQ(fm_engine_active_voices(&poly), 0);

        init_unison(counts[c], 20);
        fm_engine_set_param(&poly, FM_PARAM_BEND_RANGE, 24);
        play_random_modes(blocked, counts[c], 7, &too_many);
        TT_CHECK(memcmp(whole, blocked, sizeof whole) == 0);
    }
}

/* Renders a note released 10 ms ago for another 100 ms into `out`,
 * after setting `id` to `value` (or nothing, for id FM_PARAM_COUNT). */
static void release_tail(int16_t *out, uint8_t mode, uint16_t id,
                         int32_t value)
{
    init_unison(4, 20);
    fm_engine_set_param(&poly, FM_PARAM_VOICE_MODE, mode);
    fm_engine_note_on(&poly, 60, 100);
    fm_engine_render(&poly, out, 441);
    fm_engine_note_off(&poly, 60);
    fm_engine_render(&poly, out, 441);
    if (id < FM_PARAM_COUNT) fm_engine_set_param(&poly, id, value);
    fm_engine_render(&poly, out, PRATE / 10);
}

/*
 * Setting the voice mode to the value it has, or changing the detune in
 * poly mode, leaves sounding notes alone: a release in progress ends
 * exactly as it would have.
 */
TT_TEST(setting_mode_or_detune_leaves_a_release_alone)
{
    static int16_t ref[2 * PRATE / 10], got[2 * PRATE / 10];
    uint8_t mode;
    for (mode = 0; mode <= 1; mode++) {
        release_tail(ref, mode, FM_PARAM_COUNT, 0);
        release_tail(got, mode, FM_PARAM_VOICE_MODE, mode);
        TT_CHECK(memcmp(ref, got, sizeof ref) == 0);
    }
    release_tail(ref, FM_VOICE_MODE_POLY, FM_PARAM_COUNT, 0);
    release_tail(got, FM_VOICE_MODE_POLY, FM_PARAM_UNISON_DETUNE_CENTS, 90);
    TT_CHECK(memcmp(ref, got, sizeof ref) == 0);
}

static void run_unison_tests(void)
{
    TT_RUN(unison_note_uses_every_voice_and_no_more);
    TT_RUN(detune_spreads_voice_pitches_by_the_set_amount);
    TT_RUN(new_note_retriggers_and_release_returns_legato_to_held_note);
    TT_RUN(unison_note_changes_do_not_click);
    TT_RUN(unison_at_full_velocity_never_clips);
    TT_RUN(switching_back_to_poly_restores_normal_allocation);
    TT_RUN(unison_pedal_and_all_notes_off);
    TT_RUN(random_unison_playing_stays_within_its_voices_and_bounds);
    TT_RUN(setting_mode_or_detune_leaves_a_release_alone);
}
