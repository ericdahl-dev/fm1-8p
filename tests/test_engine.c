/* Engine behavior tests, through the public interface only. */
#include "../engine/fm1_8p.h"

#define RATE 44100u

static fm_engine eng;
static int16_t buf[RATE * 2 * 4]; /* up to 4 s of stereo */

TT_TEST(note_on_plays_saw_at_note_pitch)
{
    double period;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, RATE);
    period = period_from_crossings(buf + 2 * 4800, RATE - 4800, 0);
    /* within 1 cent */
    TT_CHECK_NEAR(period, RATE / note_hz(69), RATE / note_hz(69) * 0.0006);
}

/*
 * Aliasing threshold for a high saw, as documented in docs/fixed-point.md:
 * energy away from the harmonics of C7 (2093 Hz) at 44.1 kHz must be at
 * least ALIAS_LIMIT_DB below the total. On this measurement a naive
 * (non-band-limited) saw gives -12.6 dB and the 2-point polyBLEP saw
 * -27.9 dB, matching a double-precision polyBLEP model.
 */
#define ALIAS_LIMIT_DB (-25.0)

TT_TEST(high_saw_note_has_low_aliasing)
{
    double db;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_note_on(&eng, 96, 100);
    fm_engine_render(&eng, buf, 4800 + 32768);
    db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(96), RATE, 6);
    printf("    aliasing for note 96 at 44.1 kHz: %.1f dB\n", db);
    TT_CHECK(db < ALIAS_LIMIT_DB);
}

/* Envelope level at time `ms`: peak over the preceding 3 ms (> 1 period). */
static double amp_at(const int16_t *stereo, uint32_t rate, double ms)
{
    size_t end = (size_t)(ms * rate / 1000.0);
    return peak_abs(stereo, end - rate * 3 / 1000, end);
}

TT_TEST(amp_envelope_follows_adsr_settings)
{
    double full;
    size_t i, silent_from;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 100), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 200), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 500), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 300), FM_OK);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, RATE);           /* 0..1 s held */
    fm_engine_note_off(&eng, 69);
    fm_engine_render(&eng, buf + 2 * RATE, RATE); /* 1..2 s released */

    full = amp_at(buf, RATE, 101);
    TT_CHECK(full > 10000);
    /* attack: linear 0 -> full over 100 ms */
    TT_CHECK_NEAR(amp_at(buf, RATE, 50) / full, 0.5, 0.04);
    TT_CHECK(amp_at(buf, RATE, 92) / full < 0.95);
    /* decay: full -> 50 % over 200 ms (75 % halfway) */
    TT_CHECK_NEAR(amp_at(buf, RATE, 203) / full, 0.75, 0.03);
    TT_CHECK_NEAR(amp_at(buf, RATE, 303) / full, 0.5, 0.03);
    /* sustain holds */
    TT_CHECK_NEAR(amp_at(buf, RATE, 990) / full, 0.5, 0.03);
    /* release: 50 % -> 0 over 300 ms, then silence */
    TT_CHECK_NEAR(amp_at(buf, RATE, 1153) / full, 0.25, 0.03);
    TT_CHECK(amp_at(buf, RATE, 1290) > 0);
    silent_from = (size_t)RATE * 1300 / 1000;
    for (i = silent_from * 2; i < (size_t)RATE * 4; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

TT_TEST(set_param_clamps_to_range_and_rejects_unknown_ids)
{
    const fm_param_info *info = fm_param_info_get(FM_PARAM_AMP_SUSTAIN);
    uint16_t id;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        TT_CHECK(p != NULL && p->name != NULL);
        TT_CHECK(p->min <= p->def && p->def <= p->max);
        TT_CHECK_EQ(fm_engine_get_param(&eng, id), p->def);
    }
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 5000), FM_OK);
    TT_CHECK_EQ(fm_engine_get_param(&eng, FM_PARAM_AMP_SUSTAIN), info->max);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, -7), FM_OK);
    TT_CHECK_EQ(fm_engine_get_param(&eng, FM_PARAM_AMP_SUSTAIN), info->min);
    TT_CHECK(fm_param_info_get(FM_PARAM_COUNT) == NULL);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_COUNT, 1), FM_ERR_ARG);
}

