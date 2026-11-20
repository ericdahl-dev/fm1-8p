/*
 * The device's headroom (#320): the playable module (firmware/play.c) on its 4 voices at 44.1 kHz, booted on
 * SOUNDTRACK (the default patch), chords held past the 32 s filter attack. Measured through play_render, the
 * device's own output path (the engine's int16, then the module's +12 dB, saturated at the Q15 rails).
 */
#include "../firmware/play.h"

static play hr_p;
static int32_t hr_out[2 * PLAY_BLOCK];

typedef struct {
    double peak_dbfs;   /* the largest |sample| at the module's output */
    double sat_pct;     /* the share of samples saturated at +-32767 */
    double knee_pct;    /* the share of the engine's samples past its soft-clip knee (-2.85 dBFS) */
} hr_stats;

static hr_stats hr_chord(const uint8_t *notes, int count, uint32_t seconds, uint32_t vel)
{
    hr_stats s = { -200, 0, 0 };
    uint32_t b, i, blocks = seconds * PLAY_FS / PLAY_BLOCK, n = 0, sat = 0, knee = 0;
    int32_t peak = 0;
    int k;
    if (play_init(&hr_p) != 0) return s;
    for (k = 0; k < count; k++) play_note(&hr_p, notes[k], vel);
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < 2 * PLAY_BLOCK; i++) hr_out[i] = 0;
        play_render(&hr_p, hr_out, PLAY_BLOCK);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t a = hr_out[i] < 0 ? -hr_out[i] : hr_out[i], e = hr_p.buf[i] < 0 ? -hr_p.buf[i] : hr_p.buf[i];
            if (a > peak) peak = a;
            if (a >= 32767) sat++;
            if (e > 23620) knee++;
            n++;
        }
    }
    s.peak_dbfs = peak ? 20 * log10(peak / 32767.0) : -200;
    s.sat_pct = 100.0 * sat / n;
    s.knee_pct = 100.0 * knee / n;
    return s;
}

/*
 * A PIN, not a goal (#320): today's figures on main 248ff96, so a change to the level, the chorus (#319) or
 * aftertouch (#61) shows as a measured difference and has to move these on purpose. Through play_render the
 * module's +12 dB saturates at +-32767 (a hard clip on the FM-1's Q15 output): 17 to 33 % of these chords'
 * samples today. The engine's own soft clip (past -2.85 dBFS) bends 0.1 to 0.5 % of its samples. Whether the
 * level comes down is Eric's call (#320).
 */
TT_TEST(headroom_soundtrack_velocity_127_chords_pinned)
{
    static const struct { uint8_t notes[4]; double sat_max, knee_max; } c[] = {
        { { 48, 52, 55, 60 }, 17.5, 0.11 },  /* today 17.18 %, 0.100 % */
        { { 36, 48, 79, 91 }, 25.2, 0.21 },  /* today 24.86 %, 0.190 % */
        { { 24, 36, 96, 108 }, 33.5, 0.53 }, /* today 33.15 %, 0.496 % */
    };
    size_t k;
    for (k = 0; k < sizeof c / sizeof c[0]; k++) {
        hr_stats s = hr_chord(c[k].notes, 4, 34, 127);
        printf("    SOUNDTRACK %d %d %d %d at 127, held 34 s: module peak %+.2f dBFS, %.3f %% saturated (pin %.1f); "
               "engine %.3f %% past its knee (pin %.2f)\n", c[k].notes[0], c[k].notes[1], c[k].notes[2], c[k].notes[3],
               s.peak_dbfs, s.sat_pct, c[k].sat_max, s.knee_pct, c[k].knee_max);
        TT_CHECK(s.sat_pct <= c[k].sat_max);
        TT_CHECK(s.knee_pct <= c[k].knee_max);
    }
}

/* The same pin for everyday playing at velocity 100 (#320): a 4-note chord saturates 14.7 % of its samples
 * today, a triad 2.0 %, one note none. */
TT_TEST(headroom_soundtrack_velocity_100_pinned)
{
    static const uint8_t one[] = { 60 }, triad[] = { 60, 64, 67 }, four[] = { 48, 52, 55, 60 };
    hr_stats s;
    s = hr_chord(one, 1, 10, 100);
    printf("    SOUNDTRACK C4 at 100: peak %+.2f dBFS, %.3f %% saturated\n", s.peak_dbfs, s.sat_pct);
    TT_CHECK(s.sat_pct == 0);
    s = hr_chord(triad, 3, 10, 100);
    printf("    SOUNDTRACK C4 E4 G4 at 100: %.3f %% saturated (pin 2.2)\n", s.sat_pct);
    TT_CHECK(s.sat_pct <= 2.2);
    s = hr_chord(four, 4, 10, 100);
    printf("    SOUNDTRACK C3 E3 G3 C4 at 100: %.3f %% saturated (pin 15.0)\n", s.sat_pct);
    TT_CHECK(s.sat_pct <= 15.0);
}

static void run_headroom_tests(void)
{
    TT_RUN(headroom_soundtrack_velocity_127_chords_pinned);
    TT_RUN(headroom_soundtrack_velocity_100_pinned);
}
