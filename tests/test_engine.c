/* Engine behavior tests, through the public interface only. */
#include "../engine/fm1_8p.h"

#define RATE 48000u

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
 * energy away from the harmonics of C7 (2093 Hz) at 48 kHz must be at
 * least ALIAS_LIMIT_DB below the total. On this measurement a naive
 * (non-band-limited) saw gives -12.9 dB and the 2-point polyBLEP saw
 * -28.5 dB, matching a double-precision polyBLEP model.
 */
#define ALIAS_LIMIT_DB (-25.0)

TT_TEST(high_saw_note_has_low_aliasing)
{
    double db;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_note_on(&eng, 96, 100);
    fm_engine_render(&eng, buf, 4800 + 32768);
    db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(96), RATE, 6);
    printf("    aliasing for note 96 at 48 kHz: %.1f dB\n", db);
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

static void run_engine_tests(void)
{
    TT_RUN(note_on_plays_saw_at_note_pitch);
    TT_RUN(high_saw_note_has_low_aliasing);
    TT_RUN(amp_envelope_follows_adsr_settings);
}