TT_TEST(init_rejects_bad_arguments)
{
    TT_CHECK_EQ(fm_engine_init(NULL, RATE, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 0, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 7999, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 192001, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 8000, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&eng, 192000, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 0), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, FM_MAX_VOICES + 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, FM_MAX_VOICES), FM_OK);
}

TT_TEST(all_notes_off_controller_releases_the_note)
{
    size_t i;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 10), FM_OK);
    fm_engine_note_on(&eng, 60, 100);
    fm_engine_render(&eng, buf, 4800);
    TT_CHECK(peak_abs(buf, 2400, 4800) > 1000);
    fm_engine_controller(&eng, 123, 0); /* all notes off */
    fm_engine_render(&eng, buf, 4800);
    for (i = 2 * 480; i < 2 * 4800; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

/* Plays a short phrase, rendering in blocks of `block` frames. */
static void render_phrase_in_blocks(int16_t *out, uint32_t frames,
                                    uint32_t block)
{
    uint32_t done = 0;
    fm_engine_init(&eng, RATE, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 3);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 40);
    while (done < frames) {
        uint32_t n = frames - done < block ? frames - done : block;
        /* events land on fixed frames, whatever the block size */
        if (done == 1000) fm_engine_note_on(&eng, 57, 100);
        if (done == 6000) fm_engine_note_off(&eng, 57);
        if (done < 1000 && done + n > 1000) n = 1000 - done;
        if (done < 6000 && done + n > 6000) n = 6000 - done;
        fm_engine_render(&eng, out + 2 * done, n);
        done += n;
    }
}

TT_TEST(block_size_does_not_change_output)
{
    static int16_t whole[2 * 9000], blocked[2 * 9000];
    static const uint32_t sizes[] = { 256, 1, 255, 4096 };
    size_t k;
    render_phrase_in_blocks(whole, 9000, 9000);
    TT_CHECK(peak_abs(whole, 2000, 5000) > 1000);
    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        render_phrase_in_blocks(blocked, 9000, sizes[k]);
        TT_CHECK(memcmp(whole, blocked, sizeof whole) == 0);
    }
}

/*
 * Division by zero does not trap on the FM-1's CPU (it returns garbage),
 * so every divisor must be non-zero by construction. Here the desktop
 * would trap (SIGFPE) on any division by zero, and the output must stay
 * bounded and fall silent after release at every extreme.
 */
TT_TEST(extreme_settings_stay_bounded_and_release)
{
    static const uint32_t rates[] = { 8000, 11025, 44100, 192000 };
    static const uint8_t notes[] = { 0, 1, 60, 126, 127 };
    size_t r, n, i;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (n = 0; n < sizeof notes / sizeof notes[0]; n++) {
            int loud_after = 0;
            TT_CHECK_EQ(fm_engine_init(&eng, rates[r], 1), FM_OK);
            fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
            fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
            fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
            fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
            fm_engine_note_on(&eng, notes[n], 127);
            fm_engine_render(&eng, buf, 2000);
            TT_CHECK(peak_abs(buf, 0, 2000) <= 16384 + 64);
            fm_engine_note_off(&eng, notes[n]);
            fm_engine_render(&eng, buf, 2000);
            for (i = 2; i < 2 * 2000; i++)
                if (buf[i] != 0) loud_after = 1;
            TT_CHECK(!loud_after);
        }
    }
    /* longest times at the highest rate: steps must not round to zero */
    TT_CHECK_EQ(fm_engine_init(&eng, 192000, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 10000);
    fm_engine_note_on(&eng, 60, 100);
    fm_engine_render(&eng, buf, RATE);
    TT_CHECK(peak_abs(buf, RATE - 1000, RATE) > 0);
}

/* JX-8P attacks run to about 25 s (PG-8X 2.0.13's ATTACK 127, docs/patches.md):
 * a 20 s attack is about half way at 10 s, not clamped short. */
