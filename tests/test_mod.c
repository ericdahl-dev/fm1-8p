#include "../engine/dyna.h"
/* LFO routing and velocity, through the engine's public interface. */

#define MOD_RATE 44100u

static fm_engine mod_eng;
static int16_t mod_buf[MOD_RATE * 2 * 4]; /* up to 4 s of stereo */

/* Frequency (Hz) of one channel between `from_ms` and `to_ms`. */
static double hz_between(const int16_t *stereo, double from_ms, double to_ms)
{
    size_t from = (size_t)(from_ms * MOD_RATE / 1000.0);
    size_t to = (size_t)(to_ms * MOD_RATE / 1000.0);
    double period = period_from_crossings(stereo + 2 * from, to - from, 0);
    return period > 0 ? MOD_RATE / period : 0;
}

static double cents(double hz, double ref)
{
    return 1200.0 * log2(hz / ref);
}

TT_TEST(lfo_to_pitch_moves_the_pitch_by_the_depth)
{
    double up, down;
    TT_CHECK_EQ(fm_engine_init(&mod_eng, MOD_RATE, 1), FM_OK);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 100); /* 1 Hz */
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE);
    /* the square is high for the first half second, low for the second */
    up = cents(hz_between(mod_buf, 50, 450), note_hz(69));
    down = cents(hz_between(mod_buf, 550, 950), note_hz(69));
    TT_CHECK_NEAR(up, 100, 1);
    TT_CHECK_NEAR(down, -100, 1);
}

/* A short phrase on 3 voices with a steal, a bend and a pedal, rendered
 * into `out` in uneven blocks. Returns the frames rendered. */
static size_t mod_phrase(int16_t *out)
{
    static const uint32_t blocks[] = { 1, 255, 256, 4096, 77 };
    size_t done = 0, b = 0;
    int step;
    for (step = 0; step < 12; step++) {
        uint32_t n = blocks[b++ % 5];
        switch (step) {
        case 0: fm_engine_note_on(&mod_eng, 60, 100); break;
        case 1: fm_engine_note_on(&mod_eng, 64, 80); break;
        case 2: fm_engine_note_on(&mod_eng, 67, 30); break;
        case 3: fm_engine_note_on(&mod_eng, 71, 127); break; /* steal */
        case 4: fm_engine_pitch_bend(&mod_eng, 12000); break;
        case 5: fm_engine_controller(&mod_eng, 64, 127); break;
        case 6: fm_engine_note_off(&mod_eng, 64); break;
        case 7: fm_engine_note_on(&mod_eng, 60, 50); break; /* same note */
        case 8: fm_engine_controller(&mod_eng, 64, 0); break;
        case 9: fm_engine_note_off(&mod_eng, 71); break;
        default: break;
        }
        fm_engine_render(&mod_eng, out + 2 * done, n);
        done += n;
    }
    fm_engine_render(&mod_eng, out + 2 * done, MOD_RATE / 2);
    return done + MOD_RATE / 2;
}

static int16_t mod_ref[MOD_RATE * 2 * 2];

/* Both oscillators sounding: a 30 % pulse and a detuned saw, half and
 * half, so every LFO target has something to change. */
static void mod_osc_setup(void)
{
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, 30);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_FINE_CENTS, 7);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, 500);
}

/*
 * Renders the phrase with every LFO setting at its default into mod_ref,
 * then with `id` at `value` plus a fast square LFO and a delay, into
 * mod_buf. Returns whether the two match byte for byte.
 */
static int same_as_default(uint16_t id, int32_t value, size_t *frames)
{
    size_t n;
    fm_engine_init(&mod_eng, MOD_RATE, 3);
    mod_osc_setup();
    n = mod_phrase(mod_ref);
    fm_engine_init(&mod_eng, MOD_RATE, 3);
    mod_osc_setup();
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 3000);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 20);
    fm_engine_set_param(&mod_eng, id, value);
    *frames = mod_phrase(mod_buf);
    return *frames == n && memcmp(mod_ref, mod_buf, n * 4) == 0;
}

TT_TEST(zero_pitch_depth_leaves_the_output_untouched)
{
    size_t n;
    TT_CHECK(same_as_default(FM_PARAM_LFO_PITCH_CENTS, 0, &n));
    /* and the same phrase with depth does change it */
    TT_CHECK(!same_as_default(FM_PARAM_LFO_PITCH_CENTS, 50, &n));
}

