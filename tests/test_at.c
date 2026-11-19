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

static fm_engine at_ref;
static int16_t at_rbuf[AT_RATE / 1000u * 2];

/* C4 at AT VOL 100 on `e`, held 400 ms (into its sustain) */
static int at_held(fm_engine *e)
{
    uint32_t n;
    if (fm_engine_init(e, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(e, FM_PARAM_AT_VOL_PCT, 100) != FM_OK)
        return -1;
    fm_engine_note_on(e, 60, 100);
    for (n = 0; n < 400; n++) fm_engine_render(e, at_rbuf, AT_RATE / 1000u);
    return 0;
}

/* a pressure step ramps as a one-pole of 16 ms, as PG-8X's (#61): channel pressure 0 -> 127 on a held
 * note; against the same note without it, the gain goes 0.5 -> 1.5, so the output ratio 1 -> 3. No step
 * (the first ms under 1.3), 1/e of the way left at 16 ms (+-1.5 ms), and there by 100 ms (1 %) */
TT_TEST(at_pressure_step_ramps_in_16_ms)
{
    uint32_t ms, i, tau_ms = 0;
    double first = 0.0, last = 0.0;
    TT_CHECK_EQ(at_held(&at_eng), 0);
    TT_CHECK_EQ(at_held(&at_ref), 0);
    fm_engine_channel_pressure(&at_eng, 127);
    for (ms = 1; ms <= 100; ms++) {
        double a = 0.0, r = 0.0, ratio;
        fm_engine_render(&at_eng, at_buf, AT_RATE / 1000u);
        fm_engine_render(&at_ref, at_rbuf, AT_RATE / 1000u);
        for (i = 0; i < 2u * AT_RATE / 1000u; i++) a += abs(at_buf[i]), r += abs(at_rbuf[i]);
        ratio = a / r;
        if (ms == 1) first = ratio;
        if (!tau_ms && (3.0 - ratio) / 2.0 < 1.0 / 2.718281828) tau_ms = ms;
        last = ratio;
    }
    if (first >= 1.3 || tau_ms < 15 || tau_ms > 18 || fabs(last - 3.0) > 0.03)
        printf("    first ms x%.3f, 1/e left at %u ms, at 100 ms x%.4f\n", first, tau_ms, last);
    TT_CHECK(first < 1.3);
    TT_CHECK(tau_ms >= 15 && tau_ms <= 18);
    TT_CHECK(fabs(last - 3.0) <= 0.03);
}

/* the ramp does not depend on the caller's block size: 1-frame and 48-frame blocks render the same */
TT_TEST(at_ramp_does_not_depend_on_block_size)
{
    uint32_t i, k, same = 1;
    TT_CHECK_EQ(at_held(&at_eng), 0);
    TT_CHECK_EQ(at_held(&at_ref), 0);
    fm_engine_channel_pressure(&at_eng, 127);
    fm_engine_channel_pressure(&at_ref, 127);
    for (k = 0; k < 50; k++) {
        fm_engine_render(&at_eng, at_buf, AT_RATE / 1000u);
        for (i = 0; i < AT_RATE / 1000u; i++) fm_engine_render(&at_ref, at_rbuf + 2u * i, 1);
        for (i = 0; i < 2u * AT_RATE / 1000u; i++) same &= at_buf[i] == at_rbuf[i];
    }
    TT_CHECK(same);
}

/* a held C3 through a resonant lowpass at `cutoff` cents, AT BRI `bri`, channel pressure `p` (if any) 100 ms
 * in; the amplitude of its 6th harmonic 400 ms on */
static double at_bright(int32_t cutoff, int32_t bri, int p)
{
    if (fm_engine_init(&at_eng, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_FILTER_RESONANCE, 500) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_AT_BRI_CENTS, bri) != FM_OK)
        return -1.0;
    fm_engine_note_on(&at_eng, 48, 100);
    at_run(100);
    if (p >= 0) fm_engine_channel_pressure(&at_eng, (uint8_t)p);
    at_run(300);
    fm_engine_render(&at_eng, at_buf, AT_RATE / 10);
    return tone_amp(at_buf, AT_RATE / 10, 0, 6.0 * note_hz(48), AT_RATE);
}

/* AT BRI: full pressure adds at_bri_cents to the cutoff, half pressure about half (B5): a 6th harmonic
 * above the cutoff reads as with the cutoff that much higher (2 %) */
TT_TEST(at_bri_raises_the_cutoff_by_its_cents)
{
    double base = at_bright(6000, 0, -1), up = at_bright(7200, 0, -1), half = at_bright(6600, 0, -1);
    double full_at = at_bright(6000, 1200, 127), half_at = at_bright(6000, 1200, 64), none = at_bright(6000, 1200, 0);
    TT_CHECK(up > base * 1.5);
    if (fabs(full_at / up - 1.0) > 0.02 || fabs(half_at / half - 1.0) > 0.02 || fabs(none / base - 1.0) > 0.02)
        printf("    p127 %.4f of +1200, p64 %.4f of +600, p0 %.4f of +0\n", full_at / up, half_at / half, none / base);
    TT_CHECK(fabs(full_at / up - 1.0) <= 0.02);
    TT_CHECK(fabs(half_at / half - 1.0) <= 0.02);
    TT_CHECK(fabs(none / base - 1.0) <= 0.02);
}

