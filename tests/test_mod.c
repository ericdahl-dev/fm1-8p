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

/*
 * Renders the phrase with every LFO setting at its default into mod_ref,
 * then with `id` at `value` plus a fast square LFO and a delay, into
 * mod_buf. Returns whether the two match byte for byte.
 */
static int same_as_default(uint16_t id, int32_t value, size_t *frames)
{
    size_t n;
    fm_engine_init(&mod_eng, MOD_RATE, 3);
    n = mod_phrase(mod_ref);
    fm_engine_init(&mod_eng, MOD_RATE, 3);
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
static int held_peak(int32_t sens, uint8_t velocity)
{
    fm_engine_init(&mod_eng, MOD_RATE, 1);
    fm_engine_set_param(&mod_eng, FM_PARAM_AMP_VELOCITY, sens);
    fm_engine_note_on(&mod_eng, 69, velocity);
    fm_engine_render(&mod_eng, mod_buf, MOD_RATE / 2);
    return peak_abs(mod_buf, MOD_RATE / 5, MOD_RATE / 2);
}

TT_TEST(higher_velocity_gives_louder_output)
{
    int full = held_peak(100, 127), prev = 0;
    uint8_t vel;
    TT_CHECK(full > 8000);
    /* full velocity is full level whatever the sensitivity */
    TT_CHECK_EQ(held_peak(0, 127), full);
    TT_CHECK_EQ(held_peak(50, 127), full);
    /* louder with every step up, following (velocity / 127)^2 */
    for (vel = 8; vel <= 127; vel = (uint8_t)(vel + 17)) {
        int p = held_peak(100, vel);
        double expect = (vel / 127.0) * (vel / 127.0);
        TT_CHECK(p > prev);
        TT_CHECK_NEAR((double)p / full, expect, 0.01);
        prev = p;
    }
    /* half sensitivity goes halfway to that curve */
    TT_CHECK_NEAR((double)held_peak(50, 64) / full,
                  1 - 0.5 * (1 - (64 / 127.0) * (64 / 127.0)), 0.01);
    /* off: every velocity plays at full level */
    TT_CHECK_EQ(held_peak(0, 1), full);
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
    fm_engine_render(&mod_eng, mod_buf, 10000);
    *level = peak_abs(mod_buf, 4500, 10000);
    return jump;
}

TT_TEST(softer_retrigger_glides_down_without_a_click)
{
    int loud, soft, jump;
    retrigger_at(127, &loud);
    jump = retrigger_at(20, &soft); /* same note, much softer */
    printf("    largest jump: %d LSB\n", jump);
    TT_CHECK(jump <= CLICK_LSB);
    TT_CHECK(loud > 8000);
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
    fm_engine_note_on(&mod_eng, 60, 127);
    loud = peak_after(500);
    fm_engine_note_on(&mod_eng, 64, 40); /* softer strike */
    soft = peak_after(500);
    fm_engine_note_off(&mod_eng, 64); /* back to 60, legato */
    back = peak_after(500);
    TT_CHECK(soft < loud / 4);
    TT_CHECK_NEAR(back, soft, soft * 0.05);
}

static void run_mod_tests(void)
{
    TT_RUN(lfo_to_pitch_moves_the_pitch_by_the_depth);
    TT_RUN(zero_pitch_depth_leaves_the_output_untouched);
    TT_RUN(lfo_delay_holds_modulation_at_zero_then_fades_it_in);
    TT_RUN(block_size_does_not_change_lfo_output);
    TT_RUN(higher_velocity_gives_louder_output);
    TT_RUN(softer_retrigger_glides_down_without_a_click);
    TT_RUN(unison_legato_return_keeps_the_last_strike_velocity);
}
