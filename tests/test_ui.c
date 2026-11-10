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

static void run_ui_tests(void)
{
    TT_RUN(ui_reso_env_and_key_read_in_their_units);
    TT_RUN(ui_cutoff_reads_in_hz_three_digits);
    TT_RUN(ui_knob_clamps_at_the_ends_of_the_range);
    TT_RUN(ui_knobs_do_nothing_in_play);
    TT_RUN(ui_fast_turn_moves_the_fast_step_per_click);
    TT_RUN(ui_vcf_page_knobs_move_their_params_one_step);
    TT_RUN(ui_vcf_knob2_click_moves_resonance);
}