/* a held C4 for 600 ms on `e` with `n` params; channel pressure `p` (if any) before the note */
static void at_vib_run(fm_engine *e, const int32_t (*set)[2], uint32_t n, int p, int16_t *out)
{
    uint32_t i;
    if (fm_engine_init(e, AT_RATE, 4) != FM_OK) return;
    (void)fm_engine_set_param(e, FM_PARAM_LFO_RATE_CENTIHZ, 600);
    for (i = 0; i < n; i++) (void)fm_engine_set_param(e, (uint16_t)set[i][0], set[i][1]);
    if (p >= 0) fm_engine_channel_pressure(e, (uint8_t)p);
    fm_engine_note_on(e, 60, 100);
    for (i = 0; i < 600; i++) fm_engine_render(e, out + i * 2u * (AT_RATE / 1000u), AT_RATE / 1000u);
}

static int16_t at_va[600u * 2u * (AT_RATE / 1000u)], at_vb[600u * 2u * (AT_RATE / 1000u)];

/* AT VIB (B6): at full pressure it is the LFO's vibrato at its depth, exactly (the same render as the patch's
 * LFO pitch depth at that many cents); it starts at once whatever the LFO delay (PG-8X 2.0.13, probe 5: its
 * aftertouch vibrato is the same at LFO DELAY 0.5 and 0.7); with no pressure it does nothing */
TT_TEST(at_vib_is_the_lfo_at_its_depth)
{
    static const int32_t lfo[][2] = {{FM_PARAM_LFO_PITCH_CENTS, 300}};
    static const int32_t at[][2] = {{FM_PARAM_AT_VIB_CENTS, 300}, {FM_PARAM_LFO_DELAY_MS, 2000}};
    static const int32_t none[][2] = {{FM_PARAM_LFO_DELAY_MS, 2000}};
    TT_CHECK(sizeof at_va == sizeof at_vb);
    at_vib_run(&at_ref, lfo, 1, -1, at_va);
    at_vib_run(&at_eng, at, 2, 127, at_vb);
    TT_CHECK(memcmp(at_va, at_vb, sizeof at_va) == 0);
    at_vib_run(&at_ref, none, 1, -1, at_va);
    at_vib_run(&at_eng, at, 2, 0, at_vb);
    TT_CHECK(memcmp(at_va, at_vb, sizeof at_va) == 0);
}

/* B1, the acceptance: with every depth at 0, pressure changes nothing, byte for byte: a chord with poly and
 * channel pressure streaming against the same chord without */
TT_TEST(at_depths_zero_is_byte_identical)
{
    uint32_t i, k, same = 1;
    static const uint8_t chord[3] = {48, 60, 67};
    TT_CHECK_EQ(fm_engine_init(&at_eng, AT_RATE, 4), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&at_ref, AT_RATE, 4), FM_OK);
    for (k = 0; k < 3; k++) fm_engine_note_on(&at_eng, chord[k], 100), fm_engine_note_on(&at_ref, chord[k], 100);
    for (i = 0; i < 400; i++) {
        if (i % 10 == 0) fm_engine_poly_pressure(&at_eng, chord[i / 10 % 3], (uint8_t)(i * 7 % 128));
        if (i % 25 == 0) fm_engine_channel_pressure(&at_eng, (uint8_t)(i * 3 % 128));
        if (i == 200) fm_engine_note_off(&at_eng, 60), fm_engine_note_off(&at_ref, 60);
        fm_engine_render(&at_eng, at_buf, AT_RATE / 1000u);
        fm_engine_render(&at_ref, at_rbuf, AT_RATE / 1000u);
        for (k = 0; k < 2u * AT_RATE / 1000u; k++) same &= at_buf[k] == at_rbuf[k];
    }
    TT_CHECK(same);
}

/* B3: per voice, the latest message wins, poly or channel: C4's poly 127 then channel 64 plays at 64; channel
 * 127 then C4's poly 0 plays at 0 (AT VOL 100: gain 0.5 + p/127) */
