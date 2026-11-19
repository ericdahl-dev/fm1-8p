/*
 * Aftertouch (#61), as PG-8X 2.0.13's (measured 2026-11-18): poly and channel pressure, the latest
 * message winning per voice, smoothed as a one-pole of about 16 ms, to three depths:
 * - at_vib_cents: vibrato at the LFO's rate, added to the patch's, +-at_vib x p/127 cents;
 * - at_bri_cents: cutoff + at_bri x p/127 cents;
 * - at_vol_pct (d): the voice's gain 1 - d/2 + d x p/127 (so at full d an untouched key is 6 dB down).
 * Through the public interface: the rendered output.
 */
#include "../engine/fm1_8p.h"

#define AT_RATE 48000u

static fm_engine at_eng;
static int16_t at_buf[AT_RATE / 10 * 2];

/* render `ms` of audio (1 ms blocks); returns its RMS over both channels */
static double at_run(double ms)
{
    uint32_t n = (uint32_t)(ms * AT_RATE / 1000.0), k, i;
    double s = 0.0, c = 0.0;
    while (n) {
        k = n < AT_RATE / 1000u ? n : AT_RATE / 1000u;
        fm_engine_render(&at_eng, at_buf, k);
        for (i = 0; i < 2u * k; i++) s += (double)at_buf[i] * at_buf[i];
        c += 2.0 * k;
        n -= k;
    }
    return c ? sqrt(s / c) : 0.0;
}

/* a held C4 at sustain (d = at_vol_pct), pressure `p` sent by channel pressure 100 ms in; RMS 400 ms on */
static double at_vol_level(int32_t d, int p)
{
    if (fm_engine_init(&at_eng, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_AT_VOL_PCT, d) != FM_OK)
        return -1.0;
    fm_engine_note_on(&at_eng, 60, 100);
    at_run(100);
    if (p >= 0) fm_engine_channel_pressure(&at_eng, (uint8_t)p);
    at_run(300);
    return at_run(100);
}

/* AT VOL at full: an untouched key x0.5, full pressure x1.5, of the level with AT VOL off (1 %) */
TT_TEST(at_vol_scales_the_level_by_channel_pressure)
{
    double off = at_vol_level(0, -1), lo = at_vol_level(100, 0), hi = at_vol_level(100, 127);
    TT_CHECK(off > 100.0);
    if (fabs(lo / off - 0.5) > 0.005 || fabs(hi / off - 1.5) > 0.015)
        printf("    p0 x%.4f, p127 x%.4f (want 0.5, 1.5)\n", lo / off, hi / off);
    TT_CHECK(fabs(lo / off - 0.5) <= 0.005);
    TT_CHECK(fabs(hi / off - 1.5) <= 0.015);
}

/* C4 and G4 held at AT VOL 100; `send` (if any) 100 ms in; their fundamentals' amplitudes 400 ms on */
static void at_chord(void (*send)(void), double *c4, double *g4)
{
    *c4 = *g4 = -1.0;
    if (fm_engine_init(&at_eng, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_AT_VOL_PCT, 100) != FM_OK)
        return;
    fm_engine_note_on(&at_eng, 60, 100);
    fm_engine_note_on(&at_eng, 67, 100);
    at_run(100);
    if (send) send();
    at_run(300);
    fm_engine_render(&at_eng, at_buf, AT_RATE / 10);
    *c4 = tone_amp(at_buf, AT_RATE / 10, 0, note_hz(60), AT_RATE);
    *g4 = tone_amp(at_buf, AT_RATE / 10, 0, note_hz(67), AT_RATE);
}

static void at_send_poly_c4(void) { fm_engine_poly_pressure(&at_eng, 60, 127); }

/* poly pressure reaches only its key's voice: C4 at full goes x3 (0.5 -> 1.5), G4 stays (1 %) */
TT_TEST(at_poly_pressure_reaches_only_its_key)
{
    double c4, g4, c4p, g4p;
    at_chord(0, &c4, &g4);
    at_chord(at_send_poly_c4, &c4p, &g4p);
    TT_CHECK(c4 > 100.0 && g4 > 100.0);
    if (fabs(c4p / c4 - 3.0) > 0.03 || fabs(g4p / g4 - 1.0) > 0.01)
        printf("    C4 x%.4f (want 3), G4 x%.4f (want 1)\n", c4p / c4, g4p / g4);
    TT_CHECK(fabs(c4p / c4 - 3.0) <= 0.03);
    TT_CHECK(fabs(g4p / g4 - 1.0) <= 0.01);
}

/* a new note starts at the last channel pressure, at once (#61 choice a): one voice, channel 127, then
 * C4's poly pressure to 0 and C4 let go; C4 again plays at full pressure (x1.5), not at its voice's 0 */
TT_TEST(at_new_note_starts_at_the_channel_pressure)
{
    double off, lvl;
    TT_CHECK_EQ(fm_engine_init(&at_eng, AT_RATE, 1), FM_OK);   /* AT VOL off, one voice */
    TT_CHECK_EQ(fm_engine_set_param(&at_eng, FM_PARAM_AMP_LEVEL, 250), FM_OK); /* under the knee at x1.5 */
    fm_engine_note_on(&at_eng, 60, 100);
    at_run(400);
    off = at_run(100);
    TT_CHECK_EQ(fm_engine_init(&at_eng, AT_RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&at_eng, FM_PARAM_AT_VOL_PCT, 100), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&at_eng, FM_PARAM_AMP_RELEASE_MS, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&at_eng, FM_PARAM_AMP_LEVEL, 250), FM_OK);
    fm_engine_channel_pressure(&at_eng, 127);
    fm_engine_note_on(&at_eng, 60, 100);
    at_run(100);
    fm_engine_poly_pressure(&at_eng, 60, 0);
    fm_engine_note_off(&at_eng, 60);
    at_run(100);
    fm_engine_note_on(&at_eng, 60, 100);
    at_run(400);
    lvl = at_run(100) / off;
    if (fabs(lvl - 1.5) > 0.015) printf("    C4 again x%.4f (want 1.5)\n", lvl);
    TT_CHECK(fabs(lvl - 1.5) <= 0.015);
}

static void run_at_tests(void)
{
    TT_RUN(at_vol_scales_the_level_by_channel_pressure);
    TT_RUN(at_poly_pressure_reaches_only_its_key);
    TT_RUN(at_new_note_starts_at_the_channel_pressure);
}