TT_TEST(lfo_delay_holds_modulation_at_zero_then_fades_it_in)
{
    double mag[10];
    int k;
    TT_CHECK_EQ(fm_engine_init(&mod_eng, MOD_RATE, 1), FM_OK);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 500); /* 5 Hz */
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 400);
    /* the LFO runs freely from init; the delay starts at the note */
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 10);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE);
    /* each 100 ms is one half of the square: measure inside it */
    for (k = 0; k < 10; k++)
        mag[k] = fabs(cents(hz_between(mod_buf, k * 100 + 15, k * 100 + 85),
                            note_hz(69)));
    for (k = 0; k < 4; k++) TT_CHECK(mag[k] < 0.5);   /* delay: none */
    for (k = 4; k < 8; k++) {                         /* fade-in */
        double expect = 100.0 * ((k - 4) * 100 + 50) / 400.0;
        TT_CHECK_NEAR(mag[k], expect, 6);
    }
    TT_CHECK_NEAR(mag[8], 100, 1);                    /* full depth */
    TT_CHECK_NEAR(mag[9], 100, 1);
}

/* One note with vibrato, rendered in blocks of `block` frames. */
static void vibrato_in_blocks(int16_t *out, uint32_t block)
{
    uint32_t done = 0;
    fm_engine_init(&mod_eng, MOD_RATE, 2);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 700);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 300);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 100);
    fm_engine_render(&mod_eng, out, 1000); /* a note on mid-tick */
    fm_engine_note_on(&mod_eng, 57, 90);
    while (done < MOD_RATE) {
        uint32_t n = MOD_RATE - done < block ? MOD_RATE - done : block;
        fm_engine_render(&mod_eng, out + 2 * (1000 + done), n);
        done += n;
    }
}

TT_TEST(block_size_does_not_change_lfo_output)
{
    static const uint32_t blocks[] = { 1, 31, 255, 256, 4096 };
    size_t b, bytes = (size_t)(MOD_RATE + 1000) * 4;
    vibrato_in_blocks(mod_ref, 64);
    for (b = 0; b < 5; b++) {
        vibrato_in_blocks(mod_buf, blocks[b]);
        TT_CHECK(memcmp(mod_ref, mod_buf, bytes) == 0);
    }
}

/* Peak of one held note at `velocity` with amp_velocity at `sens`, over
 * 0.2..0.5 s (sustain, 70 % by default). */
static int vel_peak(int32_t sens, uint8_t velocity)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, sens);
    fm_engine_note_on(&mod_eng, 69, velocity);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return peak_abs(mod_buf, MOD_RATE / 5, MOD_RATE / 2);
}

TT_TEST(higher_velocity_gives_louder_output)
{
    int full = vel_peak(100, 127), prev = 0;
    uint8_t vel;
    TT_CHECK(full > 4000); /* -12 dB mix gain */
    /* full velocity is full level whatever the sensitivity */
    TT_CHECK_EQ(vel_peak(0, 127), full);
    TT_CHECK_EQ(vel_peak(50, 127), full);
    /* louder with every step up, following (velocity / 127)^2 */
    for (vel = 8; vel <= 127; vel = (uint8_t)(vel + 17)) {
        int p = vel_peak(100, vel);
        double expect = (vel / 127.0) * (vel / 127.0);
        TT_CHECK(p > prev);
        TT_CHECK_NEAR((double)p / full, expect, 0.01);
        prev = p;
    }
    /* half sensitivity goes halfway to that curve */
    TT_CHECK_NEAR((double)vel_peak(50, 64) / full,
                  1 - 0.5 * (1 - (64 / 127.0) * (64 / 127.0)), 0.01);
    /* off: every velocity plays at full level */
    TT_CHECK_EQ(vel_peak(0, 1), full);
}

/* One voice holds note 2 at velocity 127, retriggers it at `again`
 * after 2500 frames; returns the largest jump over the first 4500 frames
 * and the peak over a full cycle 4.5k frames later in *level. */
