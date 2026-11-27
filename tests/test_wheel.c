/* The mod wheel (#41): MIDI CC1 adds each patch's wheel amount to the LFO's vibrato and pulse-width
 * depths, smoothed, through the engine's public interface and its route depths (engine/mod.h). */

#define WH_RATE 44100u
#define WH_SETTLE 4410u                         /* 0.1 s: the wheel's slew is far shorter */

static fm_engine wh_a, wh_b;
static int16_t wh_buf_a[WH_RATE * 2], wh_buf_b[WH_RATE * 2];

static void wh_setup(fm_engine *e)
{
    TT_CHECK_EQ(fm_engine_init(e, WH_RATE, 2), FM_OK);
    fm_engine_set_param(e, FM_PARAM_LFO_WAVE, FM_LFO_TRIANGLE);
    fm_engine_set_param(e, FM_PARAM_LFO_RATE_CENTIHZ, 550);
}

static int wh_same(uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < 2u * frames; i++)
        if (wh_buf_a[i] != wh_buf_b[i])
            return 0;
    return 1;
}

static int32_t wh_route(const fm_engine *e, unsigned dest)
{
    return e->mod.routes.depth[FM_MOD_SRC_LFO][dest];
}

/* The new params' defaults (Eric): the wheel adds 50 ct of vibrato and no pulse width. */
TT_TEST(wheel_defaults_add_vibrato_only)
{
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_WHEEL_PITCH_CENTS)->def, 50);
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_WHEEL_PW_PCT)->def, 0);
}

/* With the wheel at 0 (never moved, or CC1 0) the output is what it was: the same samples. */
TT_TEST(wheel_at_zero_changes_nothing)
{
    wh_setup(&wh_a);
    wh_setup(&wh_b);
    fm_engine_set_param(&wh_a, FM_PARAM_LFO_PITCH_CENTS, 30);
    fm_engine_set_param(&wh_b, FM_PARAM_LFO_PITCH_CENTS, 30);
    fm_engine_controller(&wh_b, 1, 0);
    fm_engine_note_on(&wh_a, 57, 100);
    fm_engine_note_on(&wh_b, 57, 100);
    fm_engine_render(&wh_a, wh_buf_a, WH_RATE);
    fm_engine_render(&wh_b, wh_buf_b, WH_RATE);
    TT_CHECK(wh_same(WH_RATE));
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PITCH1), 30 * 65536 / 1200);
}

/* At 127 the wheel adds its whole amount: a patch with no vibrato and the wheel up sounds as the same
 * patch with the amount as its vibrato depth, sample for sample, once the wheel has settled. Osc 2 gets
 * it too (the wheel adds to both oscillators' vibrato). */
TT_TEST(wheel_full_adds_its_amount_to_the_vibrato)
{
    wh_setup(&wh_a);
    wh_setup(&wh_b);
    fm_engine_set_param(&wh_a, FM_PARAM_WHEEL_PITCH_CENTS, 75);
    fm_engine_set_param(&wh_b, FM_PARAM_LFO_PITCH_CENTS, 75);
    fm_engine_set_param(&wh_b, FM_PARAM_LFO_PITCH_OSC2_CENTS, 75);
    fm_engine_set_param(&wh_a, FM_PARAM_LFO_PITCH_OSC2_CENTS, 0);    /* osc 2's own depth 0 */
    fm_engine_controller(&wh_a, 1, 127);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    fm_engine_render(&wh_b, wh_buf_b, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PITCH1), wh_route(&wh_b, FM_MOD_PITCH1));
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PITCH2), wh_route(&wh_b, FM_MOD_PITCH2));
    fm_engine_note_on(&wh_a, 57, 100);
    fm_engine_note_on(&wh_b, 57, 100);
    fm_engine_render(&wh_a, wh_buf_a, WH_RATE / 2);
    fm_engine_render(&wh_b, wh_buf_b, WH_RATE / 2);
    TT_CHECK(wh_same(WH_RATE / 2));
}

/* The cutoff (#172): the wheel adds wheel_cutoff_cents to the VCF LFO's depth; 0 by default, so the
 * wheel moves no cutoff until a patch sets it (the JX-8P has no wheel of its own). */
TT_TEST(wheel_adds_its_cutoff_amount_to_the_vcf_lfo)
{
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_WHEEL_CUTOFF_CENTS)->def, 0);
    wh_setup(&wh_a);
    fm_engine_set_param(&wh_a, FM_PARAM_LFO_CUTOFF_CENTS, 300);
    fm_engine_set_param(&wh_a, FM_PARAM_WHEEL_CUTOFF_CENTS, 600);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_CUTOFF), 300);
    fm_engine_controller(&wh_a, 1, 127);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_CUTOFF), 900);
    fm_engine_controller(&wh_a, 1, 0);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_CUTOFF), 300);
}