TT_TEST(amp_attack_runs_to_25_seconds)
{
    const uint32_t r = 8000; /* 21 s fits buf at this rate */
    double half, full;
    TT_CHECK_EQ(fm_engine_init(&eng, r, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 20000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_note_on(&eng, 60, 127);
    fm_engine_render(&eng, buf, r * 21);
    half = amp_at(buf, r, 10000), full = amp_at(buf, r, 20900);
    TT_CHECK(full > 1000);
    TT_CHECK(half > 0.4 * full && half < 0.6 * full);
}

/* A zeroed engine (static storage before, or after a failed, init) must
 * not divide by its zero sample rate. */
TT_TEST(uninitialized_engine_is_silent_and_does_not_divide_by_zero)
{
    static fm_engine zero;
    size_t i;
    int loud = 0;
    memset(&zero, 0, sizeof zero);
    fm_engine_note_on(&zero, 60, 100);
    fm_engine_set_param(&zero, FM_PARAM_AMP_RELEASE_MS, 100);
    fm_engine_render(&zero, buf, 256);
    for (i = 0; i < 2 * 256; i++)
        if (buf[i] != 0) loud = 1;
    TT_CHECK(!loud);
}

/*
 * Raising sustain while a 0 ms decay is in progress once overflowed a
 * signed comparison (UBSan: sustain + step = 2^31). The level must end
 * up at the new sustain (full), never wrap negative or past full.
 */
TT_TEST(raising_sustain_mid_decay_does_not_overflow)
{
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 0);
    fm_engine_note_on(&eng, 45, 100);
    fm_engine_render(&eng, buf, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_render(&eng, buf, 4410);
    TT_CHECK(peak_abs(buf, 2000, 4410) > 15000);
    TT_CHECK(peak_abs(buf, 0, 4410) <= 16384 + 64);
}

TT_TEST(shortening_release_mid_release_takes_effect)
{
    size_t i;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 10000);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4410);
    fm_engine_note_off(&eng, 57);
    fm_engine_render(&eng, buf, 441);
    TT_CHECK(peak_abs(buf, 0, 441) > 1000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
    fm_engine_render(&eng, buf, 441);
    for (i = 2 * 2; i < 2 * 441; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

/*
 * Raising sustain above the current level mid-decay moves the level
 * there over the decay time instead of jumping (a click).
 */
TT_TEST(raising_sustain_mid_decay_glides_instead_of_jumping)
{
    double before, just_after, settled;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 1000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 0);
    fm_engine_note_on(&eng, 69, 100); /* period < amp_at window */
    fm_engine_render(&eng, buf, RATE / 2); /* level about 0.5 */
    before = amp_at(buf, RATE, 500);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_render(&eng, buf, 2 * RATE);
    just_after = amp_at(buf, RATE, 4);
    settled = amp_at(buf, RATE, 1500);
    TT_CHECK_NEAR(before / settled, 0.5, 0.05);
    TT_CHECK_NEAR(just_after / settled, 0.5, 0.05);
}

static void run_engine_tests(void)
{
    TT_RUN(note_on_plays_saw_at_note_pitch);
    TT_RUN(high_saw_note_has_low_aliasing);
    TT_RUN(amp_envelope_follows_adsr_settings);
    TT_RUN(amp_attack_runs_to_25_seconds);
    TT_RUN(set_param_clamps_to_range_and_rejects_unknown_ids);
    TT_RUN(init_rejects_bad_arguments);
    TT_RUN(all_notes_off_controller_releases_the_note);
    TT_RUN(block_size_does_not_change_output);
    TT_RUN(extreme_settings_stay_bounded_and_release);
    TT_RUN(uninitialized_engine_is_silent_and_does_not_divide_by_zero);
    TT_RUN(raising_sustain_mid_decay_does_not_overflow);
    TT_RUN(shortening_release_mid_release_takes_effect);
    TT_RUN(raising_sustain_mid_decay_glides_instead_of_jumping);
}