static int retrigger_at(uint8_t again, int *level)
{
    int jump;
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, 100);
    /* one voice peaks at 16384, so a 20 ms attack keeps the start under
     * CLICK_LSB too. Note 2 (9 Hz): its saw changes by about 7 LSB per
     * sample and does not wrap inside the 4500 frames checked */
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_ATTACK_MS, 20);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_DECAY_MS, 50);
    fm_engine_note_on(&mod_eng, 2, 127);
    fm_engine_render(&mod_eng, mod_buf, 2500);
    fm_engine_note_on(&mod_eng, 2, again);
    fm_engine_render(&mod_eng, mod_buf + 2 * 2500, 2000);
    jump = max_jump(mod_buf, 0, 4500);
    /* the decay glides there exponentially (tau 50 ms): within 0.1 % of
     * full after 7 tau, so the level is read from 0.4 s on */
    fm_engine_render(&mod_eng, mod_buf, 20000);
    *level = peak_abs(mod_buf, 14500, 20000);
    return jump;
}

TT_TEST(softer_retrigger_glides_down_without_a_click)
{
    int loud, soft, jump;
    retrigger_at(127, &loud);
    jump = retrigger_at(20, &soft); /* same note, much softer */
    printf("    largest jump: %d LSB\n", jump);
    TT_CHECK(jump <= CLICK_LSB);
    TT_CHECK(loud > 4000); /* -12 dB mix gain */
    TT_CHECK_NEAR((double)soft / loud, (20 / 127.0) * (20 / 127.0), 0.002);
}

/* Peak over the last 0.1 s of `ms` more of rendering. */
static int peak_after(uint32_t ms)
{
    uint32_t n = MOD_RATE / 1000 * ms;
    fm_engine_render(&mod_eng, mod_buf, n);
    return peak_abs(mod_buf, n - MOD_RATE / 10, n);
}

TT_TEST(unison_legato_return_keeps_the_last_strike_velocity)
{
    int loud, soft, back;
    fm_engine_init(&mod_eng, MOD_RATE, 2);
    fm_engine_set_param(&mod_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&mod_eng, FM_PARAM_UNISON_DETUNE_CENTS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, 100);
    /* the decay's tau 30 ms: a velocity glide has settled 500 ms on */
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_DECAY_MS, 30);
    fm_engine_note_on(&mod_eng, 60, 127);
    loud = peak_after(500);
    fm_engine_note_on(&mod_eng, 64, 40); /* softer strike */
    soft = peak_after(500);
    fm_engine_note_off(&mod_eng, 64); /* back to 60, legato */
    back = peak_after(500);
    TT_CHECK(soft < loud / 4);
    TT_CHECK_NEAR(back, soft, soft * 0.05);
}

TT_TEST(zero_pulse_width_depth_leaves_the_output_untouched)
{
    size_t n;
    TT_CHECK(same_as_default(FM_PARAM_LFO_PW_PCT, 0, &n));
    TT_CHECK(!same_as_default(FM_PARAM_LFO_PW_PCT, 20, &n));
}

/* Share of samples above zero over [from_ms, to_ms): a pulse's duty. */
static double duty_between(const int16_t *stereo, double from_ms,
                           double to_ms)
{
    size_t from = (size_t)(from_ms * MOD_RATE / 1000.0);
    size_t to = (size_t)(to_ms * MOD_RATE / 1000.0), i, high = 0;
    for (i = from; i < to; i++)
        if (stereo[2 * i] > 0) high++;
    return (double)high / (double)(to - from);
}

TT_TEST(lfo_to_pulse_width_moves_the_duty_by_the_depth)
{
    TT_CHECK_EQ(fm_engine_init(&mod_eng, MOD_RATE, 1), FM_OK);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, 50);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 100); /* 1 Hz */
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, 30);
    fm_engine_note_on(&mod_eng, 45, 127); /* 110 Hz: 401 samples a cycle */
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE);
    /* 50 % + 30 % for the first half second, 50 % - 30 % after */
    TT_CHECK_NEAR(duty_between(mod_buf, 50, 450), 0.80, 0.01);
    TT_CHECK_NEAR(duty_between(mod_buf, 550, 950), 0.20, 0.01);
    /* and the pulse stays at its set level (peak 1.0 at any width) */
    TT_CHECK_NEAR(peak_abs(mod_buf, MOD_RATE / 20, MOD_RATE * 9 / 20),
                  peak_abs(mod_buf, MOD_RATE * 11 / 20, MOD_RATE * 19 / 20),
                  200);
}

