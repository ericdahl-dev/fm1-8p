/*
 * The device's headroom (#320): the playable module (firmware/play.c) on its 4 voices at 44.1 kHz, booted on
 * SOUNDTRACK (the default patch): velocity-127 chords held 34 s, past the 32 s filter attack, and everyday
 * velocity-100 chords held 10 s. Measured through play_render, the device's own output path (the engine's
 * int16, then the module's +12 dB, saturated at the Q15 rails).
 */
#include "../firmware/play.h"

static play hr_p;
static int32_t hr_out[2 * PLAY_BLOCK];

/* The engine's soft-clip knee in its int16 output: engine.c's OUT_KNEE (12083420, Q8.24 after the makeup)
 * through to_int16's shift of 9. Every sample past it has been bent. */
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
    if (play_init(&hr_p) != 0) return s;
    s.ok = 1;
    for (k = 0; k < count; k++) play_note(&hr_p, notes[k], vel);
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < 2 * PLAY_BLOCK; i++) hr_out[i] = 0;
        play_render(&hr_p, hr_out, PLAY_BLOCK);
        for (i = 0; i < 2 * PLAY_BLOCK; i++) {
            int32_t a = hr_out[i] < 0 ? -hr_out[i] : hr_out[i], e = hr_p.buf[i] < 0 ? -hr_p.buf[i] : hr_p.buf[i];
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

/* One pinned case: its figures, printed, then each inside its band. A knee band of -1 is not checked. */
typedef struct {
    const char *name;
    uint8_t notes[4];
    int count;
    uint32_t seconds, vel;
    double sat_min, sat_max, peak_min, peak_max, knee_min, knee_max;
} hr_pin;

static int hr_check(const hr_pin *p)
{
    hr_stats s = hr_chord(p->notes, p->count, p->seconds, p->vel);
    printf("    SOUNDTRACK %s at %u, held %u s: module peak %+.2f dBFS (pin %+.1f..%+.1f), %.3f %% saturated "
           "(pin %.2f..%.2f); engine %.3f %% past its knee\n", p->name, (unsigned)p->vel, (unsigned)p->seconds,
           s.peak_dbfs, p->peak_min, p->peak_max, s.sat_pct, p->sat_min, p->sat_max, s.knee_pct);
    return s.ok && s.sat_pct >= p->sat_min && s.sat_pct <= p->sat_max && s.peak_dbfs >= p->peak_min &&
           s.peak_dbfs <= p->peak_max && (p->knee_min < 0 || (s.knee_pct >= p->knee_min && s.knee_pct <= p->knee_max));
}

static int hr_check_all(const hr_pin *p, size_t n)
{
    size_t k;
    int ok = 1;
    for (k = 0; k < n; k++)
        ok &= hr_check(&p[k]);
    return ok;
}

/*
 * A PIN, not a goal (#320): the figures on main at PLAY_GAIN_SHIFT 1 (+6 dB), inside a band either way, so a
 * change to the level, the chorus (#319) or aftertouch (#61), up or down, shows as a measured difference and
 * has to move these on purpose. At +6 dB the module still saturates at +-32767 on velocity-127 chords held past
 * the filter attack (1.3 to 5.2 % of their samples) and on a 4-note chord at velocity 100 (0.8 %); a triad
 * barely touches the rails and one note peaks 6.4 dB under them. The engine's own soft clip (past -2.85 dBFS)
 * bends 0.1 to 0.5 % of its samples, whatever the module's gain.
 */
TT_TEST(headroom_soundtrack_velocity_127_chords_pinned)
{
    static const hr_pin c[] = {
        { "C3 E3 G3 C4", { 48, 52, 55, 60 }, 4, 34, 127, 1.0, 1.6, -0.1, 0.1, 0.09, 0.11 },   /* today 1.304 %, peak 0.00, knee 0.100 % */
        { "C2 C3 G5 G6", { 36, 48, 79, 91 }, 4, 34, 127, 2.1, 2.7, -0.1, 0.1, 0.17, 0.21 },   /* today 2.410 %, knee 0.191 % */
        { "C1 C2 C7 C8", { 24, 36, 96, 108 }, 4, 34, 127, 4.85, 5.5, -0.1, 0.1, 0.47, 0.53 }, /* today 5.189 %, knee 0.500 % */
    };
    TT_CHECK(hr_check_all(c, sizeof c / sizeof c[0]));
}

/* The same pin for everyday playing at velocity 100 (#320), chords held 10 s: a 4-note chord saturates
 * 0.8 % of its samples, a triad just reaches the rails, one note peaks 6.4 dB under them. */
TT_TEST(headroom_soundtrack_velocity_100_pinned)
{
    static const hr_pin c[] = {
        { "C4", { 60 }, 1, 10, 100, 0, 0, -6.9, -5.9, -1, -1 },                        /* today 0 %, peak -6.37 dBFS */
        { "C4 E4 G4", { 60, 64, 67 }, 3, 10, 100, 0.0001, 0.05, -0.1, 0.1, -1, -1 },   /* today 0.003 % */
        { "C3 E3 G3 C4", { 48, 52, 55, 60 }, 4, 10, 100, 0.5, 1.1, -0.1, 0.1, -1, -1 }, /* today 0.789 % */
    };
    TT_CHECK(hr_check_all(c, sizeof c / sizeof c[0]));
}

static void run_headroom_tests(void)
{
    TT_RUN(headroom_soundtrack_velocity_127_chords_pinned);
    TT_RUN(headroom_soundtrack_velocity_100_pinned);
}
