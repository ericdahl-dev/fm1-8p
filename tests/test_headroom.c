/*
 * The device's headroom (#320): the playable module (firmware/play.c) on its 4 voices at 44.1 kHz, booted on
 * SOUNDTRACK (the default patch): velocity-127 chords held 34 s, past the 32 s filter attack, and everyday
 * velocity-100 chords held 10 s. Measured through play_render, the device's own output path (the engine's
 * output stage at FM_OUT_SHIFT_DEVICE, #342: the makeup, then the module's +12 dB, saturated at the Q15
 * rails; at that gain the soft clip never shapes a sample, since its knee is past the clamp, so every
 * saturated sample here is a hard clip). The knee figure comes from a twin at the engine's own level (output shift 0), as the stage's
 * soft clip sees it.
 */
#include "../firmware/play.h"

static play hr_p, hr_q;     /* the device; its twin at the engine's level (shift 0) */
static int32_t hr_out[2 * PLAY_BLOCK], hr_eng[2 * PLAY_BLOCK];

/* The engine's soft-clip knee in its int16 output at shift 0: output.c's OUT_KNEE (12083420, Q8.24 after
 * the makeup) through to_int16's shift of 9. Every sample past it has been bent. */
#define HR_KNEE (12083420 >> 9)

typedef struct {
    int ok;             /* play_init succeeded */
    double peak_dbfs;   /* the largest |sample| at the module's output */
    double sat_pct;     /* the share of samples saturated at +-32767 */
    double knee_pct;    /* the share of the engine's samples past its soft-clip knee (-2.85 dBFS) */
} hr_stats;

static hr_stats hr_chord(const uint8_t *notes, int count, uint32_t seconds, uint32_t vel)
{
    hr_stats s = { 0, -200, 0, 0 };
    uint32_t b, i, blocks = seconds * PLAY_FS / PLAY_BLOCK, n = 0, sat = 0, knee = 0;
    int32_t peak = 0;
    int k;
    if (play_init(&hr_p) != 0 || play_init(&hr_q) != 0 || fm_engine_set_output_shift(&hr_q.engine, 0) != FM_OK)
        return s;
    s.ok = 1;
    for (k = 0; k < count; k++) {
        play_note(&hr_p, notes[k], vel);
        play_note(&hr_q, notes[k], vel);
    }
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < 2 * PLAY_BLOCK; i++) hr_out[i] = hr_eng[i] = 0;
        play_render(&hr_p, hr_out, PLAY_BLOCK);
        play_render(&hr_q, hr_eng, PLAY_BLOCK);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t a = hr_out[i] < 0 ? -hr_out[i] : hr_out[i], e = hr_eng[i] < 0 ? -hr_eng[i] : hr_eng[i];
            if (a > peak) peak = a;
            if (a >= 32767) sat++;
            if (e > HR_KNEE) knee++;
            n++;
        }
    }
    s.peak_dbfs = peak ? 20 * log10(peak / 32767.0) : -200;
    s.sat_pct = 100.0 * sat / n;
    s.knee_pct = 100.0 * knee / n;
    return s;
}

/*
 * A PIN, not a goal (#320): today's figures with PG-8X's start phases (#310, on main 80b8d9b), inside a band either way, so a change to the
 * level, the chorus (#319) or aftertouch (#61), up or down, shows as a measured difference and has to move
 * these on purpose. Through play_render the module's +12 dB saturates at +-32767 (a hard clip on the FM-1's
 * Q15 output): 15 to 29 % of these chords' samples today. The engine's own soft clip (past -2.85 dBFS) bends
 * 0.08 to 0.4 % of its samples. Whether the level comes down is Eric's call (#320).
 */
TT_TEST(headroom_soundtrack_velocity_127_chords_pinned)
{
    static const struct { uint8_t notes[4]; double sat_min, sat_max, knee_min, knee_max; } c[] = {
        { { 48, 52, 55, 60 }, 15.1, 15.9, 0.065, 0.085 }, /* today 15.499 %, 0.076 % */
        { { 36, 48, 79, 91 }, 20.7, 21.4, 0.09, 0.11 },   /* today 21.061 %, 0.100 % */
        { { 24, 36, 96, 108 }, 28.6, 29.4, 0.36, 0.42 }, /* today 28.985 %, 0.387 % */
    };
    size_t k;
    for (k = 0; k < sizeof c / sizeof c[0]; k++) {
        hr_stats s = hr_chord(c[k].notes, 4, 34, 127);
        printf("    SOUNDTRACK %d %d %d %d at 127, held 34 s: module peak %+.2f dBFS, %.3f %% saturated (pin %.1f..%.1f); "
               "engine %.3f %% past its knee (pin %.2f..%.2f)\n", c[k].notes[0], c[k].notes[1], c[k].notes[2],
               c[k].notes[3], s.peak_dbfs, s.sat_pct, c[k].sat_min, c[k].sat_max, s.knee_pct, c[k].knee_min,
               c[k].knee_max);
        TT_CHECK(s.ok);
        TT_CHECK(s.sat_pct >= c[k].sat_min && s.sat_pct <= c[k].sat_max);
        TT_CHECK(s.knee_pct >= c[k].knee_min && s.knee_pct <= c[k].knee_max);
    }
}

/* The same pin for everyday playing at velocity 100 (#320), chords held 10 s: a 4-note chord saturates
 * 14.2 % of its samples today, a triad 1.9 %, one note 0.05 %. */
TT_TEST(headroom_soundtrack_velocity_100_pinned)
{
    static const uint8_t one[] = { 60 }, triad[] = { 60, 64, 67 }, four[] = { 48, 52, 55, 60 };
    hr_stats s;
    s = hr_chord(one, 1, 10, 100);
    printf("    SOUNDTRACK C4 at 100: peak %+.2f dBFS, %.3f %% saturated\n", s.peak_dbfs, s.sat_pct);
    TT_CHECK(s.ok);
    TT_CHECK(s.sat_pct >= 0.03 && s.sat_pct <= 0.08); /* today 0.051 %: it just reaches the rails */
    TT_CHECK(s.peak_dbfs > -1.0);   /* today +0.00 dBFS: on the rails */
    s = hr_chord(triad, 3, 10, 100);
    printf("    SOUNDTRACK C4 E4 G4 at 100: %.3f %% saturated (pin 1.8..2.2)\n", s.sat_pct);
    TT_CHECK(s.ok);
    TT_CHECK(s.sat_pct >= 1.8 && s.sat_pct <= 2.2);     /* today 1.885 % */
    s = hr_chord(four, 4, 10, 100);
    printf("    SOUNDTRACK C3 E3 G3 C4 at 100: %.3f %% saturated (pin 13.9..14.6)\n", s.sat_pct);
    TT_CHECK(s.ok);
    TT_CHECK(s.sat_pct >= 13.9 && s.sat_pct <= 14.6);   /* today 14.212 % */
}

static void run_headroom_tests(void)
{
    TT_RUN(headroom_soundtrack_velocity_127_chords_pinned);
    TT_RUN(headroom_soundtrack_velocity_100_pinned);
}