/*
 * Duty of a 1 % pulse on A4 under a 1 Hz square vibrato of an octave,
 * over the low half of the second LFO cycle, after a small bend sent at
 * `bend_ms` (which recomputes the oscillator's pitch and width).
 */
static double duty_after_bend_at(uint32_t bend_ms)
{
    uint32_t at = MOD_RATE / 1000 * bend_ms;
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_PULSE_WIDTH_PCT, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 100);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 1200);
    fm_engine_note_on(&mod_eng, 69, 127);
    fm_engine_render(&mod_eng, mod_buf, at);
    fm_engine_pitch_bend(&mod_eng, 8193);
    fm_engine_render(&mod_eng, mod_buf + 2 * at, 2 * MOD_RATE - at);
    return duty_between(mod_buf, 1550, 1950);
}

TT_TEST(pulse_width_limits_do_not_depend_on_the_lfo_phase)
{
    /* a bend at the top of the vibrato and one at the bottom: the
     * width limit (two samples from each edge) comes from the
     * unmodulated 440 Hz, 2.0 %, either way */
    double high = duty_after_bend_at(250), low = duty_after_bend_at(750);
    printf("    duty after a bend at the top %.4f, at the bottom %.4f\n",
           high, low);
    TT_CHECK_NEAR(high, low, 0.002);
    /* the 1 % the bottom (220 Hz) would allow reads about 0.0185 here
     * (the polyBLEP edges add about 0.8 %); 2.0 % reads about 0.028 */
    TT_CHECK(low > 0.024);
}

TT_TEST(restriking_a_sounding_note_keeps_its_vibrato)
{
    double before, after;
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200); /* 2 Hz */
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 1200);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 10);
    fm_engine_note_on(&mod_eng, 57, 127);
    /* after the 10 + 10 ms delay and fade, the triangle peaks (+1 octave)
     * at 125 ms: restrike there */
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 8);
    fm_engine_note_on(&mod_eng, 57, 127);
    fm_engine_render(&mod_eng, mod_buf + 2 * (MOD_RATE / 8), MOD_RATE / 8);
    before = hz_between(mod_buf, 110, 125);
    after = hz_between(mod_buf, 125, 140);
    printf("    %.1f Hz before the restrike, %.1f Hz after\n", before, after);
    /* the pitch carries on from the top of the vibrato (a 15 ms window
     * moves about 30 cents there) instead of dropping an octave */
    TT_CHECK(fabs(cents(after, before)) < 60);
}

/* The same in unison: a new note retriggers every voice, which carries on
 * from the LFO's last value instead of dropping to the bare pitch. */
TT_TEST(unison_retrigger_keeps_its_vibrato)
{
    double before, after;
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200); /* 2 Hz */
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 1200);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 10);
    fm_engine_note_on(&mod_eng, 57, 127);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 8);
    fm_engine_note_on(&mod_eng, 60, 100); /* retriggers, 3 semitones up */
    fm_engine_render(&mod_eng, mod_buf + 2 * (MOD_RATE / 8), MOD_RATE / 8);
    before = hz_between(mod_buf, 110, 125);
    after = hz_between(mod_buf, 125, 140);
    printf("    %.1f Hz before the retrigger, %.1f Hz after\n", before, after);
    /* up the 300 cents of the new note, still near the vibrato's top,
     * instead of an octave lower */
    TT_CHECK(fabs(cents(after, before) - 300) < 60);
}

/* A note restruck mid-vibrato: its voice's pitch and pulse width sums
 * are what they were before the strike, not back at 0 for even one tick
 * (the next tick would hide that from a pitch reading). Poly restrike and
 * unison retrigger alike. */
TT_TEST(restrike_keeps_the_lfo_sums_exactly)
{
    int mode;
    for (mode = 0; mode < 2; mode++) {
        int32_t pitch, pw;
        fm_engine_init(&mod_eng, MOD_RATE, 1);
        if (mode)
            fm_engine_set_param(&mod_eng, FM_PARAM_VOICE_MODE,
                                FM_VOICE_MODE_UNISON);
        fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200);
        fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 1200);
        fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, 30);
        fm_engine_note_on(&mod_eng, 57, 127);
        fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 8);
        pitch = fm_mod_get(&mod_eng.voices[0].mod, FM_MOD_PITCH1);
        pw = fm_mod_get(&mod_eng.voices[0].mod, FM_MOD_PW);
        TT_CHECK(pitch != 0 && pw != 0);
        fm_engine_note_on(&mod_eng, 57, 90);
        TT_CHECK_EQ(fm_mod_get(&mod_eng.voices[0].mod, FM_MOD_PITCH1), pitch);
        TT_CHECK_EQ(fm_mod_get(&mod_eng.voices[0].mod, FM_MOD_PW), pw);
    }
}

