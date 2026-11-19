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

static void run_at_tests(void)
{
    TT_RUN(at_vol_scales_the_level_by_channel_pressure);
}
