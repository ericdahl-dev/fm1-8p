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

static void run_engine_tests(void)
{
    TT_RUN(note_on_plays_saw_at_note_pitch);
}