/* fm_engine_init sets every route, so the storage it starts from (a
 * task stack, reused RAM) never reaches the output: an engine laid over
 * poison renders exactly as one over zeros, notes, velocity and LFO alike. */
TT_TEST(init_sets_every_route_whatever_the_storage_held)
{
    static fm_engine dirty;
    size_t frames = 6000;
    memset(&mod_eng, 0, sizeof mod_eng);
    memset(&dirty, 0xA5, sizeof dirty);
    TT_CHECK_EQ(fm_engine_init(&mod_eng, MOD_RATE, 2), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&dirty, MOD_RATE, 2), FM_OK);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, 100);
    fm_engine_set_param(&dirty, FM_PARAM_AMP_VELOCITY, 100);
    fm_engine_note_on(&mod_eng, 57, 70);
    fm_engine_note_on(&dirty, 57, 70);
    fm_engine_render(&mod_eng, mod_ref, (uint32_t)frames);
    fm_engine_render(&dirty, mod_buf, (uint32_t)frames);
    TT_CHECK(memcmp(mod_ref, mod_buf, frames * 4) == 0);
}

/* A note in its decay; at 100 ms amp_velocity_pct is set to `sens` (or
 * left alone for -1). */
static void decay_with_velocity_change(int16_t *out, int32_t sens)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_DECAY_MS, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_SUSTAIN, 0);
    fm_engine_note_on(&mod_eng, 57, 90);
    fm_engine_render(&mod_eng, out, MOD_RATE / 10);
    if (sens >= 0) fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, sens);
    fm_engine_render(&mod_eng, out + 2 * (MOD_RATE / 10), MOD_RATE);
}

TT_TEST(velocity_setting_mid_note_leaves_the_note_alone)
{
    /* the setting applies from the next note on; the sounding decay
     * must not be recomputed */
    decay_with_velocity_change(mod_ref, -1);
    decay_with_velocity_change(mod_buf, 37);
    TT_CHECK(memcmp(mod_ref, mod_buf, (size_t)(MOD_RATE + MOD_RATE / 10) * 4) == 0);
}

/* Renders `frames` in blocks of `block` into `out`. */
static void render_in_blocks(int16_t *out, uint32_t frames, uint32_t block)
{
    while (frames) {
        uint32_t n = frames < block ? frames : block;
        fm_engine_render(&mod_eng, out, n);
        out += 2 * n;
        frames -= n;
    }
}

/* Notes with the vibrato depth off, on (inside the delay of a note a
 * steal started mid-block), then off again. */
static void depth_on_and_off_in_blocks(int16_t *out, uint32_t block)
{
    fm_engine_init(&mod_eng, MOD_RATE, 2);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 900);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 100);
    render_in_blocks(out, 333, block);
    fm_engine_note_on(&mod_eng, 60, 100);
    fm_engine_note_on(&mod_eng, 64, 100);
    render_in_blocks(out + 2 * 333, 2000, block);
    fm_engine_note_on(&mod_eng, 67, 100); /* steals: starts after 3 ms */
    render_in_blocks(out + 2 * 2333, 5000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 300);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, 20);
    render_in_blocks(out + 2 * 7333, 9000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, 0);
    render_in_blocks(out + 2 * 16333, 6000, block);
}

TT_TEST(block_size_does_not_change_output_as_depths_come_and_go)
{
    /* with every depth at 0 the engine batches its control ticks; the
     * output must still not depend on the block size around a change */
    static const uint32_t blocks[] = { 1, 31, 100, 4096 };
    size_t b, bytes = 22333u * 4;
    depth_on_and_off_in_blocks(mod_ref, 64);
    for (b = 0; b < 4; b++) {
        depth_on_and_off_in_blocks(mod_buf, blocks[b]);
        TT_CHECK(memcmp(mod_ref, mod_buf, bytes) == 0);
    }
}

