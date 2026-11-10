/* The on-device UI's logic (firmware/ui.c): pages, knobs and values, run on the desktop. */
#include "../firmware/ui.h"

static play ui_p;
static ui ui_u;
static int32_t ui_out[2 * PLAY_BLOCK];

static void ui_setup(void)
{
    (void)play_init(&ui_p);
    ui_init(&ui_u, &ui_p);
}

/* audio blocks until a knob's change has fully reached the engine */
static void ui_settle(void)
{
    uint32_t i, k;
    for (k = 0; k < 64; k++) {
        for (i = 0; i < 2 * PLAY_BLOCK; i++)
            ui_out[i] = 0;
        play_render(&ui_p, ui_out, PLAY_BLOCK);
    }
}

/* On page 4A, KNOB2 is 42 RESO: a click moves it 1 on screen, 10 per mille in the engine. */
TT_TEST(ui_vcf_knob2_click_moves_resonance)
{
    int32_t before;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    before = fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), before + 10);
}

/* Page 4A (interaction.md section 3): 41 CUTOFF 100 ct, 42 RESO 10 per mille, 43 ENV 100 ct (1 st),
 * 44 KEY 10 per mille a click; a click down moves back. */
TT_TEST(ui_vcf_page_knobs_move_their_params_one_step)
{
    static const struct { uint32_t enc; uint16_t id; int32_t step; } K[4] = {
        {UI_ENC_KNOB1, FM_PARAM_FILTER_CUTOFF_CENTS, 100},
        {UI_ENC_KNOB2, FM_PARAM_FILTER_RESONANCE, 10},
        {UI_ENC_KNOB3, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 100},
        {UI_ENC_KNOB4, FM_PARAM_FILTER_KEY_TRACK, 10},
    };
    uint32_t k;
    for (k = 0; k < 4; k++) {
        int32_t before;
        ui_setup();
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
        before = play_get_param(&ui_p, K[k].id);
        ui_event(&ui_u, UI_EV_ENC | K[k].enc, -1, 0);
        ui_settle();
        TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, K[k].id), before - K[k].step);
    }
}

/* One event of 3 or more clicks is a fast turn: each click moves the fast step (CUTOFF 400 ct). */
TT_TEST(ui_fast_turn_moves_the_fast_step_per_click)
{
    int32_t before;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    before = play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 3, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), before + 1200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -2, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), before + 1000);
}

/* At the end of its range a value stays put (no wrap). */
TT_TEST(ui_knob_clamps_at_the_ends_of_the_range)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 120, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -200, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -9600);
}

/* This slice's PLAY screen has no knobs: a turn there changes nothing. */
TT_TEST(ui_knobs_do_nothing_in_play)
{
    int32_t before;
    ui_setup();
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    before = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), before);
}

static int ui_shows(uint16_t id, int32_t value, const char *want)
{
    char s[UI_VALUE_LEN];
    ui_format(id, value, s);
    if (strcmp(s, want) != 0) {
        printf("    %d shows \"%s\", not \"%s\"\n", (int)value, s, want);
        return 0;
    }
    return 1;
}

/* CUTOFF reads in Hz from 8.18 x 2^(cents / 1200), three significant digits; OPEN at the top. */
TT_TEST(ui_cutoff_reads_in_hz_three_digits)
{
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 6900, "440 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 14400, "OPEN"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 0, "8.18 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 9100, "1.57 kHz"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 13200, "16.7 kHz"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 3300, "55.0 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 7800, "740 Hz"));
}

/* RESO 0..100 (10 per mille a step), ENV signed semitones, KEY a percentage. */
TT_TEST(ui_reso_env_and_key_read_in_their_units)
{
    TT_CHECK(ui_shows(FM_PARAM_FILTER_RESONANCE, 340, "34"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_RESONANCE, 1000, "100"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400, "+24 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0, "0 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -9600, "-96 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_KEY_TRACK, 500, "50 %"));
}

/* The knob meter, an 8-segment ladder (bit k: segment k lit, left to right): a range lights from
 * the left; ENV, bipolar, lights out from the middle. */
TT_TEST(ui_meter_lights_the_value)
{
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_RESONANCE, 340), 0x07u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_KEY_TRACK, 500), 0x0Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_CUTOFF_CENTS, 9300), 0x1Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_RESONANCE, 0), 0u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400), 0x10u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -9600), 0x0Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0), 0u);
}

/* What to redraw: the page in full when it opens; after a turn, that knob's column and the focus
 * row (the focus moves to the knob turned: the column it left too), and the curve only for CUTOFF
 * and RESO. Turns closer than UI_FRAME_MS to the last redraw wait for the next one. */
TT_TEST(ui_redraws_only_what_a_turn_changed)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    TT_CHECK_EQ(ui_take(&ui_u, 0) & UI_D_FULL, UI_D_FULL);
    TT_CHECK_EQ(ui_take(&ui_u, 100), 0u);       /* nothing changed */
    TT_CHECK_EQ(ui_u.focus, 1u);                /* RESO, as the page opens */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 200);
    TT_CHECK_EQ(ui_take(&ui_u, 200), UI_D_COL(1) | UI_D_FOCUS | UI_D_SLOT);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 210);
    TT_CHECK_EQ(ui_take(&ui_u, 210), 0u);       /* within a frame of the last redraw */
    TT_CHECK_EQ(ui_take(&ui_u, 200 + UI_FRAME_MS), UI_D_COL(1) | UI_D_COL(2) | UI_D_FOCUS);
    TT_CHECK_EQ(ui_u.focus, 2u);
}

