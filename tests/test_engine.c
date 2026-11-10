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

static void run_engine_tests(void)
{
    TT_RUN(note_on_plays_saw_at_note_pitch);
    TT_RUN(high_saw_note_has_low_aliasing);
}