/* Only the pitch envelope live, no LFO depth: two notes on a moving
 * envelope 1, the depths on mid-note (one on each osc) and off again. */
static void pitch_env_on_and_off_in_blocks(int16_t *out, uint32_t block)
{
    fm_engine_init(&mod_eng, MOD_RATE, 2);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_LEVEL, 500);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, 500);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 150);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_DECAY_MS, 300);
    render_in_blocks(out, 333, block);
    fm_engine_note_on(&mod_eng, 60, 100);
    render_in_blocks(out + 2 * 333, 2000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 1200);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC2_CENTS, -700);
    fm_engine_note_on(&mod_eng, 64, 100);
    render_in_blocks(out + 2 * 2333, 9000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC2_CENTS, 0);
    render_in_blocks(out + 2 * 11333, 6000, block);
}

TT_TEST(block_size_does_not_change_pitch_env_output)
{
    static const uint32_t blocks[] = { 1, 31, 100, 4096 };
    size_t b, bytes = 17333u * 4;
    pitch_env_on_and_off_in_blocks(mod_ref, 64);
    for (b = 0; b < 4; b++) {
        pitch_env_on_and_off_in_blocks(mod_buf, blocks[b]);
        TT_CHECK(memcmp(mod_ref, mod_buf, bytes) == 0);
    }
}

/* A pulse note, its LFO routed to pulse width only at `pct` for 9000
 * frames, then back to 0: 20000 frames in blocks of `block`. */
static void pw_only_on_and_off(int16_t *out, int32_t pct, uint32_t block)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 900);
    fm_engine_note_on(&mod_eng, 60, 100);
    render_in_blocks(out, 1000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, pct);
    render_in_blocks(out + 2 * 1000, 9000, block);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PW_PCT, 0);
    render_in_blocks(out + 2 * 10000, 10000, block);
}

/* With only the pulse width routed, the LFO still ticks the voices (in
 * step, whatever the block size) and, once the depth is back at 0, the
 * offset clears: the tail matches a note never modulated. */
TT_TEST(pulse_width_alone_ticks_the_voices_and_clears)
{
    size_t bytes = 20000u * 4, tail = 10100;
    pw_only_on_and_off(mod_ref, 0, 4096);
    pw_only_on_and_off(mod_buf, 25, 4096);
    TT_CHECK(memcmp(mod_ref + 2 * 1000, mod_buf + 2 * 1000, 9000u * 4) != 0);
    TT_CHECK(memcmp(mod_ref + 2 * tail, mod_buf + 2 * tail,
                    (20000u - tail) * 4) == 0);
    pw_only_on_and_off(mod_ref, 25, 1);
    TT_CHECK(memcmp(mod_ref, mod_buf, bytes) == 0);
}

/* A4 with a 2 Hz square vibrato of 100 cents and a 500 ms delay; the
 * depth is set before the note, or only after 1 s with `late`. Returns
 * the pitch (cents) over 1.02..1.23 s, the top half of an LFO cycle. */
static double vibrato_after_late_depth(int late)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_DELAY_MS, 500);
    if (!late) fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE);
    if (late) fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    fm_engine_render(&mod_eng, mod_buf + 2 * MOD_RATE, MOD_RATE / 2);
    return cents(hz_between(mod_buf, 1020, 1230), note_hz(69));
}

/* The one vibrato depth moves osc 2 as well as osc 1: with only osc 2
 * heard (osc1 at level 0, osc2 at full), a 100-cent square vibrato reads
 * +100 cents over the top half of an LFO cycle. */
TT_TEST(vibrato_reaches_osc2)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    TT_CHECK(fabs(cents(hz_between(mod_buf, 20, 230), note_hz(69)) - 100) < 3);
}

/* Osc 2's own vibrato depth: at 0 with osc 1's at 100 cents, the heard
 * osc 2 holds its pitch (BUBBLES: DCO-1's LFO wobbles the muted DCO-1). */
TT_TEST(osc2_vibrato_depth_zero_holds_osc2_still)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200);
    fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_CENTS, 100);
    TT_CHECK_EQ(fm_engine_set_param(&mod_eng, FM_PARAM_LFO_PITCH_OSC2_CENTS, 0), FM_OK);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    TT_CHECK(fabs(cents(hz_between(mod_buf, 20, 230), note_hz(69))) < 3);
}

