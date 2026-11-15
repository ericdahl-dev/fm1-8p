/* The output's DC block (engine/dcblock.c): PG-8X's roll-off below a few hertz. */
#include "../engine/dcblock.h"
#include "../engine/fm1_8p.h"

#define DC_RATE 48000u

/* Gain (dB) of the DC block at `hz`: a sine at 0.5 of full through it, settled for 4 s, then
 * measured over a whole number of cycles. */
static double dc_gain_db(double hz)
{
    static fm_dcblock b;
    double si = 0, so = 0;
    uint32_t i, settle = 4 * DC_RATE, n = (uint32_t)(DC_RATE * (hz < 4 ? 8.0 / hz : 2.0));
    fm_dcblock_init(&b, DC_RATE);
    for (i = 0; i < settle + n; i++) {
        double x = 0.5 * sin(2.0 * 3.14159265358979 * hz * i / DC_RATE);
        int32_t xi = (int32_t)(x * 16777216.0);
        int32_t y = fm_dcblock_process(&b, xi);
        if (i >= settle) {
            si += (double)xi * xi;
            so += (double)y * y;
        }
    }
    return 10.0 * log10(so / si);
}

/* PG-8X's HPF 1 below 32 Hz (noise through it, PSD re 200-400 Hz, measured on PG-8X 3.0,
 * 2026-11-16): -32.5 dB at 1 Hz, -17.4 at 2, -7.8 at 4, -3.9 at 8, -0.9 at 16, -0.2 at 32. Two
 * first-order highpasses at 5.5 Hz fit it within 1.24 dB rms; the block keeps to the fit within
 * 0.3 dB, and so within 2.7 dB of PG-8X at every point. */
TT_TEST(dcblock_rolls_off_as_pg8x_below_a_few_hertz)
{
    static const struct { double hz, pg; } pts[] = {
        { 1, -32.5 }, { 2, -17.4 }, { 4, -7.8 }, { 8, -3.9 }, { 16, -0.9 }, { 32, -0.2 }
    };
    size_t k;
    for (k = 0; k < sizeof pts / sizeof pts[0]; k++) {
        double fit = -20.0 * log10(1.0 + (5.5 / pts[k].hz) * (5.5 / pts[k].hz));
        double got = dc_gain_db(pts[k].hz);
        printf("    DC block at %4.0f Hz: %+6.2f dB (fit %+6.2f, PG-8X %+5.1f)\n", pts[k].hz, got, fit,
               pts[k].pg);
        TT_CHECK(fabs(got - fit) < 0.3);
        TT_CHECK(fabs(got - pts[k].pg) < 2.7);
    }
}

/* Above the bass it is a wire: within 0.02 dB from 100 Hz up. */
TT_TEST(dcblock_passes_audio_untouched)
{
    static const double hz[] = { 100, 1000, 10000 };
    size_t k;
    for (k = 0; k < 3; k++) TT_CHECK(fabs(dc_gain_db(hz[k])) < 0.02);
}

/* A constant offset goes to silence: a step to 1.0 is under 1 LSB of the output (2^-15) within
 * 2 s, and the last sample of a long run is exactly 0. */
TT_TEST(dcblock_takes_a_dc_offset_to_silence)
{
    static fm_dcblock b;
    uint32_t i;
    int32_t y = 0, worst_late = 0;
    fm_dcblock_init(&b, DC_RATE);
    for (i = 0; i < 10 * DC_RATE; i++) {
        y = fm_dcblock_process(&b, 1 << 24);
        if (i >= 2 * DC_RATE && (y < 0 ? -y : y) > worst_late) worst_late = y < 0 ? -y : y;
    }
    printf("    DC block, a step to 1.0: after 2 s at most %d Q8.24 units, finally %d\n", worst_late, y);
    TT_CHECK(worst_late < (1 << 9));
    TT_CHECK_EQ(y, 0);
}


/* The engine's output carries no DC (#265 B). A ring at full DCO-2 level (osc1 a square at 0.58,
 * osc2 a square at full a major sixth up, as CHURCHBELL's DCO-2 reaches with MIX ENV) bends one
 * side of its peaks at the ring's +-2.0, which leaves an offset; PG-8X's output has none. Held
 * note, 1 to 3 s: the mean is under half an LSB. */
TT_TEST(engine_output_has_no_dc_from_a_ring)
{
    static fm_engine e;
    static int16_t ob[2 * DC_RATE * 3];
    double sum = 0;
    uint32_t i;
    fm_engine_init(&e, DC_RATE, 6);
    fm_engine_set_param(&e, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&e, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&e, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&e, FM_PARAM_OSC1_WAVE, FM_WAVE_SQUARE);
    fm_engine_set_param(&e, FM_PARAM_OSC2_WAVE, FM_WAVE_SQUARE);
    fm_engine_set_param(&e, FM_PARAM_OSC1_LEVEL, 583);
    fm_engine_set_param(&e, FM_PARAM_OSC2_LEVEL, 1000);
    fm_engine_set_param(&e, FM_PARAM_OSC2_COARSE_SEMIS, 9);
    fm_engine_set_param(&e, FM_PARAM_OSC_RING, 1);
    fm_engine_note_on(&e, 72, 127);
    fm_engine_render(&e, ob, 3 * DC_RATE);
    for (i = DC_RATE; i < 3 * DC_RATE; i++) sum += ob[2 * i];
    printf("    engine output mean, a ring at full DCO-2: %+.3f LSB\n", sum / (2 * DC_RATE));
    TT_CHECK(fabs(sum / (2 * DC_RATE)) < 0.5);
}

static void run_dcblock_tests(void)
{
    TT_RUN(engine_output_has_no_dc_from_a_ring);
    TT_RUN(dcblock_rolls_off_as_pg8x_below_a_few_hertz);
    TT_RUN(dcblock_passes_audio_untouched);
    TT_RUN(dcblock_takes_a_dc_offset_to_silence);
}
