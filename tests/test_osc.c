/*
 * Oscillator section tests: two oscillators per voice, each with saw,
 * pulse, square and noise, octave and fine tune, and a mix between them.
 * Driven through the parameter table and fm_engine_* calls, the same way
 * the host plays them.
 */
#include "../engine/fm1_8p.h"

/* Sets up a held note on osc1 alone (mix fully to osc1). */
static void osc_init(void)
{
    fm_engine_init(&eng, RATE, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
}

/* Mean period (in samples) of a held note, skipping the first 0.1 s. */
static double held_period(uint8_t note)
{
    fm_engine_note_on(&eng, note, 100);
    fm_engine_render(&eng, buf, RATE);
    return period_from_crossings(buf + 2 * 4410, RATE - 4410, 0);
}

TT_TEST(square_renders_at_note_pitch)
{
    double want = RATE / note_hz(69);
    osc_init();
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_OSC1_WAVE, FM_WAVE_SQUARE),
                FM_OK);
    TT_CHECK_NEAR(held_period(69), want, want * 0.0006); /* 1 cent */
}

static void run_osc_tests(void)
{
    TT_RUN(square_renders_at_note_pitch);
}