/* A4 with the filter envelope held at full (no attack, full sustain), a
 * pitch envelope depth of `osc1` / `osc2` cents and osc 2 at level `mix` per
 * mille, osc 1 at the rest (0: osc 1 heard, 1000: osc 2); the pitch in cents over 0.3..0.5 s. */
static double pitch_env_cents(int32_t osc1, int32_t osc2, int32_t mix)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_LEVEL, 1000 - (mix));
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, mix);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, osc1);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC2_CENTS, osc2);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return cents(hz_between(mod_buf, 300, 500), note_hz(69));
}

/* DCO-1's envelope depth (agent2's first test on #58): +1573 cents with
 * envelope 1 at full puts osc 1 1573 cents sharp, and osc 2 stays. */
TT_TEST(osc1_pitch_env_bends_osc1_only)
{
    TT_CHECK(fabs(pitch_env_cents(1573, 0, 0) - 1573) < 3);
    TT_CHECK(fabs(pitch_env_cents(1573, 0, 1000)) < 3);
}

/* DCO-2's envelope depth moves osc 2 only. */
TT_TEST(osc2_pitch_env_bends_osc2_only)
{
    TT_CHECK(fabs(pitch_env_cents(0, 1573, 1000) - 1573) < 3);
    TT_CHECK(fabs(pitch_env_cents(0, 1573, 0)) < 3);
}

/* A negative depth (the inverted modes) is the exact mirror: PG-8X reads
 * -1576 against +1573 cents. */
TT_TEST(negative_pitch_env_is_the_mirror)
{
    TT_CHECK(fabs(pitch_env_cents(-1573, 0, 0) + 1573) < 3);
    TT_CHECK(fabs(pitch_env_cents(0, -1573, 1000) + 1573) < 3);
}

/* Linear in the envelope's level (PG-8X: half the sustain, half the
 * cents): with envelope 1 held at half, +1600 cents reads +800. */
TT_TEST(pitch_env_is_linear_in_the_level)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_DECAY_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 500);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 1600);
    fm_engine_note_on(&mod_eng, 57, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    TT_CHECK(fabs(cents(hz_between(mod_buf, 300, 500), note_hz(57)) - 800) < 3);
}

/* pitch_env_source 1 follows envelope 2, the amp envelope's share of the
 * note's peak: held at half, +1600 cents reads +800 at any velocity
 * (envelope 1 held at 0 would give none). */
static double pitch_env_amp_cents(uint8_t velocity)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_SUSTAIN, 500);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, 100);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_SOURCE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 1600);
    fm_engine_note_on(&mod_eng, 57, velocity);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return cents(hz_between(mod_buf, 300, 500), note_hz(57));
}

/* With the gate (#63) envelope 2 runs apart from the VCA: the pitch
 * envelope on it reads envelope 2 held at half (+800 cents), not the gate,
 * which holds the note at full. */
TT_TEST(pitch_env_on_envelope_2_follows_it_with_the_gate)
{
    double got;
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_SUSTAIN, 500);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_GATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_SOURCE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 1600);
    fm_engine_note_on(&mod_eng, 57, 127);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    got = cents(hz_between(mod_buf, 300, 500), note_hz(57));
    printf("    gate, pitch env on envelope 2 at half: %+.1f cents\n", got);
    TT_CHECK(fabs(got - 800) < 3);
}

TT_TEST(pitch_env_from_the_amp_envelope_ignores_velocity)
{
    TT_CHECK(fabs(pitch_env_amp_cents(127) - 800) < 3);
    TT_CHECK(fabs(pitch_env_amp_cents(40) - 800) < 3);
}

/* DCO DYNAMICS: the note's velocity scales the pitch envelope by the
 * shared DYNAMICS curve (engine/dyna.c): position 2 at velocity 64 plays
 * fm_dyna_share_q15(2, 64) of +1600 cents; position 0 plays all of it. */
static double pitch_env_dyna_cents(int32_t dyna, uint8_t velocity)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 1600);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_DYNA, dyna);
    fm_engine_note_on(&mod_eng, 57, velocity);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return cents(hz_between(mod_buf, 300, 500), note_hz(57));
}