/* Pulse width: the patch's depth plus the wheel's, at most the route's 49 %. */
TT_TEST(wheel_adds_pulse_width_up_to_its_limit)
{
    wh_setup(&wh_a);
    fm_engine_set_param(&wh_a, FM_PARAM_LFO_PW_PCT, 20);
    fm_engine_set_param(&wh_a, FM_PARAM_WHEEL_PW_PCT, 10);
    fm_engine_controller(&wh_a, 1, 127);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PW), 30 * 42949673);
    fm_engine_set_param(&wh_a, FM_PARAM_WHEEL_PW_PCT, 49);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PW), 49 * 42949673);
    fm_engine_controller(&wh_a, 1, 64);         /* half way: 20 + 49 x 64 / 127 */
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PW), (20 + 49 * 64 / 127) * 42949673);
}

/* A wheel move is smoothed: the depth climbs a little each control tick, never in one jump, and arrives
 * exactly. */
TT_TEST(wheel_move_is_smoothed)
{
    int32_t last, now, top = 1200 * 65536 / 1200, worst = 0;
    uint32_t ticks = 0;
    wh_setup(&wh_a);
    fm_engine_set_param(&wh_a, FM_PARAM_WHEEL_PITCH_CENTS, 1200);
    fm_engine_note_on(&wh_a, 57, 100);
    fm_engine_render(&wh_a, wh_buf_a, 1000);
    fm_engine_controller(&wh_a, 1, 127);
    last = wh_route(&wh_a, FM_MOD_PITCH1);
    TT_CHECK_EQ(last, 0);                        /* nothing yet: the move starts at the next tick */
    while (ticks < 1000u && last != top) {
        fm_engine_render(&wh_a, wh_buf_a, FM_LFO_TICK_FRAMES);
        now = wh_route(&wh_a, FM_MOD_PITCH1);
        worst = now - last > worst ? now - last : worst;
        last = now;
        ticks++;
    }
    TT_CHECK_EQ(last, top);
    TT_CHECK(ticks >= 16u);                     /* at least 16 ticks (about 12 ms): no jump */
    TT_CHECK(worst <= top / 16 + 1);
}

/* All controllers off (CC121) puts the wheel back to 0: the depths go back to the patch's. */
TT_TEST(reset_all_controllers_puts_the_wheel_back)
{
    wh_setup(&wh_a);
    fm_engine_set_param(&wh_a, FM_PARAM_LFO_PITCH_CENTS, 25);
    fm_engine_controller(&wh_a, 1, 127);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PITCH1), (25 + 50) * 65536 / 1200);
    fm_engine_controller(&wh_a, 121, 0);
    fm_engine_render(&wh_a, wh_buf_a, WH_SETTLE);
    TT_CHECK_EQ(wh_route(&wh_a, FM_MOD_PITCH1), 25 * 65536 / 1200);
}

/* A wheel move mid-note sounds the same rendered in blocks of 1, 37 or 256 frames, from a patch with no
 * LFO depth of its own (so the move itself must cut the render at each control tick). */
TT_TEST(wheel_move_does_not_depend_on_the_block_size)
{
    static const uint32_t SIZES[] = {1, 37, 256};
    uint32_t r, done, n;
    for (r = 0; r < 3u; r++) {
        fm_engine *e = r ? &wh_b : &wh_a;
        int16_t *buf = r ? wh_buf_b : wh_buf_a;
        wh_setup(e);
        fm_engine_set_param(e, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);   /* at full at once: no delay */
        fm_engine_set_param(e, FM_PARAM_LFO_DELAY_MS, 0);
        fm_engine_set_param(e, FM_PARAM_WHEEL_PITCH_CENTS, 1200);
        fm_engine_set_param(e, FM_PARAM_WHEEL_PW_PCT, 30);
        fm_engine_set_param(e, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
        fm_engine_note_on(e, 45, 100);
        for (done = 0; done < WH_RATE / 2; done += n) {
            if (done == 4010)
                fm_engine_controller(e, 1, 100);
            n = SIZES[r] < WH_RATE / 2 - done ? SIZES[r] : WH_RATE / 2 - done;
            if (done < 4010 && done + n > 4010)
                n = 4010 - done;                /* (the move at frame 4010, off the 32-frame ticks) */
            fm_engine_render(e, buf + 2u * done, n);
        }
        if (r)
            TT_CHECK(wh_same(WH_RATE / 2));
    }
}

static void run_wheel_tests(void)
{
    TT_RUN(wheel_defaults_add_vibrato_only);
    TT_RUN(wheel_at_zero_changes_nothing);
    TT_RUN(wheel_full_adds_its_amount_to_the_vibrato);
    TT_RUN(wheel_adds_pulse_width_up_to_its_limit);
    TT_RUN(wheel_adds_its_cutoff_amount_to_the_vcf_lfo);
    TT_RUN(wheel_move_is_smoothed);
    TT_RUN(reset_all_controllers_puts_the_wheel_back);
    TT_RUN(wheel_move_does_not_depend_on_the_block_size);
}
