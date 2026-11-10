/* LFO module tests (engine/lfo.h): rate per waveform. */
#include "../engine/lfo.h"

/* Counts LFO cycles over `seconds` of ticks at 44.1 kHz for waveform
 * `wave` at `centihz`: one event per cycle, picked per waveform. */
static int lfo_cycles(uint8_t wave, int32_t centihz, int seconds)
{
    fm_lfo l;
    uint32_t ticks = 44100u * (uint32_t)seconds / FM_LFO_TICK_FRAMES, i;
    int32_t prev, cur;
    int count = 0;
    fm_lfo_reset(&l);
    fm_lfo_set_wave(&l, wave);
    fm_lfo_set_rate(&l, centihz, 44100u);
    prev = fm_lfo_tick(&l);
    for (i = 1; i < ticks; i++) {
        cur = fm_lfo_tick(&l);
        switch (wave) {
        case FM_LFO_TRIANGLE: /* upward zero crossing */
            if (prev < 0 && cur >= 0) count++;
            break;
        case FM_LFO_SAW: /* the ramp falls once a cycle */
            if (cur < prev) count++;
            break;
        case FM_LFO_SQUARE: /* rising edge */
            if (prev < 0 && cur > 0) count++;
            break;
        default: /* sample and hold: a new value each cycle */
            if (cur != prev) count++;
            break;
        }
        prev = cur;
    }
    return count;
}

TT_TEST(lfo_rate_matches_the_setting_for_each_waveform)
{
    static const int32_t rates[] = { 5, 50, 500, 1000, 3000 };
    uint8_t w;
    size_t r;
    for (w = 0; w < FM_LFO_WAVE_COUNT; w++) {
        for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
            int seconds = rates[r] < 100 ? 200 : 20;
            double expect = rates[r] / 100.0 * seconds;
            int got = lfo_cycles(w, rates[r], seconds);
            /* within one cycle (edges at the window ends) */
            if (got < expect - 1.01 || got > expect + 1.01) {
                printf("    wave %d at %d cHz: %d cycles, expected %.1f\n",
                       w, rates[r], got, expect);
                TT_CHECK(0);
            }
        }
    }
}

static void run_lfo_tests(void)
{
    TT_RUN(lfo_rate_matches_the_setting_for_each_waveform);
}