/* EDIT goes back to the PLAY screen, in full. */
TT_TEST(ui_edit_returns_to_play)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    (void)ui_take(&ui_u, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 400);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 500);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    TT_CHECK_EQ(ui_take(&ui_u, 500) & UI_D_FULL, UI_D_FULL);
}

/* The slot's filter curve, as render.py's filter_curve draws it (the cutoff and the resonance as
 * fractions of their range; column i of n across 2.6 decades): its level in tenths of a dB, to
 * half a dB of the mockup's floating point. */
TT_TEST(ui_curve_follows_the_mockup)
{
    TT_CHECK_NEAR(ui_curve_db(9300, 340, 0, 226), 0, 5);
    TT_CHECK_NEAR(ui_curve_db(9300, 340, 120, 226), 39, 5);    /* the resonant bump */
    TT_CHECK_NEAR(ui_curve_db(9300, 340, 150, 226), -38, 5);
    TT_CHECK_NEAR(ui_curve_db(9300, 340, 200, 226), -302, 5);
    TT_CHECK_NEAR(ui_curve_db(7800, 200, 140, 226), -114, 5);
    TT_CHECK_NEAR(ui_curve_db(14400, 1000, 200, 226), 89, 5);
    TT_CHECK_NEAR(ui_curve_db(0, 0, 50, 226), -231, 5);
}

static int ui_column_shows(uint16_t id, int32_t value, const char *num, const char *unit)
{
    char n[UI_VALUE_LEN], u[UI_VALUE_LEN];
    ui_column(id, value, n, u);
    if (strcmp(n, num) != 0 || strcmp(u, unit) != 0) {
        printf("    %d shows \"%s\" \"%s\", not \"%s\" \"%s\"\n", (int)value, n, u, num, unit);
        return 0;
    }
    return 1;
}

/* A knob column has 56 px: the number, and a short unit after it (visual.md: 1.25k, +24st). */
TT_TEST(ui_column_value_is_short)
{
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 9100, "1.57", "k"));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 6900, "440", ""));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_CUTOFF_CENTS, 14400, "OPEN", ""));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_RESONANCE, 340, "34", ""));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400, "+24", "st"));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_KEY_TRACK, 500, "50", "%"));
}

/* The header's page strip: 14 segments with 2 px gaps share 230 px from x 5 (the leftover px one
 * each to the first); 4A is the 5th. */
TT_TEST(ui_page_strip_fills_the_rule)
{
    int32_t x, w, end = 5;
    uint32_t i;
    for (i = 0; i < UI_PAGES; i++) {
        ui_strip(i, &x, &w);
        TT_CHECK_EQ(x, end);
        end = x + w + 2;
    }
    TT_CHECK_EQ(end - 2, 235);
    TT_CHECK_EQ(UI_PAGE_4A, 4u);
}

/* The LED window's 7-segment digits (bits a..g = 0..6). */
TT_TEST(ui_seg7_digits)
{
    TT_CHECK_EQ(ui_seg7('4'), 0x66u);           /* f g b c */
    TT_CHECK_EQ(ui_seg7('2'), 0x5Bu);           /* a b g e d */
    TT_CHECK_EQ(ui_seg7('8'), 0x7Fu);
    TT_CHECK_EQ(ui_seg7('-'), 0x40u);
    TT_CHECK_EQ(ui_seg7(' '), 0u);
}

/* HOME tap goes back to PLAY (interaction.md: EDIT --> PLAY on a HOME tap). */
TT_TEST(ui_home_tap_returns_to_play)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_HOME, 0, 1000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_HOME, 0, 1100);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
}

/* EDIT held 400 ms or more is compare (not built yet): it doesn't change the page. */
TT_TEST(ui_edit_hold_does_not_toggle)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 1400);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 2000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2399);  /* a tap */
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
}

static void run_ui_tests(void)
{
    TT_RUN(ui_edit_hold_does_not_toggle);
    TT_RUN(ui_home_tap_returns_to_play);
    TT_RUN(ui_page_strip_fills_the_rule);
    TT_RUN(ui_seg7_digits);
    TT_RUN(ui_column_value_is_short);
    TT_RUN(ui_curve_follows_the_mockup);
    TT_RUN(ui_redraws_only_what_a_turn_changed);
    TT_RUN(ui_edit_returns_to_play);
    TT_RUN(ui_meter_lights_the_value);
    TT_RUN(ui_reso_env_and_key_read_in_their_units);
    TT_RUN(ui_cutoff_reads_in_hz_three_digits);
    TT_RUN(ui_knob_clamps_at_the_ends_of_the_range);
    TT_RUN(ui_knobs_do_nothing_in_play);
    TT_RUN(ui_fast_turn_moves_the_fast_step_per_click);
    TT_RUN(ui_vcf_page_knobs_move_their_params_one_step);
    TT_RUN(ui_vcf_knob2_click_moves_resonance);
}