TT_TEST(pitch_env_dynamics_scale_by_velocity)
{
    double want = 1600.0 * fm_dyna_share_q15(2, 64) / 32768.0;
    TT_CHECK(fabs(pitch_env_dyna_cents(2, 64) - want) < 3);
    TT_CHECK(fabs(pitch_env_dyna_cents(0, 64) - 1600) < 3);
}

/* The pitch envelope adds to each osc's tuning and the bend, also when
 * the bend comes while the envelope is held: osc 1 an octave down, 50
 * cents sharp, bent 2 semitones up and +700 cents of envelope reads -250
 * cents; osc 2 coarse +3 with +400 reads +900. */
static double tuned_pitch_env_cents(int32_t mix)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_LEVEL, 1000 - (mix));
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_LEVEL, mix);
    fm_engine_set_param(&mod_eng, FM_PARAM_BEND_RANGE, 2);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_OCTAVE, -1);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC1_FINE_CENTS, 50);
    fm_engine_set_param(&mod_eng, FM_PARAM_OSC2_COARSE_SEMIS, 3);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&mod_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC1_CENTS, 700);
    fm_engine_set_param(&mod_eng, FM_PARAM_PITCH_ENV_OSC2_CENTS, 400);
    fm_engine_note_on(&mod_eng, 69, 100);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 5);
    fm_engine_pitch_bend(&mod_eng, 16383);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return cents(hz_between(mod_buf, 100, 300), note_hz(69));
}

TT_TEST(pitch_env_adds_to_tuning_and_bend)
{
    TT_CHECK(fabs(tuned_pitch_env_cents(0) + 250) < 3);
    TT_CHECK(fabs(tuned_pitch_env_cents(1000) - 900) < 3);
}

TT_TEST(lfo_delay_counts_from_the_note_even_with_no_depth_set)
{
    /* the delay ran out at 0.5 s whether or not a depth was set then */
    double early = vibrato_after_late_depth(0), late = vibrato_after_late_depth(1);
    printf("    depth before the note %.1f cents, after 1 s %.1f cents\n",
           early, late);
    TT_CHECK_NEAR(early, 100, 1);
    TT_CHECK_NEAR(late, early, 0.5);
}

static void run_mod_tests(void)
{
    TT_RUN(lfo_to_pitch_moves_the_pitch_by_the_depth);
    TT_RUN(zero_pitch_depth_leaves_the_output_untouched);
    TT_RUN(lfo_delay_holds_modulation_at_zero_then_fades_it_in);
    TT_RUN(block_size_does_not_change_lfo_output);
    TT_RUN(zero_pulse_width_depth_leaves_the_output_untouched);
    TT_RUN(lfo_to_pulse_width_moves_the_duty_by_the_depth);
    TT_RUN(higher_velocity_gives_louder_output);
    TT_RUN(softer_retrigger_glides_down_without_a_click);
    TT_RUN(unison_legato_return_keeps_the_last_strike_velocity);
    TT_RUN(pulse_width_limits_do_not_depend_on_the_lfo_phase);
    TT_RUN(restriking_a_sounding_note_keeps_its_vibrato);
    TT_RUN(unison_retrigger_keeps_its_vibrato);
    TT_RUN(restrike_keeps_the_lfo_sums_exactly);
    TT_RUN(init_sets_every_route_whatever_the_storage_held);
    TT_RUN(velocity_setting_mid_note_leaves_the_note_alone);
    TT_RUN(block_size_does_not_change_output_as_depths_come_and_go);
    TT_RUN(pulse_width_alone_ticks_the_voices_and_clears);
    TT_RUN(vibrato_reaches_osc2);
    TT_RUN(osc2_vibrato_depth_zero_holds_osc2_still);
    TT_RUN(osc1_pitch_env_bends_osc1_only);
    TT_RUN(osc2_pitch_env_bends_osc2_only);
    TT_RUN(negative_pitch_env_is_the_mirror);
    TT_RUN(pitch_env_is_linear_in_the_level);
    TT_RUN(pitch_env_from_the_amp_envelope_ignores_velocity);
    TT_RUN(pitch_env_on_envelope_2_follows_it_with_the_gate);
    TT_RUN(pitch_env_dynamics_scale_by_velocity);
    TT_RUN(pitch_env_adds_to_tuning_and_bend);
    TT_RUN(block_size_does_not_change_pitch_env_output);
    TT_RUN(lfo_delay_counts_from_the_note_even_with_no_depth_set);
}
