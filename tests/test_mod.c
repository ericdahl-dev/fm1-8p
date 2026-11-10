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

static void run_mod_tests(void)
{
    TT_RUN(lfo_to_pitch_moves_the_pitch_by_the_depth);
    TT_RUN(zero_pitch_depth_leaves_the_output_untouched);
    TT_RUN(lfo_delay_holds_modulation_at_zero_then_fades_it_in);
    TT_RUN(block_size_does_not_change_lfo_output);
}