static void at_send_poly_then_channel(void)
{
    fm_engine_poly_pressure(&at_eng, 60, 127);
    fm_engine_channel_pressure(&at_eng, 64);
}
static void at_send_channel_then_poly(void)
{
    fm_engine_channel_pressure(&at_eng, 127);
    fm_engine_poly_pressure(&at_eng, 60, 0);
}
TT_TEST(at_latest_message_wins_per_voice)
{
    double c4, g4, c4a, g4a, c4b, g4b;
    at_chord(0, &c4, &g4);                      /* both at 0: gain 0.5 */
    at_chord(at_send_poly_then_channel, &c4a, &g4a);
    at_chord(at_send_channel_then_poly, &c4b, &g4b);
    if (fabs(c4a / c4 - 2.0 * (0.5 + 64.0 / 127.0)) > 0.02 || fabs(c4b / c4 - 1.0) > 0.01 ||
        fabs(g4b / g4 - 3.0) > 0.03)
        printf("    poly then channel: C4 x%.4f; channel then poly: C4 x%.4f, G4 x%.4f\n", c4a / c4, c4b / c4,
               g4b / g4);
    TT_CHECK(fabs(c4a / c4 - 2.0 * (0.5 + 64.0 / 127.0)) <= 0.02);
    TT_CHECK(fabs(c4b / c4 - 1.0) <= 0.01);
    TT_CHECK(fabs(g4b / g4 - 3.0) <= 0.03);
}

/* AT VOL's gain leaves the release floor alone (the floor stays on the VCA level, #169): a note at no
 * pressure, AT VOL 100 (6 dB down), frees its voice at the same sample as with AT VOL off */
static uint32_t at_frees_at(int32_t d)
{
    uint32_t n = 0;
    if (fm_engine_init(&at_eng, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_AT_VOL_PCT, d) != FM_OK)
        return 0;
    fm_engine_note_on(&at_eng, 60, 100);
    at_run(400);
    fm_engine_note_off(&at_eng, 60);
    while (fm_engine_active_voices(&at_eng) && n < 10u * AT_RATE) fm_engine_render(&at_eng, at_buf, 1), n++;
    return n;
}
TT_TEST(at_vol_leaves_the_release_floor)
{
    uint32_t off = at_frees_at(0), on = at_frees_at(100);
    TT_CHECK(off > 0 && off < 10u * AT_RATE);
    if (on != off) printf("    frees after %u samples, %u with AT VOL off\n", on, off);
    TT_CHECK_EQ(on, off);
}

/* unison: C4 then E4 held (E4 sounds, on every voice), AT VOL 100; poly pressure `key` 127 100 ms in (none if
 * 0); RMS 400 ms on */
static double at_unison(uint8_t key)
{
    if (fm_engine_init(&at_eng, AT_RATE, 4) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON) != FM_OK ||
        fm_engine_set_param(&at_eng, FM_PARAM_AT_VOL_PCT, 100) != FM_OK)
        return -1.0;
    fm_engine_note_on(&at_eng, 60, 100);
    fm_engine_note_on(&at_eng, 64, 100);
    at_run(100);
    if (key) fm_engine_poly_pressure(&at_eng, key, 127);
    at_run(300);
    return at_run(100);
}

/* unison (cos): the sounding note's poly pressure reaches every voice (x3); a held key that isn't sounding
 * changes nothing */
TT_TEST(at_unison_takes_the_sounding_notes_pressure)
{
    double none = at_unison(0), held = at_unison(60), sounding = at_unison(64);
    if (fabs(held / none - 1.0) > 0.001 || fabs(sounding / none - 3.0) > 0.03)
        printf("    held key x%.4f (want 1), sounding x%.4f (want 3)\n", held / none, sounding / none);
    TT_CHECK(fabs(held / none - 1.0) <= 0.001);
    TT_CHECK(fabs(sounding / none - 3.0) <= 0.03);
}

/* AT BRI that opens the filter all the way (cutoff 13000 + 2360 at full pressure, held at fully open: a wire)
 * keeps the voice off the fast path while it is shifted, as its busy word says (debug builds check it,
 * FM_CHECK_BUSY), and back on it when the pressure is gone */
TT_TEST(at_bri_to_fully_open_keeps_the_busy_word_true)
{
    TT_CHECK(at_bright(13000, 2360, 127) > 0.0);
    TT_CHECK(fm_voice_busy(&at_eng.voices[0]) != 0);
    fm_engine_channel_pressure(&at_eng, 0);
    at_run(300);
    TT_CHECK(at_bright(13000, 2360, 0) > 0.0);
}

static void run_at_tests(void)
{
    TT_RUN(at_vol_scales_the_level_by_channel_pressure);
    TT_RUN(at_poly_pressure_reaches_only_its_key);
    TT_RUN(at_new_note_starts_at_the_channel_pressure);
    TT_RUN(at_pressure_step_ramps_in_16_ms);
    TT_RUN(at_ramp_does_not_depend_on_block_size);
    TT_RUN(at_bri_raises_the_cutoff_by_its_cents);
    TT_RUN(at_vib_is_the_lfo_at_its_depth);
    TT_RUN(at_depths_zero_is_byte_identical);
    TT_RUN(at_latest_message_wins_per_voice);
    TT_RUN(at_vol_leaves_the_release_floor);
    TT_RUN(at_unison_takes_the_sounding_notes_pressure);
    TT_RUN(at_bri_to_fully_open_keeps_the_busy_word_true);
}
