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

/* Opens the VCF page that has param id (ENV's ring) and returns its knob's encoder, 0 if none. */
static uint32_t ui_goto(uint16_t id)
{
    uint32_t n, k;
    for (n = 0; n < 8u; n++) {                  /* (ENV's ring has 6 pages) */
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
        for (k = 0; k < 4u; k++)
            if (ui_page_slot(&ui_u, k)->kind == UI_LIVE && ui_page_slot(&ui_u, k)->id == id)
                return UI_ENC_KNOB1 + k;
    }
    return 0;
}

/* On page 5A, KNOB3 is 53 RES: a click moves it 1 on screen, 10 per mille in the engine. */
TT_TEST(ui_vcf_knob3_click_moves_resonance)
{
    int32_t before;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    before = fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 0);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), before + 10);
}

/* The VCF pages: 52 CUTOFF 100 ct, 53 RES 10 per mille, 55 ENV 100 ct (1 st), 56 KEY 10 per mille
 * a click; a click down moves back. */
TT_TEST(ui_vcf_page_knobs_move_their_params_one_step)
{
    static const struct { uint16_t id; int32_t step; } K[4] = {
        {FM_PARAM_FILTER_CUTOFF_CENTS, 100},
        {FM_PARAM_FILTER_RESONANCE, 10},
        {FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 100},
        {FM_PARAM_FILTER_KEY_TRACK, 10},
    };
    uint32_t k;
    for (k = 0; k < 4; k++) {
        int32_t before;
        uint32_t enc;
        ui_setup();
        enc = ui_goto(K[k].id);
        before = fm_param_info_get(K[k].id)->max / 2;   /* away from the ends (an ENV size's is 0) */
        play_set_param(&ui_p, K[k].id, before);
        ui_event(&ui_u, UI_EV_ENC | enc, -1, 0);
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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 3, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), before + 1200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -2, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), before + 1000);
}

/* At the end of its range a value stays put (no wrap); an ENV size stops at 0 and at its top. */
TT_TEST(ui_knob_clamps_at_the_ends_of_the_range)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | ui_goto(FM_PARAM_FILTER_RESONANCE), 120, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), 1000);
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 500);
    ui_event(&ui_u, UI_EV_ENC | ui_goto(FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -200, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), 0);
    ui_event(&ui_u, UI_EV_ENC | ui_goto(FM_PARAM_FILTER_ENV_AMOUNT_CENTS), 200, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), 16400);
}

/* PLAY's knobs are the performance four (interaction.md 6, PLAY): K1 52 CUTOFF, K2 53 RES, K3 55 ENV,
 * K4 94 RELEASE, as their pages turn them; the screen stays PLAY. */
TT_TEST(ui_play_knobs_are_the_performance_four)
{
    static const uint8_t NUM[4] = {52, 53, 55, 94};
    uint32_t k;
    ui_setup();
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    for (k = 0; k < 4u; k++)
        TT_CHECK_EQ(ui_page_slot(&ui_u, k)->num, NUM[k]);
    play_set_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), 6100);
    play_set_param(&ui_p, FM_PARAM_AMP_RELEASE_MS, 13);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 10);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_AMP_RELEASE_MS), 15);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
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

/* RES 0..100 (10 per mille a step), ENV its size in semitones (58 MODE has the sign), KEY a
 * percentage. */
TT_TEST(ui_reso_env_and_key_read_in_their_units)
{
    TT_CHECK(ui_shows(FM_PARAM_FILTER_RESONANCE, 340, "34"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_RESONANCE, 1000, "100"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400, "24 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0, "0 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -9600, "96 st"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_KEY_TRACK, 500, "50 %"));
}

/* The knob meter, an 8-segment ladder (bit k: segment k lit, left to right): a range lights from
 * the left; ENV, bipolar, lights out from the middle. */
TT_TEST(ui_meter_lights_the_value)
{
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_RESONANCE, 340), 0x07u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_KEY_TRACK, 650), 0x0Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_CUTOFF_CENTS, 9300), 0x1Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_RESONANCE, 0), 0u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400), 0x10u);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -16400), 0x0Fu);
    TT_CHECK_EQ(ui_meter(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0), 0u);
}

/* What to redraw: the page in full when it opens; after a turn, that knob's column and the focus
 * row (the focus moves to the knob turned: the column it left too), and the curve only for CUTOFF
 * and RES. Turns closer than UI_FRAME_MS to the last redraw wait for the next one. */
TT_TEST(ui_redraws_only_what_a_turn_changed)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    TT_CHECK_EQ(ui_take(&ui_u, 0) & UI_D_FULL, UI_D_FULL);
    TT_CHECK_EQ(ui_take(&ui_u, 100), 0u);       /* nothing changed */
    TT_CHECK_EQ(ui_u.focus, 1u);                /* CUTOFF, as the page opens */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 200);
    TT_CHECK_EQ(ui_take(&ui_u, 200), UI_D_COL(1) | UI_D_FOCUS | UI_D_SLOT | UI_D_FOOT);  /* (the first
                                                 * edit: the footer's `*`) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 210);
    TT_CHECK_EQ(ui_take(&ui_u, 210), 0u);       /* within a frame of the last redraw */
    TT_CHECK_EQ(ui_take(&ui_u, 200 + UI_FRAME_MS), UI_D_COL(1) | UI_D_COL(0) | UI_D_FOCUS);
    TT_CHECK_EQ(ui_u.focus, 0u);
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
static int32_t ui_curve_at(int32_t cut, int32_t res, uint32_t i)
{
    ui_curve c;
    ui_curve_begin(&c, cut, res, 226);
    return ui_curve_db(&c, i);
}

TT_TEST(ui_curve_follows_the_mockup)
{
    TT_CHECK_NEAR(ui_curve_at(9300, 340, 0), 0, 5);
    TT_CHECK_NEAR(ui_curve_at(9300, 340, 120), 39, 5);    /* the resonant bump */
    TT_CHECK_NEAR(ui_curve_at(9300, 340, 150), -38, 5);
    TT_CHECK_NEAR(ui_curve_at(9300, 340, 200), -302, 5);
    TT_CHECK_NEAR(ui_curve_at(7800, 200, 140), -114, 5);
    TT_CHECK_NEAR(ui_curve_at(14400, 1000, 200), 89, 5);
    TT_CHECK_NEAR(ui_curve_at(0, 0, 50), -231, 5);
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
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400, "24", "st"));
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_KEY_TRACK, 500, "50", "%"));
}

/* The header's page strip: 18 segments with 2 px gaps share 230 px from x 5 (the leftover px one
 * each to the first); 5A is the 8th (interaction.md's page order). */
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
    TT_CHECK_EQ(UI_PAGES, 18u);
    TT_CHECK(strcmp(UI_PAGE_INFO[UI_PAGE_5A].name, "5A") == 0);
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

/* EDIT held 400 ms or more is compare, never a page change; under 400 ms it is a tap. */
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

/* A section button's LED is lit while one of its pages shows and glows otherwise (ENV: 4A). */
TT_TEST(ui_section_leds_light_their_page)
{
    ui_setup();
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_LFO), UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_FX), UI_LED_GLOW);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), UI_LED_LIT);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), UI_LED_GLOW);
}

/* EDIT held 400 ms or more is compare: while it is held the stored patch sounds and the page shows its
 * values; the release puts the edit buffer back exactly, and the page stays (interaction.md). */
TT_TEST(ui_edit_hold_compares_with_the_stored_patch)
{
    uint32_t enc;
    int32_t stored, edited;
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    stored = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 0);
    edited = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    TT_CHECK(edited != stored);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    (void)ui_take(&ui_u, 1399);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), edited);   /* not yet a hold */
    (void)ui_take(&ui_u, 1400);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), stored);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), stored);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2000);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), edited);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), edited);
}

/* With no edits, an EDIT hold shows NO EDITS and changes nothing; its release doesn't toggle the page. */
TT_TEST(ui_edit_hold_with_no_edits_says_so)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    TT_CHECK(strcmp(ui_banner(&ui_u), "NO EDITS") == 0);
    TT_CHECK(!ui_p.comparing);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 1600);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(!play_edited(&ui_p));
}

/* During compare the knobs are ignored and a turn shows the compare message; PRESETS loads nothing
 * either, so the edit buffer comes back whole. */
TT_TEST(ui_compare_ignores_knobs_and_presets)
{
    uint32_t enc;
    int32_t edited;
    const patch *t;
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 0);
    edited = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    t = play_patch(&ui_p);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    ui_event(&ui_u, UI_EV_ENC | enc, 3, 1500);
    TT_CHECK(strcmp(ui_banner(&ui_u), "COMPARE: RELEASE EDIT") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1600);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1700);
    TT_CHECK(play_patch(&ui_p) == t);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2000);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), edited);
}

/* The EDIT LED blinks during compare (on and off, 250 ms each), and is lit again after it: the edit
 * buffer is still edited. */
TT_TEST(ui_edit_led_blinks_during_compare)
{
    uint32_t enc, a, b;
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    a = ui_led(&ui_u, UI_BTN_EDIT);
    (void)ui_take(&ui_u, 1650);
    b = ui_led(&ui_u, UI_BTN_EDIT);
    TT_CHECK((a == UI_LED_LIT && b == UI_LED_OFF) || (a == UI_LED_OFF && b == UI_LED_LIT));
    (void)ui_take(&ui_u, 1900);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_EDIT), a);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2000);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_EDIT), UI_LED_LIT);
}

/* A param set during compare (a MIDI CC, later) goes to the edit buffer, not the stored patch: the
 * stored value keeps sounding and showing until EDIT is released, and then the engine has the new value
 * (interaction.md acceptance 59). */
TT_TEST(ui_param_set_during_compare_reaches_the_edit_buffer)
{
    uint32_t enc;
    int32_t stored;
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    stored = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    TT_CHECK(play_edited(&ui_p));               /* the edit buffer is still edited */
    play_set_param(&ui_p, FM_PARAM_FILTER_RESONANCE, 777);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), stored);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), stored);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2000);
    ui_settle();
    TT_CHECK_EQ(fm_engine_get_param(&ui_p.engine, FM_PARAM_FILTER_RESONANCE), 777);
}

/* Compare redraws the header (its COMPARE marker) and the values, when it begins and when it ends. */
TT_TEST(ui_compare_redraws_the_header_and_values)
{
    uint32_t enc, want = UI_D_HEAD | UI_D_FOCUS | UI_D_COL(0) | UI_D_COL(3);
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 0);
    (void)ui_take(&ui_u, 500);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1000);
    TT_CHECK_EQ(ui_take(&ui_u, 1400) & want, want);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2000);
    TT_CHECK_EQ(ui_take(&ui_u, 2100) & want, want);
}

/* The footer shows `*` while the patch is edited (visual.md): the first edit marks the footer for a
 * redraw, and so does turning back to the stored value. */
TT_TEST(ui_edited_state_change_redraws_the_footer)
{
    uint32_t enc;
    ui_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    (void)ui_take(&ui_u, 100);
    ui_event(&ui_u, UI_EV_ENC | enc, 1, 200);
    TT_CHECK(ui_take(&ui_u, 300) & UI_D_FOOT);
    ui_event(&ui_u, UI_EV_ENC | enc, 1, 400);
    TT_CHECK(!(ui_take(&ui_u, 500) & UI_D_FOOT));   /* still edited: no change */
    ui_event(&ui_u, UI_EV_ENC | enc, -2, 600);
    TT_CHECK(ui_take(&ui_u, 700) & UI_D_FOOT);
}

/* The EDIT LED glows while the sound is the stored patch and is lit once it is edited; turning
 * back to the stored value glows again. */
TT_TEST(ui_edit_led_shows_an_edited_patch)
{
    ui_setup();
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_EDIT), UI_LED_GLOW);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_EDIT), UI_LED_LIT);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -1, 0);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_EDIT), UI_LED_GLOW);
}

/* draw(full) draws everything: what was pending is drawn with it, not again after. */
TT_TEST(ui_full_redraw_takes_the_pending_bits)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    TT_CHECK_EQ(ui_take_full(&ui_u, 100), UI_D_ALL);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), 0u);
}

/* The focus row's value: the number in D24 and the full unit after it. */
TT_TEST(ui_focus_value_splits_number_and_unit)
{
    char n[UI_VALUE_LEN], u[UI_VALUE_LEN];
    ui_value(FM_PARAM_FILTER_CUTOFF_CENTS, 9100, n, u);
    TT_CHECK(strcmp(n, "1.57") == 0 && strcmp(u, "kHz") == 0);
    ui_value(FM_PARAM_FILTER_RESONANCE, 340, n, u);
    TT_CHECK(strcmp(n, "34") == 0 && strcmp(u, "") == 0);
}

/* A redraw goes one band per draw() call, so the main loop gets back to the keys between bands:
 * the frame first, then the focus row, the curve and the columns, each handed out once. */
TT_TEST(ui_redraw_goes_one_band_at_a_time)
{
    uint32_t pending = UI_D_ALL, k;
    TT_CHECK_EQ(ui_next_band(&pending), UI_D_FULL);
    TT_CHECK_EQ(ui_next_band(&pending), UI_D_FOCUS);
    TT_CHECK_EQ(ui_next_band(&pending), UI_D_SLOT);
    for (k = 0; k < 4u; k++)
        TT_CHECK_EQ(ui_next_band(&pending), UI_D_COL(k));
    TT_CHECK_EQ(ui_next_band(&pending), 0u);
}

/* HOME held 1.5 s opens the host's picker, which sends HOME's release first: a release 400 ms or
 * more after the press is not a tap, so the page stays. */
TT_TEST(ui_home_hold_keeps_the_page)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_HOME, 0, 1000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_HOME, 0, 2500);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
}

/* ENV again while 5A shows steps to 5B (its ring), a new page to draw; there KNOB1 is 55 ENV. ENV on
 * 5B goes on to 8A. */
TT_TEST(ui_env_on_5a_steps_to_5b)
{
    int32_t cut;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    (void)ui_take_full(&ui_u, 100);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 200);
    TT_CHECK(ui_take(&ui_u, 1000) & UI_D_FULL);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 55);
    cut = play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS);
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1100);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), 100);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), cut);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 1200);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 81);    /* then on round its ring: 8A ENV-1 */
}

/* PLAY shows no last note any more (visual.md: its columns are the performance knobs): a note redraws
 * nothing. A knob turn there shows that knob in the focus row for 3 s (interaction.md 6, PLAY), then
 * the name again; a CUTOFF turn doesn't redraw the slot (PLAY's is the badge, not the curve). */
TT_TEST(ui_play_turn_shows_its_readout_for_3_s)
{
    uint32_t d;
    ui_setup();
    (void)ui_take_full(&ui_u, 0);
    play_key_down(&ui_p, 3, 100);
    TT_CHECK_EQ(ui_take(&ui_u, 100), 0u);
    TT_CHECK(!ui_play_readout(&ui_u, 100));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1000);
    d = ui_take(&ui_u, 1000);
    TT_CHECK(d & UI_D_FOCUS);
    TT_CHECK(!(d & UI_D_SLOT));
    TT_CHECK(ui_play_readout(&ui_u, 3999));
    TT_CHECK_EQ(ui_take(&ui_u, 3999), 0u);
    TT_CHECK(!ui_play_readout(&ui_u, 4000));
    TT_CHECK_EQ(ui_take(&ui_u, 4000), UI_D_FOCUS | UI_D_COL(0));  /* the name again, and the frame off */
}

/* HB_F_SCREEN hides the host's line, so the header shows the octave: a change redraws it, once. */
TT_TEST(ui_octave_change_marks_the_header)
{
    ui_setup();
    (void)ui_take_full(&ui_u, 0);
    play_octave(&ui_p, 1);
    TT_CHECK_EQ(ui_take(&ui_u, 100), UI_D_HEAD);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), 0u);
}

/* The header's octave, as the host's line reads it: OCT -2 .. OCT +2, OCT +0 at the center. */
TT_TEST(ui_octave_label_reads_like_the_hosts)
{
    char s[UI_VALUE_LEN];
    ui_octave_label(-2, s);
    TT_CHECK(strcmp(s, "OCT -2") == 0);
    ui_octave_label(0, s);
    TT_CHECK(strcmp(s, "OCT +0") == 0);
    ui_octave_label(1, s);
    TT_CHECK(strcmp(s, "OCT +1") == 0);
}

/* A slot's number as the screen shows it: two digits. */
TT_TEST(ui_slot_number_is_two_digits)
{
    char s[3];
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);       /* 5A */
    ui_slot_number(ui_page_slot(&ui_u, 0), s);
    TT_CHECK(strcmp(s, "51") == 0);
}

/* PRESETS, with no edits: each click loads the next or previous patch at once (interaction.md). */
TT_TEST(ui_presets_click_loads_the_next_patch)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 0);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK + 1u]);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -2, 100);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK - 1u]);
}

/* A fast turn (3 or more clicks in one event) moves 4 patches, as interaction.md says. */
TT_TEST(ui_presets_fast_turn_moves_four)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 3, 0);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK + 4u]);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -5, 100);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK]);
}

/* Patch numbers wrap: before the first is the last, after the last the first. */
TT_TEST(ui_presets_wrap)
{
    ui_setup();
    play_load_patch(&ui_p, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -1, 0);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_COUNT - 1u]);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 4, 100);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[3]);
}

/* With edits, the first PRESETS event loads nothing, whatever its clicks, and warns. */
TT_TEST(ui_presets_with_edits_warns_first)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 3, 100);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK]);
    TT_CHECK(play_edited(&ui_p));
    TT_CHECK(strcmp(ui_banner(&ui_u), "EDITED: TURN AGAIN TO DISCARD") == 0);
}

/* After the warning, a later event the same way within 2 s discards the edits and loads, moving
 * by that event's clicks. */
TT_TEST(ui_presets_again_discards_and_loads)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 2, 99 + UI_DISCARD_MS);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK + 2u]);
    TT_CHECK(!play_edited(&ui_p));
}

/* The discard loads at once and takes the warning down with it: no "TURN AGAIN" over the new patch. */
TT_TEST(ui_presets_discard_takes_the_warning_down)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100);
    (void)ui_take_full(&ui_u, 100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 200);
    TT_CHECK(strcmp(ui_banner(&ui_u), "") == 0);
    TT_CHECK_EQ(ui_take(&ui_u, 200) & UI_D_FOOT, UI_D_FOOT);
}

/* The discard window is the warning's time on screen: once it is gone, PRESETS warns again, so a
 * turn never discards edits with nothing on screen. */
TT_TEST(ui_presets_discard_window_is_the_warning_on_screen)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100);
    (void)ui_take(&ui_u, 100 + UI_BANNER_MS);
    TT_CHECK(strcmp(ui_banner(&ui_u), "") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100 + UI_BANNER_MS);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK]);
    TT_CHECK(play_edited(&ui_p));
    TT_CHECK(strcmp(ui_banner(&ui_u), "") != 0);
}

/* A PRESETS event of 0 clicks is no turn: it loads nothing, so no notes are let go. */
TT_TEST(ui_presets_zero_clicks_does_nothing)
{
    uint32_t head;
    ui_setup();
    head = ui_p.head;
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 0, 100);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK]);
    TT_CHECK_EQ(ui_p.loads, 0u);
    TT_CHECK_EQ(ui_p.head, head);
}

/* Anything else between the two events cancels the warning: the next PRESETS warns again. */
TT_TEST(ui_presets_warning_cancels_on_anything_else)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 300);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[PATCH_SOUNDTRACK]);
}

/* The banner is in the footer: showing it redraws the footer, and 1.5 s later it is gone and the
 * footer redraws again. */
TT_TEST(ui_banner_shows_for_one_and_a_half_seconds)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 0);
    (void)ui_take_full(&ui_u, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1000);
    TT_CHECK_EQ(ui_take(&ui_u, 1000) & UI_D_FOOT, UI_D_FOOT);
    TT_CHECK_EQ(ui_take(&ui_u, 2499), 0u);
    TT_CHECK_EQ(ui_take(&ui_u, 2500), UI_D_FOOT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "") == 0);
}

/* A load keeps the page and the focus, and redraws everything below the header: the values, the
 * curve and the footer's patch. */
TT_TEST(ui_presets_load_redraws_the_values_and_keeps_the_page)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -1, 0);           /* back to the stored value */
    (void)ui_take_full(&ui_u, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1000);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_u.focus, 2u);
}

/* The patch's number: the factory tones are the starter group, P1 .. P32 (interaction.md). */
TT_TEST(ui_patch_number_is_p_and_its_place)
{
    char s[UI_VALUE_LEN];
    ui_patch_number(&PATCHES[0], s);
    TT_CHECK(strcmp(s, "P1") == 0);
    ui_patch_number(&PATCHES[PATCH_SOUNDTRACK], s);
    TT_CHECK(strcmp(s, "P17") == 0);
}

/* The VCF pages carry the JX-8P's own (PG-800) numbers in its panel order: 51 HPF, 52 CUTOFF,
 * 53 RES, 54 LFO, then 55 ENV, 56 KEY, 57 DYNAMICS, 58 MODE. */
TT_TEST(ui_vcf_slots_are_the_jx_panels)
{
    static const struct { uint8_t num; uint16_t id; } WANT[8] = {
        {51, FM_PARAM_HIGHPASS_STEP}, {52, FM_PARAM_FILTER_CUTOFF_CENTS},
        {53, FM_PARAM_FILTER_RESONANCE}, {54, 0},
        {55, FM_PARAM_FILTER_ENV_AMOUNT_CENTS}, {56, FM_PARAM_FILTER_KEY_TRACK},
        {57, FM_PARAM_FILTER_ENV_DYNA}, {58, FM_PARAM_FILTER_ENV_SOURCE},
    };
    uint32_t i;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    for (i = 0; i < 8u; i++) {
        const ui_slot *sl;
        if (i == 4u)
            ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
        sl = ui_page_slot(&ui_u, i % 4u);
        TT_CHECK_EQ(sl->num, WANT[i].num);
        TT_CHECK(WANT[i].id ? sl->kind == UI_LIVE && sl->id == WANT[i].id : sl->kind == UI_PENDING);
    }
}

/* HPF turns in the JX-8P's order (#138): BOOST (engine step 4), FLAT (0), 240 Hz (2), 560 Hz (3),
 * and stays at either end. Engine step 1 (100 Hz) has no JX position. BOOST is offered once the
 * engine has step 4 (#136); until then FLAT is the bottom. */
TT_TEST(ui_hpf_turns_in_jx_order)
{
    static const int32_t UP[] = {0, 2, 3, 3};
    int32_t boost = fm_param_info_get(FM_PARAM_HIGHPASS_STEP)->max >= 4 ? 4 : 0;
    const int32_t DOWN[] = {0, boost, boost};
    uint32_t enc, i;
    ui_setup();
    enc = ui_goto(FM_PARAM_HIGHPASS_STEP);
    TT_CHECK(enc != 0);
    play_set_param(&ui_p, FM_PARAM_HIGHPASS_STEP, 0);
    for (i = 1; i < 4; i++) {
        ui_event(&ui_u, UI_EV_ENC | enc, 1, 0);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_HIGHPASS_STEP), UP[i]);
    }
    play_set_param(&ui_p, FM_PARAM_HIGHPASS_STEP, 0);
    for (i = 1; i < 3; i++) {
        ui_event(&ui_u, UI_EV_ENC | enc, -1, 0);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_HIGHPASS_STEP), DOWN[i]);
    }
}

/* Membrane's meter is one bar in a 44 px track (visual.md): a range fills from the left in
 * proportion, rounded; a bipolar param fills out from the middle, either way. */
TT_TEST(ui_meter_bar_spans_the_value)
{
    int32_t a, b;
    ui_meter_bar(FM_PARAM_FILTER_RESONANCE, 500, 44, &a, &b);
    TT_CHECK(a == 0 && b == 22);
    ui_meter_bar(FM_PARAM_FILTER_RESONANCE, 1000, 44, &a, &b);
    TT_CHECK(a == 0 && b == 44);
    ui_meter_bar(FM_PARAM_FILTER_RESONANCE, 0, 44, &a, &b);
    TT_CHECK(a == 0 && b == 0);
    ui_meter_bar(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 8200, 44, &a, &b);    /* +82 of +-164 st */
    TT_CHECK(a == 22 && b == 33);
    ui_meter_bar(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -16400, 44, &a, &b);
    TT_CHECK(a == 0 && b == 22);
}

/* HPF reads as the JX-8P's positions (#138): BOOST, FLAT, 240 Hz, 560 Hz; engine step 1, which
 * only MIDI or an old patch sets, as 100 Hz. The column drops Hz as CUTOFF's does. */
TT_TEST(ui_hpf_reads_as_its_positions)
{
    char n[UI_VALUE_LEN], u[UI_VALUE_LEN];
    TT_CHECK(ui_shows(FM_PARAM_HIGHPASS_STEP, 4, "BOOST"));
    TT_CHECK(ui_shows(FM_PARAM_HIGHPASS_STEP, 0, "FLAT"));
    TT_CHECK(ui_shows(FM_PARAM_HIGHPASS_STEP, 2, "240 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_HIGHPASS_STEP, 3, "560 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_HIGHPASS_STEP, 1, "100 Hz"));
    ui_column(FM_PARAM_HIGHPASS_STEP, 2, n, u);
    TT_CHECK(strcmp(n, "240") == 0 && strcmp(u, "") == 0);
}

/* HPF at engine step 1 (100 Hz, between FLAT and 240 Hz; only MIDI or an old patch sets it): the
 * first click snaps to the JX position the turn points to (interaction.md, units). */
TT_TEST(ui_hpf_off_the_panel_snaps_the_way_it_turns)
{
    uint32_t enc;
    ui_setup();
    enc = ui_goto(FM_PARAM_HIGHPASS_STEP);
    play_set_param(&ui_p, FM_PARAM_HIGHPASS_STEP, 1);
    ui_event(&ui_u, UI_EV_ENC | enc, 1, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_HIGHPASS_STEP), 2);
    play_set_param(&ui_p, FM_PARAM_HIGHPASS_STEP, 1);
    ui_event(&ui_u, UI_EV_ENC | enc, -1, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_HIGHPASS_STEP), 0);
}

/* A choice's meter is one segment per knob position (visual.md), lit at the value's position: HPF
 * BOOST, FLAT, 240, 560 are 0..3 of 4; the engine's 100 Hz sits on FLAT's segment. */
TT_TEST(ui_choice_meter_lights_its_position)
{
    const ui_slot *hpf;
    uint32_t n;
    ui_setup();
    hpf = ui_page_slot(&ui_u, ui_goto(FM_PARAM_HIGHPASS_STEP) - UI_ENC_KNOB1);
    TT_CHECK_EQ(ui_choice_pos(hpf, 4, &n), 0u);
    TT_CHECK_EQ(n, 4u);
    TT_CHECK_EQ(ui_choice_pos(hpf, 0, &n), 1u);
    TT_CHECK_EQ(ui_choice_pos(hpf, 1, &n), 1u);
    TT_CHECK_EQ(ui_choice_pos(hpf, 3, &n), 3u);
}

/* Under turns every frame, each frame's bands are all drawn before the next frame's start, so the
 * columns aren't starved by the focus row and curve a turn marks again (#47, slice 1b): CUTOFF turns
 * every 33 ms, draw() comes every 10 ms, and the turned column still draws. */
TT_TEST(ui_steady_turns_still_reach_the_columns)
{
    uint32_t pending = 0, ms, cols = 0, busy = 0;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    for (ms = 0; ms < 1000u; ms += 10u) {
        uint32_t b;
        if (ms % 30u == 0)
            ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, ms % 60u ? 1 : -1, ms);
        if (busy) {                             /* the curve's other parts (draw.c: 3 a curve) */
            busy--;
            continue;
        }
        b = ui_band(&ui_u, &pending, ms);
        cols += b == UI_D_COL(1);
        busy = b == UI_D_SLOT ? 2u : 0u;
    }
    printf("    column drawn %u times in 1 s\n", (unsigned)cols);
    TT_CHECK(cols >= 10u);
}

/* SEL from PLAY opens 1A, DCO-1 (interaction.md 3, 4): 11 RANGE and 12 WAVE on KNOB1 and KNOB2. */
TT_TEST(ui_sel_opens_dco1)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 100);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 11);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->id, FM_PARAM_OSC1_OCTAVE);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 1)->num, 12);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 1)->id, FM_PARAM_OSC1_WAVE);
}

/* A section button is lit while a page of its ring shows (interaction.md 4): on 1A SEL is lit and
 * ENV glows; on 5A, the other way round. */
TT_TEST(ui_section_led_follows_the_ring)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 100);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), UI_LED_LIT);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), UI_LED_GLOW);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 200);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), UI_LED_LIT);
}

/* DCO values (interaction.md 3, 1x/2x): RANGE in feet, WAVE by name, LFO depth in cents. */
TT_TEST(ui_dco_values_read_as_the_panel)
{
    TT_CHECK(ui_shows(FM_PARAM_OSC1_OCTAVE, 0, "8'"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_OCTAVE, -2, "32'"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_OCTAVE, 2, "2'"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_WAVE, 0, "SAW"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_WAVE, 3, "NOISE"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_PITCH_CENTS, 450, "450 ct"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_PITCH_OSC2_CENTS, -1, "D1"));    /* follows DCO-1's depth */
    TT_CHECK(ui_shows(FM_PARAM_LFO_PITCH_OSC2_CENTS, 90, "90 ct"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC1_CENTS, 1240, "12.4 st"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC2_CENTS, -3200, "32.0 st"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC1_CENTS, 0, "0.0 st"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_FINE_CENTS, 5, "+5 ct"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_FINE_CENTS, -100, "-100 ct"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_COARSE_SEMIS, 7, "+7 st"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_PULSE_WIDTH_PCT, 50, "50 %"));
}

/* SEL steps its ring (interaction.md 4): 1A, 1B, 2A, 2B, 3A, 4A, 4B, and back to 1A. */
TT_TEST(ui_sel_walks_the_dco_pages)
{
    static const uint8_t WANT[8] = {11, 15, 21, 25, 31, 41, 45, 11};
    uint32_t i;
    ui_setup();
    for (i = 0; i < 8u; i++) {
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000 * i);
        ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1000 * i + 100);
        TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, WANT[i]);
    }
}

/* 23 X-MOD is the JX-8P's CROSS MOD on one knob (interaction.md 17a): OFF, SYNC 1, SYNC 2, X-MOD,
 * setting osc_sync and osc_ring as tools/jx8p.py maps it, and staying at either end. */
TT_TEST(ui_xmod_steps_the_cross_mod_positions)
{
    static const struct { int32_t sync, ring; const char *shown; } WANT[5] = {
        {0, 0, "OFF"}, {1, 0, "SYNC 1"}, {1, 1, "SYNC 2"}, {0, 1, "X-MOD"}, {0, 1, "X-MOD"},
    };
    uint32_t i;
    ui_setup();
    for (i = 0; i < 3u; i++) {                  /* SEL: 1A, 1B, 2A */
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000 * i);
        ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1000 * i + 100);
    }
    play_set_param(&ui_p, FM_PARAM_OSC_SYNC, 0);
    play_set_param(&ui_p, FM_PARAM_OSC_RING, 0);
    for (i = 0; i < 5u; i++) {
        char s[UI_VALUE_LEN];
        if (i)
            ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 5000);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_OSC_SYNC), WANT[i].sync);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_OSC_RING), WANT[i].ring);
        ui_format(ui_page_slot(&ui_u, 2)->id, ui_slot_value(&ui_u, ui_page_slot(&ui_u, 2)), s);
        TT_CHECK(strcmp(s, WANT[i].shown) == 0);
    }
}

/* The DCO pages' slot draws the waveform, two cycles across n columns (levels -1000..+1000): a saw
 * rises through each cycle, a pulse is high for its width, a square half the cycle, noise anywhere
 * in range. */
TT_TEST(ui_wave_picture_follows_the_wave)
{
    uint32_t i, high = 0;
    TT_CHECK(ui_wave_level(FM_WAVE_SAW, 50, 0, 200) < ui_wave_level(FM_WAVE_SAW, 50, 90, 200));
    TT_CHECK(ui_wave_level(FM_WAVE_SAW, 50, 99, 200) > ui_wave_level(FM_WAVE_SAW, 50, 100, 200));
    for (i = 0; i < 200u; i++)
        high += ui_wave_level(FM_WAVE_PULSE, 25, i, 200) > 0;
    TT_CHECK(high >= 48u && high <= 52u);
    for (high = 0, i = 0; i < 200u; i++)
        high += ui_wave_level(FM_WAVE_SQUARE, 25, i, 200) > 0;
    TT_CHECK(high >= 98u && high <= 102u);
    for (i = 0; i < 200u; i++) {
        int32_t v = ui_wave_level(FM_WAVE_NOISE, 50, i, 200);
        TT_CHECK(v >= -1000 && v <= 1000);
    }
}

/* On a DCO page the slot is the waveform: a WAVE turn redraws it, a RANGE turn doesn't. */
TT_TEST(ui_wave_turn_redraws_the_picture)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 100);
    (void)ui_take_full(&ui_u, 100);
    play_set_param(&ui_p, FM_PARAM_OSC1_WAVE, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 200);
    TT_CHECK(ui_take(&ui_u, 300) & UI_D_SLOT);
    play_set_param(&ui_p, FM_PARAM_OSC1_OCTAVE, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 400);
    TT_CHECK(!(ui_take(&ui_u, 500) & UI_D_SLOT));
}

/* 17 WIDTH is inactive unless DCO-1's WAVE is PULSE (interaction.md 3, 1x), and 28 likewise for
 * DCO-2; other slots are active. */
TT_TEST(ui_width_is_inactive_off_pulse)
{
    const ui_slot *w1 = &UI_SLOTS[4 * 1 + 2], *w2 = &UI_SLOTS[4 * 3 + 3];
    ui_setup();
    TT_CHECK_EQ(w1->num, 17);
    TT_CHECK_EQ(w2->num, 28);
    play_set_param(&ui_p, FM_PARAM_OSC1_WAVE, FM_WAVE_SAW);
    play_set_param(&ui_p, FM_PARAM_OSC2_WAVE, FM_WAVE_PULSE);
    TT_CHECK(!ui_slot_active(&ui_u, w1));
    TT_CHECK(ui_slot_active(&ui_u, w2));
    play_set_param(&ui_p, FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE);
    TT_CHECK(ui_slot_active(&ui_u, w1));
    TT_CHECK(ui_slot_active(&ui_u, &UI_SLOTS[0]));
}

/* 14 LFO (interaction.md 3): 1 to 99 ct a click of 1 (fast 10), from 100 a click of 10 (fast 50). */
TT_TEST(ui_lfo_depth_steps_finer_below_100)
{
    static const struct { int32_t from, clicks, to; } C[] = {
        {98, 1, 99}, {99, 1, 100}, {100, 1, 110}, {110, -1, 100}, {100, -1, 99}, {50, 3, 80}, {100, 3, 250},
    };
    uint32_t i;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 100);
    for (i = 0; i < sizeof C / sizeof C[0]; i++) {
        play_set_param(&ui_p, FM_PARAM_LFO_PITCH_CENTS, C[i].from);
        ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, C[i].clicks, 200);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_LFO_PITCH_CENTS), C[i].to);
    }
}

/* SEL's ring goes on to 3A DCO ENV (interaction.md 3, 3x): 31 DYNA (OFF, 1, 2, 3), 32 MODE,
 * 33 XMOD AMT (osc_xmod_permille, 0..100 %). */
static void ui_sel_to(uint32_t times)
{
    uint32_t i;
    for (i = 0; i < times; i++) {
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000 * i);
        ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1000 * i + 100);
    }
}

TT_TEST(ui_sel_reaches_dco_env)
{
    ui_setup();
    ui_sel_to(5);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 31);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->id, FM_PARAM_PITCH_ENV_DYNA);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 2)->id, FM_PARAM_OSC_XMOD);
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_DYNA, 0, "OFF"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_DYNA, 2, "2"));
    TT_CHECK(ui_shows(FM_PARAM_OSC_XMOD, 450, "45 %"));
}

/* 32 MODE is the JX-8P's DCO ENV MODE on one knob (interaction.md 66): ENV2 INV, ENV2, ENV1 INV,
 * ENV1, setting pitch_env_source and the sign of both DCO envelope depths (15, 27), which show their
 * size. With the depths at 0 the knob still holds INV, and a depth turned up then goes negative. */
TT_TEST(ui_dco_env_mode_sets_source_and_sign)
{
    static const struct { const char *shown; int32_t src, sign; } WANT[5] = {
        {"ENV1", 0, 1}, {"ENV1 INV", 0, -1}, {"ENV2", 1, 1}, {"ENV2 INV", 1, -1}, {"ENV2 INV", 1, -1},
    };
    const ui_slot *mode;
    uint32_t i;
    char s[UI_VALUE_LEN];
    ui_setup();
    ui_sel_to(5);
    mode = ui_page_slot(&ui_u, 1);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_SOURCE, 0);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS, 500);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC2_CENTS, 300);
    for (i = 0; i < 5u; i++) {
        if (i)
            ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -1, 9000);
        ui_format(mode->id, ui_slot_value(&ui_u, mode), s);
        TT_CHECK(strcmp(s, WANT[i].shown) == 0);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_PITCH_ENV_SOURCE), WANT[i].src);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS), 500 * WANT[i].sign);
        TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_PITCH_ENV_OSC2_CENTS), 300 * WANT[i].sign);
    }
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC1_CENTS, -500, "5.0 st"));    /* a size: MODE has the sign */
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS, 0);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC2_CENTS, 0);
    ui_format(mode->id, ui_slot_value(&ui_u, mode), s);
    TT_CHECK(strcmp(s, "ENV2 INV") == 0);       /* held with the depths at 0 */
    ui_sel_to(4);                               /* (on through 4A, 4B and 1A to 1B: 15 ENV on KNOB1) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 9000);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS), -10);
}

/* An ENV MODE held at INV with its depths at 0 is the edit's, not the next patch's: a PRESETS load
 * reads MODE from the loaded patch (its depths' signs). */
TT_TEST(ui_mode_memory_goes_with_a_load)
{
    const ui_slot *mode;
    ui_setup();
    ui_sel_to(5);
    mode = ui_page_slot(&ui_u, 1);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS, 0);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC2_CENTS, 0);
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_SOURCE, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -1, 9000);       /* ENV1 INV */
    TT_CHECK_EQ(ui_slot_value(&ui_u, mode) >> 1, 1);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 9100);       /* edited: warns */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 9200);       /* loads */
    play_set_param(&ui_p, FM_PARAM_PITCH_ENV_OSC1_CENTS, 0);
    TT_CHECK_EQ(ui_slot_value(&ui_u, mode) >> 1, 0);
}

/* MIXER (interaction.md 3, 4x): 4A 41 DCO-1, 42 DCO-2 (levels 0..100), 43 ENV (its size), 44 DYNA;
 * 4B 45 MODE, the mix envelope's source and sign. SEL's ring: ... 3A, 4A, 4B, then 1A. */
TT_TEST(ui_mixer_pages)
{
    const ui_slot *mode;
    ui_setup();
    ui_sel_to(6);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 41);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->id, FM_PARAM_OSC1_LEVEL);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 2)->id, FM_PARAM_OSC_MIX_ENV);
    TT_CHECK(ui_shows(FM_PARAM_OSC2_LEVEL, 670, "67"));
    TT_CHECK(ui_shows(FM_PARAM_OSC_MIX_ENV, -500, "50"));
    play_set_param(&ui_p, FM_PARAM_OSC_MIX_ENV, 400);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 9000);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_OSC_MIX_ENV), 410);
    ui_sel_to(1);
    mode = ui_page_slot(&ui_u, 0);
    TT_CHECK_EQ(mode->num, 45);
    play_set_param(&ui_p, FM_PARAM_OSC_MIX_ENV_SOURCE, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -1, 9000);       /* ENV1 INV */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_OSC_MIX_ENV), -410);
    ui_sel_to(1);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 11);
}

/* 5B (interaction.md 3, 5x): 55 ENV is the cutoff envelope's size, 57 DYNA the VCF DYNAMICS,
 * 58 MODE its source and sign. */
TT_TEST(ui_vcf_env_mode_and_dynamics)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 100);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 2)->kind, UI_LIVE);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 2)->id, FM_PARAM_FILTER_ENV_DYNA);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 3)->kind, UI_LIVE);
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, -2400, "24 st"));
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_SOURCE, 1);
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 200);         /* ENV2 -> ENV1 INV */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_SOURCE), 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -2400);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 300);         /* 55: the size grows, still inverted */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -2500);
}

/* An ENV size's bar fills from the left over 0..its top (the sign is MODE's): 55 ENV at 82 of 164 st
 * is half the track, whichever way the envelope goes. */
TT_TEST(ui_size_bar_fills_from_the_left)
{
    const ui_slot *env;
    int32_t a, b;
    ui_setup();
    env = ui_page_slot(&ui_u, ui_goto(FM_PARAM_FILTER_ENV_AMOUNT_CENTS) - UI_ENC_KNOB1);
    ui_slot_bar(env, 8200, 44, &a, &b);
    TT_CHECK(a == 0 && b == 22);
    ui_slot_bar(env, 16400, 44, &a, &b);
    TT_CHECK(a == 0 && b == 44);
}

/* A page opens with the focus on knob 2 (visual.md), or on its first live slot when knob 2's is
 * empty: 4B has only 45 MODE (knob 1), so the focus row never shows an empty slot. */
TT_TEST(ui_focus_opens_on_a_live_slot)
{
    ui_setup();
    ui_sel_to(7);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 45);
    TT_CHECK_EQ(ui_page_slot(&ui_u, ui_u.focus)->kind, UI_LIVE);
}

/* Every slot in the table (interaction.md 3): an empty one has no label; the rest have a label of
 * at most 7 characters (a column's width) and a full one that fits the focus row after the section's
 * name (20 characters of Terminus, 163 px), and their number is the page's section
 * and the knob (page 5A's KNOB3 is 53); a live one names an engine param. */
TT_TEST(ui_map_table_is_well_formed)
{
    uint32_t p, k;
    for (p = 0; p < UI_PAGES; p++)
        for (k = 0; k < 4u; k++) {
            const ui_slot *sl = &UI_SLOTS[4u * p + k];
            uint32_t sec = (uint32_t)(UI_PAGE_INFO[p].name[0] - '0');
            uint32_t knob = (UI_PAGE_INFO[p].name[1] == 'B' ? 5u : 1u) + k;
            if (sl->kind == UI_RESERVED) {
                TT_CHECK(sl->label == 0);
                continue;
            }
            if (strlen(sl->label) > 7u || sl->num != sec * 10u + knob)
                printf("    slot %u: \"%s\" on page %s knob %u\n", sl->num, sl->label, UI_PAGE_INFO[p].name, k + 1u);
            TT_CHECK(strlen(sl->label) <= 7u && sl->full != 0);
            if (strlen(UI_PAGE_INFO[p].section) + 1u + strlen(sl->full) > 20u)  /* the focus row: 163 px */
                printf("    slot %u: \"%s %s\" is over 20 characters\n", sl->num, UI_PAGE_INFO[p].section, sl->full);
            TT_CHECK(strlen(UI_PAGE_INFO[p].section) + 1u + strlen(sl->full) <= 20u);
            TT_CHECK_EQ(sl->num, sec * 10u + knob);
            TT_CHECK(sl->kind != UI_LIVE || fm_param_info_get(sl->id) != 0);
        }
}

/* An inverted ENV MODE fits a column (56 px, 7 characters of Terminus) as E1 INV / E2 INV; the focus
 * row has it in full. */
TT_TEST(ui_mode_column_is_short)
{
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_ENV_SOURCE, 3, "E2 INV", ""));
    TT_CHECK(ui_column_shows(FM_PARAM_OSC_MIX_ENV_SOURCE, 2, "E1 INV", ""));
    TT_CHECK(ui_column_shows(FM_PARAM_PITCH_ENV_SOURCE, 1, "ENV2", ""));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_SOURCE, 3, "ENV2 INV"));
}

/* Whatever the buttons, keys and knobs do, an edit page is a real one, and the focus row shows no
 * slot (focus 4) or a slot with a label (#177 faulted on the FM-1 while SEL changed pages): SEL, ENV, EDIT and
 * HOME taps, holds and every knob, many times over, in a fixed pseudo-random order. */
TT_TEST(ui_hammered_buttons_keep_a_live_page_and_focus)
{
    static const uint32_t EV[] = {UI_EV_BTN_DOWN | UI_BTN_SEL, UI_EV_BTN_UP | UI_BTN_SEL,
                                  UI_EV_BTN_DOWN | UI_BTN_ENV, UI_EV_BTN_UP | UI_BTN_ENV,
                                  UI_EV_BTN_DOWN | UI_BTN_EDIT, UI_EV_BTN_UP | UI_BTN_EDIT,
                                  UI_EV_BTN_DOWN | UI_BTN_HOME, UI_EV_BTN_UP | UI_BTN_HOME,
                                  UI_EV_ENC | UI_ENC_KNOB1, UI_EV_ENC | UI_ENC_KNOB2,
                                  UI_EV_ENC | UI_ENC_KNOB3, UI_EV_ENC | UI_ENC_KNOB4,
                                  UI_EV_BTN_DOWN | UI_BTN_LFO, UI_EV_BTN_DOWN | UI_BTN_FX,
                                  UI_EV_ENC | UI_ENC_SELECT, UI_EV_ENC | UI_ENC_ALGORITHM,
                                  UI_EV_ENC | UI_ENC_PRESETS};
    uint32_t i, r = 12345u, ms = 0, bad = 0;
    ui_setup();
    for (i = 0; i < 20000u; i++) {
        const ui_slot *f;
        r = r * 1103515245u + 12345u;
        ms += (r >> 8) % 600u;                  /* taps and holds */
        uint32_t ev = EV[(r >> 16) % (sizeof EV / sizeof EV[0])];
        if ((r >> 12) % 4u == 0)                /* a key now and then (the number jump's keypad) */
            (void)ui_key(&ui_u, (r >> 20) % PLAY_KEYS, (int)((r >> 6) & 1u), ms);
        else
            ui_event(&ui_u, ev, (int32_t)((r >> 4) % 7u) - 3, ms);
        (void)ui_take(&ui_u, ms);               /* (the jump screen's 400 ms timer) */
        if (ev == (UI_EV_BTN_UP | UI_BTN_SEL) && (r >> 12) % 4u != 0 && ui_screen(&ui_u) == UI_SCREEN_JUMP)
            bad++;                              /* its screen never outlives SEL */
        if (ui_u.state != UI_EDIT)
            continue;
        if (ui_u.focus == 4u)                   /* no focus (a number with no parameter): an empty row */
            continue;
        f = ui_page_slot(&ui_u, ui_u.focus);
        if (ui_u.page >= UI_PAGES || ui_u.focus > 3u || f->kind == UI_RESERVED || !f->label || !f->full)
            bad++;
    }
    printf("    %u bad states in 20000 events\n", (unsigned)bad);
    TT_CHECK_EQ(bad, 0u);
}

/* The time table (interaction.md, units): 0 to 32000 ms in about 115 entries, 0, 1, 2, 3, 4, 5, 6,
 * 8, 10, 12, 15, 18, 20, 22 at the start, then about 7 % apart (none above 10 %: whole ms, 5 ms
 * steps from 100 ms, 50 from 1 s, 500 from 10 s); a click moves
 * one entry, a fast one four; a time not in the table snaps the way the knob turns. */
TT_TEST(ui_time_table_steps_about_7_percent)
{
    static const int32_t START[14] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 20, 22};
    uint32_t i, n = ui_time_count();
    TT_CHECK(n >= 110u && n <= 125u);
    for (i = 0; i < 14u; i++)
        TT_CHECK_EQ(ui_time_at(i), START[i]);
    TT_CHECK_EQ(ui_time_at(n - 1u), 32000);
    for (i = 14; i < n; i++)
        TT_CHECK(ui_time_at(i) > ui_time_at(i - 1u) && ui_time_at(i) * 100 <= ui_time_at(i - 1u) * 110);
    TT_CHECK_EQ(ui_time_step(13, 1), 15);
    TT_CHECK_EQ(ui_time_step(13, -1), 12);
    TT_CHECK_EQ(ui_time_step(12, 1), 15);
    TT_CHECK_EQ(ui_time_step(12, 4), ui_time_at(13));
    TT_CHECK_EQ(ui_time_step(32000, 1), 32000);
    TT_CHECK_EQ(ui_time_step(0, -1), 0);
}

/* Times read as interaction.md has them: 0 ms .. 999 ms, 1.00 s .. 9.99 s, 10.0 s .. 32.0 s; sustain
 * 0..100; ENV KEY OFF, 1, 2, 3 (260 per mille a position). */
TT_TEST(ui_envelope_values_read_as_the_map)
{
    TT_CHECK(ui_shows(FM_PARAM_AMP_ATTACK_MS, 0, "0 ms"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_DECAY_MS, 480, "480 ms"));
    TT_CHECK(ui_shows(FM_PARAM_AMP_RELEASE_MS, 1250, "1.25 s"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_ATTACK_MS, 12500, "12.5 s"));
    TT_CHECK(ui_shows(FM_PARAM_AMP_DECAY_MS, 32000, "32.0 s"));
    TT_CHECK(ui_shows(FM_PARAM_AMP_SUSTAIN, 700, "70"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_SUSTAIN, 0, "0"));
    TT_CHECK(ui_shows(FM_PARAM_AMP_KEY_FOLLOW, 0, "OFF"));
    TT_CHECK(ui_shows(FM_PARAM_FILTER_ENV_KEY_FOLLOW, 520, "2"));
}

/* ENV's ring (interaction.md 4): 5A, 5B, 8A ENV-1, 8B, 9A ENV-2, 9B, around. FX's: 6A VCA, 6B. A time
 * knob walks the time table: 81 ATTACK from 13 ms clicks to 15, a fast turn moves 4 entries a click. */
TT_TEST(ui_env_and_fx_rings_and_time_knobs)
{
    static const uint8_t ENVR[7] = {51, 55, 81, 85, 91, 95, 51};
    uint32_t i;
    ui_setup();
    for (i = 0; i < 7u; i++) {
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 100 * i);
        TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, ENVR[i]);
    }
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 1000);     /* 5B */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 1100);     /* 8A */
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_ATTACK_MS, 13);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1200);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_ATTACK_MS), 15);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 3, 1300);        /* fast: 12 entries */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_ATTACK_MS), ui_time_at(10 + 12));
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_FX, 0, 1400);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 61);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_FX), UI_LED_LIT);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_FX, 0, 1500);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 65);
}

/* 65 VEL (ours) does something only while 63 DYNA is OFF (amp_dyna 0: the amp_velocity curve), so it
 * is inactive otherwise. Its value reads in %. */
TT_TEST(ui_vel_is_inactive_with_dynamics)
{
    const ui_slot *vel = &UI_SLOTS[4 * 10];
    ui_setup();
    TT_CHECK_EQ(vel->num, 65);
    play_set_param(&ui_p, FM_PARAM_AMP_DYNA, 2);
    TT_CHECK(!ui_slot_active(&ui_u, vel));
    play_set_param(&ui_p, FM_PARAM_AMP_DYNA, 0);
    TT_CHECK(ui_slot_active(&ui_u, vel));
    TT_CHECK(ui_shows(FM_PARAM_AMP_VELOCITY, 40, "40 %"));
}

/* The ENV pages' slot draws the ADSR as render.py's env_points does (per mille knob positions a, d, r,
 * sustain s): from 0 up to full at the end of the attack, down toward the sustain, held, then
 * released to 0. */
TT_TEST(ui_adsr_picture_follows_the_envelope)
{
    const uint32_t n = 200;
    uint32_t i, peak = 0, at = 0;
    TT_CHECK_EQ(ui_adsr_level(200, 500, 700, 400, 0, n), 0);
    for (i = 0; i < n; i++)
        if ((uint32_t)ui_adsr_level(200, 500, 700, 400, i, n) > peak)
            peak = (uint32_t)ui_adsr_level(200, 500, 700, 400, i, n), at = i;
    TT_CHECK(peak >= 980u);
    TT_CHECK(at > 5u && at < 60u);             /* the attack's end, the first part of the window */
    TT_CHECK(ui_adsr_level(200, 500, 700, 400, at + 40u, n) < 900);   /* decaying */
    TT_CHECK(ui_adsr_level(200, 500, 700, 400, n - 1u, n) < 50);      /* released */
    for (i = 0; i < n; i++) {                   /* the hold: at the sustain somewhere */
        int32_t v = ui_adsr_level(200, 500, 700, 400, i, n);
        if (v >= 690 && v <= 710)
            break;
    }
    TT_CHECK(i < n);
}

/* A time's bar is its place in the time table (where the knob is), not a share of 32 s: the middle
 * entry fills half the track. */
TT_TEST(ui_time_bar_follows_the_table)
{
    const ui_slot *att = &UI_SLOTS[4 * 12];
    int32_t a, b, mid = ui_time_at((ui_time_count() - 1u) / 2u);
    TT_CHECK_EQ(att->num, 81);
    ui_slot_bar(att, mid, 44, &a, &b);
    TT_CHECK(a == 0 && b >= 21 && b <= 23);
    ui_slot_bar(att, 32000, 44, &a, &b);
    TT_CHECK(a == 0 && b == 44);
    ui_slot_bar(att, 0, 44, &a, &b);
    TT_CHECK(a == 0 && b == 0);
}

/* On an ENV page the slot is its ADSR: a SUSTAIN turn redraws it. */
TT_TEST(ui_env_turn_redraws_the_adsr)
{
    uint32_t i;
    ui_setup();
    for (i = 0; i < 3u; i++)                    /* 5A, 5B, 8A */
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 100 * i);
    (void)ui_take_full(&ui_u, 300);
    play_set_param(&ui_p, FM_PARAM_FILTER_ENV_SUSTAIN, 500);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 400);
    TT_CHECK(ui_take(&ui_u, 500) & UI_D_SLOT);
}

/* LFO's ring (interaction.md 4): 7A LFO, 0A AFTERTOUCH (04 GLIDE live), 0B CONTROL. 7A: 71 WAVE in
 * panel order (RANDOM, SQUARE, TRI, and our SAW), 72 DELAY a time, 73 RATE in Hz, 74 PW in %. */
TT_TEST(ui_lfo_ring_and_page)
{
    static const uint8_t WANT[4] = {71, 1, 5, 71};
    uint32_t i;
    ui_setup();
    for (i = 0; i < 4u; i++) {
        ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_LFO, 0, 100 * i);
        TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, WANT[i]);
    }
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_LFO), UI_LED_LIT);
    TT_CHECK(ui_shows(FM_PARAM_LFO_WAVE, FM_LFO_RANDOM, "RANDOM"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_WAVE, FM_LFO_TRIANGLE, "TRI"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_DELAY_MS, 1500, "1.50 s"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_RATE_CENTIHZ, 585, "5.85 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_RATE_CENTIHZ, 3000, "30.0 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_RATE_CENTIHZ, 1, "0.01 Hz"));
    TT_CHECK(ui_shows(FM_PARAM_LFO_PW_PCT, 20, "20 %"));
    play_set_param(&ui_p, FM_PARAM_LFO_WAVE, FM_LFO_RANDOM);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 900);         /* RANDOM -> SQUARE */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_LFO_WAVE), FM_LFO_SQUARE);
}

/* The LFO rate table (interaction.md, units): 0.01 Hz to 30.0 Hz, every 0.01 Hz to 0.17 Hz, then
 * about 6 % apart (none above 8 %); 73 RATE walks it a click an entry, a fast turn four. */
TT_TEST(ui_rate_table_steps_about_6_percent)
{
    uint32_t i, n = ui_rate_count();
    TT_CHECK(n >= 100u && n <= 120u);
    for (i = 0; i < 17u; i++)
        TT_CHECK_EQ(ui_rate_at(i), (int32_t)i + 1);
    TT_CHECK_EQ(ui_rate_at(n - 1u), 3000);
    for (i = 17; i < n; i++)
        TT_CHECK(ui_rate_at(i) > ui_rate_at(i - 1u) && ui_rate_at(i) * 100 <= ui_rate_at(i - 1u) * 108);
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_LFO, 0, 0);
    play_set_param(&ui_p, FM_PARAM_LFO_RATE_CENTIHZ, 3);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 100);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_LFO_RATE_CENTIHZ), 4);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 3, 200);         /* fast: 12 entries */
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_LFO_RATE_CENTIHZ), 16);
    play_set_param(&ui_p, FM_PARAM_LFO_RATE_CENTIHZ, 3000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 300);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_LFO_RATE_CENTIHZ), 3000);
    {                                           /* its bar: the place in the table */
        int32_t a, b;
        ui_slot_bar(ui_page_slot(&ui_u, 2), ui_rate_at((n - 1u) / 2u), 44, &a, &b);
        TT_CHECK(a == 0 && b >= 21 && b <= 23);
    }
}

/* 08 DETUNE is the unison spread: inactive in POLY (interaction.md 3, 0x). */
TT_TEST(ui_detune_is_inactive_in_poly)
{
    const ui_slot *det = &UI_SLOTS[4 * 17 + 3];
    ui_setup();
    TT_CHECK_EQ(det->num, 8);
    play_set_param(&ui_p, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY);
    TT_CHECK(!ui_slot_active(&ui_u, det));
    play_set_param(&ui_p, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    TT_CHECK(ui_slot_active(&ui_u, det));
}

/* A VOICE turn redraws 08 DETUNE's column too: DETUNE's activity follows it, on the same page. */
TT_TEST(ui_voice_turn_redraws_detune)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_LFO, 0, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_LFO, 0, 100);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_LFO, 0, 200);      /* 0B */
    TT_CHECK_EQ(ui_page_slot(&ui_u, 2)->num, 7);
    (void)ui_take_full(&ui_u, 300);
    play_set_param(&ui_p, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_POLY);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 400);
    TT_CHECK(ui_take(&ui_u, 500) & UI_D_COL(3));
}

/* 7A's slot draws the LFO's wave, two cycles (levels -1000..+1000): a triangle peaks mid-cycle, a saw
 * rises, a square is high half the cycle, random holds a level through each cycle. */
TT_TEST(ui_lfo_picture_follows_the_wave)
{
    uint32_t i, high = 0;
    TT_CHECK(ui_lfo_level(FM_LFO_TRIANGLE, 50, 200) > 900);
    TT_CHECK(ui_lfo_level(FM_LFO_TRIANGLE, 0, 200) < -900);
    TT_CHECK(ui_lfo_level(FM_LFO_SAW, 10, 200) < ui_lfo_level(FM_LFO_SAW, 90, 200));
    for (i = 0; i < 200u; i++)
        high += ui_lfo_level(FM_LFO_SQUARE, i, 200) > 0;
    TT_CHECK(high >= 98u && high <= 102u);
    TT_CHECK_EQ(ui_lfo_level(FM_LFO_RANDOM, 5, 200), ui_lfo_level(FM_LFO_RANDOM, 90, 200));
    TT_CHECK(ui_lfo_level(FM_LFO_RANDOM, 90, 200) != ui_lfo_level(FM_LFO_RANDOM, 110, 200));
}

/* SELECT walks the pages in number order and wraps (interaction.md, page order): 5A +1 is 5B, 0B +1
 * is 1A, 1A -1 is 0B. ALGORITHM jumps by section to its page A: 4B +1 is 5A, 9A +1 is 0A, 0B +1 is 1A,
 * 1B -1 is 0A. In PLAY both go to EDIT, from the last edit page. */
static uint32_t ui_num0(void) { return ui_page_slot(&ui_u, 0)->num; }

TT_TEST(ui_select_and_algorithm_walk_pages_and_sections)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 1, 0);           /* PLAY: on from 5A */
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_num0(), 55);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, -1, 100);
    TT_CHECK_EQ(ui_num0(), 51);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 10, 200);        /* 5A + 10: 0B */
    TT_CHECK_EQ(ui_num0(), 5);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 1, 300);
    TT_CHECK_EQ(ui_num0(), 11);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, -1, 400);
    TT_CHECK_EQ(ui_num0(), 5);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 500);      /* 0B: on to 1A */
    TT_CHECK_EQ(ui_num0(), 11);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 1, 600);         /* 1B */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, -1, 700);     /* 1B: back to 0A */
    TT_CHECK_EQ(ui_num0(), 1);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, -1, 800);     /* 0A: back to 9A */
    TT_CHECK_EQ(ui_num0(), 91);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 900);
    TT_CHECK_EQ(ui_num0(), 1);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 5, 1000);     /* 0 + 5 sections: 5A */
    TT_CHECK_EQ(ui_num0(), 51);
}

/* The number jump (interaction.md 4, test 5): hold SEL and tap two white keys, F3..A4 being the
 * digits 1..9 and 0: G3 then A3 (2, 3) opens 2A with 23 X-MOD the focus. The keys don't sound. */
#define UI_KEY_G3 2u
#define UI_KEY_A3 4u
TT_TEST(ui_number_jump_goes_to_the_parameter)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    TT_CHECK(ui_key(&ui_u, UI_KEY_G3, 1, 100));
    TT_CHECK(ui_key(&ui_u, UI_KEY_G3, 0, 150));
    TT_CHECK(ui_key(&ui_u, UI_KEY_A3, 1, 200));
    TT_CHECK(ui_key(&ui_u, UI_KEY_A3, 0, 250));
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 900);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_page_slot(&ui_u, ui_u.focus)->num, 23);
    TT_CHECK(!ui_key(&ui_u, UI_KEY_G3, 1, 1000));             /* SEL let go: keys play again */
}

/* Releasing SEL after one digit (C4, 5) opens 5A with no focus; number 18 (reserved) opens 1B with no
 * focus and the banner NO PARAMETER (interaction.md tests 6, 7). A turn then focuses its knob. */
#define UI_KEY_F3 0u
#define UI_KEY_C4 7u
#define UI_KEY_F4 12u
TT_TEST(ui_number_jump_without_a_parameter)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    (void)ui_key(&ui_u, UI_KEY_C4, 1, 100);
    (void)ui_key(&ui_u, UI_KEY_C4, 0, 150);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 900);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 51);
    TT_CHECK(ui_page_slot(&ui_u, ui_u.focus) == 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 2000);
    (void)ui_key(&ui_u, UI_KEY_F3, 1, 2100);
    (void)ui_key(&ui_u, UI_KEY_F4, 1, 2200);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 15);              /* 1B */
    TT_CHECK(ui_page_slot(&ui_u, ui_u.focus) == 0);
    TT_CHECK(strcmp(ui_banner(&ui_u), "NO PARAMETER") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 2300);
    TT_CHECK_EQ(ui_page_slot(&ui_u, ui_u.focus)->num, 16);
}

/* A key held before SEL went down still gets its note-off (its up isn't the keypad's), and a SEL tap
 * with a key pressed in it is a jump, not SEL's ring step. */
TT_TEST(ui_keypad_keeps_held_notes_and_taps_apart)
{
    ui_setup();
    TT_CHECK(!ui_key(&ui_u, UI_KEY_C4, 1, 0));                /* plays */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 100);
    TT_CHECK(!ui_key(&ui_u, UI_KEY_C4, 0, 150));              /* its up plays too: the note ends */
    TT_CHECK(ui_key(&ui_u, UI_KEY_G3, 1, 200));
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 300);        /* under 400 ms, but a key came */
    TT_CHECK(ui_key(&ui_u, UI_KEY_G3, 0, 350));               /* its up is still the keypad's */
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 21);             /* digit 2: 2A, not SEL's 1A */
}

/* While SEL is held the ten digit keys glow as a keypad (interaction.md 4); otherwise the keypad
 * lights none (the keys' own LEDs are the module's). */
TT_TEST(ui_keypad_glows_while_sel_is_held)
{
    uint32_t k, glow = 0;
    ui_setup();
    for (k = 0; k < PLAY_KEYS; k++)
        glow += ui_keypad_led(&ui_u, k) == UI_LED_GLOW;
    TT_CHECK_EQ(glow, 0u);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    for (glow = 0, k = 0; k < PLAY_KEYS; k++)
        glow += ui_keypad_led(&ui_u, k) == UI_LED_GLOW;
    TT_CHECK_EQ(glow, 10u);
    TT_CHECK_EQ(ui_keypad_led(&ui_u, UI_KEY_F3), UI_LED_GLOW);
    TT_CHECK_EQ(ui_keypad_led(&ui_u, 1u), UI_LED_OFF);         /* F#3, a black key */
}

/* The number jump's screen (interaction.md 6): it comes up once SEL has been held 400 ms (a tap never
 * shows it) or at once with a digit; it goes on a finished jump (the page shows, SEL still down) and
 * on SEL up. A knob turn under it leaves it up. */
TT_TEST(ui_jump_screen_comes_and_goes)
{
    ui_setup();
    (void)ui_take_full(&ui_u, 0);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PLAY);
    (void)ui_take(&ui_u, 1399);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PLAY);
    TT_CHECK(ui_take(&ui_u, 1400) & UI_D_FULL);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 1450);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1500);
    TT_CHECK(ui_take(&ui_u, 1600) & UI_D_FULL);
    TT_CHECK(ui_screen(&ui_u) != UI_SCREEN_JUMP);
    /* a digit at once, then the second: the page while SEL is still held, and no timer after */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 3000);
    (void)ui_key(&ui_u, UI_KEY_G3, 1, 3050);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    (void)ui_key(&ui_u, UI_KEY_A3, 1, 3100);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PAGE);
    (void)ui_take(&ui_u, 3600);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PAGE);
    TT_CHECK_EQ(ui_page_slot(&ui_u, ui_u.focus)->num, 23);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 3700);
    /* a tap: no screen at all */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 5000);
    (void)ui_take(&ui_u, 5100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 5200);
    TT_CHECK(ui_screen(&ui_u) != UI_SCREEN_JUMP);
}

/* The jump screen's number: the digits typed, then _ (interaction.md 6: "4_"); the LED window has _
 * as segment d. Each section has a name for its list. */
TT_TEST(ui_jump_digits_and_sections)
{
    char s[3];
    uint32_t i;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 0);
    ui_jump_digits(&ui_u, s);
    TT_CHECK(strcmp(s, "__") == 0);
    (void)ui_key(&ui_u, UI_KEY_C4, 1, 50);
    ui_jump_digits(&ui_u, s);
    TT_CHECK(strcmp(s, "5_") == 0);
    TT_CHECK_EQ(ui_seg7('_'), 0x08u);
    for (i = 0; i < 10u; i++)
        TT_CHECK(UI_SECTION_NAME[i] && strlen(UI_SECTION_NAME[i]) <= 12u);
    TT_CHECK(strcmp(UI_SECTION_NAME[5], "VCF") == 0);
}

/* PLAY's readout belongs to that visit: turn KNOB1 in PLAY, go to 5A and back within 3 s, and PLAY shows
 * the patch again, not a readout of a knob not turned there. */
TT_TEST(ui_play_readout_ends_with_the_visit)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1000);
    TT_CHECK(ui_play_readout(&ui_u, 1100));
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 1200);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 1500);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 1600);      /* a tap: PLAY again */
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    TT_CHECK(!ui_play_readout(&ui_u, 1700));
}

/* The same in PLAY: under the jump screen the performance knobs don't turn (Eric's choice (a)). */
TT_TEST(ui_jump_screen_holds_the_knobs_in_play)
{
    int32_t cut;
    ui_setup();
    cut = play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 3, 1500);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), cut);
    TT_CHECK(!ui_play_readout(&ui_u, 1600));
}

/* While the jump screen is up, the other controls do nothing (Eric, 2026-11-14: only the keypad's keys
 * and SEL's release act): in EDIT a knob, PRESETS, SELECT, ALGORITHM and the section buttons leave the
 * page, its values and the patch as they were. */
TT_TEST(ui_jump_screen_holds_the_controls_in_edit)
{
    static const uint32_t EV[] = {UI_EV_ENC | UI_ENC_KNOB2, UI_EV_ENC | UI_ENC_PRESETS, UI_EV_ENC | UI_ENC_SELECT,
                                  UI_EV_ENC | UI_ENC_ALGORITHM, UI_EV_BTN_DOWN | UI_BTN_ENV,
                                  UI_EV_BTN_DOWN | UI_BTN_FX, UI_EV_BTN_DOWN | UI_BTN_LFO};
    const patch *was;
    int32_t cut;
    uint32_t i;
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);        /* 5A */
    was = play_patch(&ui_p);
    cut = play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1000);
    (void)ui_take(&ui_u, 1400);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    for (i = 0; i < sizeof EV / sizeof EV[0]; i++)
        ui_event(&ui_u, EV[i], 2, 1500 + 10 * i);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_CUTOFF_CENTS), cut);
    TT_CHECK(play_patch(&ui_p) == was);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 2000);
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 51);             /* still 5A */
}

/* WCAG contrast of two RGB565 colors as the panel shows them (each channel's bits repeated into 8,
 * as docs/ui/mockups/render.py's q()), rounded to one decimal as visual.md gives its rules */
static double ui_lum(uint16_t c)
{
    uint32_t r = c >> 11, g = c >> 5 & 63u, b = c & 31u, i;
    double ch[3];
    ch[0] = (double)(r << 3 | r >> 2);
    ch[1] = (double)(g << 2 | g >> 4);
    ch[2] = (double)(b << 3 | b >> 2);
    for (i = 0; i < 3; i++) {
        double s = ch[i] / 255.0;
        ch[i] = s <= 0.03928 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}

static double ui_contrast(uint16_t a, uint16_t b)
{
    double la = ui_lum(a), lb = ui_lum(b);
    return floor(10.0 * (la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05)) + 0.5) / 10.0;
}

/* Membrane (visual.md's style table) meets its contrast rules: text at least 10.2:1 on every
 * surface, dim labels 4.9:1, each section color 4.7:1 on surface and 4.9:1 on bg. */
TT_TEST(ui_membrane_meets_its_contrast_rules)
{
    const ui_theme *t = &UI_MEMBRANE;
    const uint16_t surf[3] = {t->bg, t->surface, t->surface_hi};
    uint32_t i;
    for (i = 0; i < 3; i++) {
        TT_CHECK(ui_contrast(t->text, surf[i]) >= 10.2);
        TT_CHECK(ui_contrast(t->dim, surf[i]) >= 4.9);
    }
    for (i = 0; i < UI_SECTIONS; i++) {
        TT_CHECK(ui_contrast(t->section[i], t->surface) >= 4.7);
        TT_CHECK(ui_contrast(t->section[i], t->bg) >= 4.9);
    }
}

static void run_ui_tests(void)
{
    TT_RUN(ui_membrane_meets_its_contrast_rules);
    TT_RUN(ui_jump_screen_holds_the_controls_in_edit);
    TT_RUN(ui_jump_screen_holds_the_knobs_in_play);
    TT_RUN(ui_play_readout_ends_with_the_visit);
    TT_RUN(ui_jump_screen_comes_and_goes);
    TT_RUN(ui_jump_digits_and_sections);
    TT_RUN(ui_keypad_glows_while_sel_is_held);
    TT_RUN(ui_keypad_keeps_held_notes_and_taps_apart);
    TT_RUN(ui_number_jump_without_a_parameter);
    TT_RUN(ui_number_jump_goes_to_the_parameter);
    TT_RUN(ui_select_and_algorithm_walk_pages_and_sections);
    TT_RUN(ui_lfo_picture_follows_the_wave);
    TT_RUN(ui_voice_turn_redraws_detune);
    TT_RUN(ui_detune_is_inactive_in_poly);
    TT_RUN(ui_rate_table_steps_about_6_percent);
    TT_RUN(ui_lfo_ring_and_page);
    TT_RUN(ui_env_turn_redraws_the_adsr);
    TT_RUN(ui_time_bar_follows_the_table);
    TT_RUN(ui_adsr_picture_follows_the_envelope);
    TT_RUN(ui_vel_is_inactive_with_dynamics);
    TT_RUN(ui_env_and_fx_rings_and_time_knobs);
    TT_RUN(ui_envelope_values_read_as_the_map);
    TT_RUN(ui_time_table_steps_about_7_percent);
    TT_RUN(ui_mode_column_is_short);
    TT_RUN(ui_map_table_is_well_formed);
    TT_RUN(ui_focus_opens_on_a_live_slot);
    TT_RUN(ui_size_bar_fills_from_the_left);
    TT_RUN(ui_vcf_env_mode_and_dynamics);
    TT_RUN(ui_mixer_pages);
    TT_RUN(ui_mode_memory_goes_with_a_load);
    TT_RUN(ui_dco_env_mode_sets_source_and_sign);
    TT_RUN(ui_sel_reaches_dco_env);
    TT_RUN(ui_hammered_buttons_keep_a_live_page_and_focus);
    TT_RUN(ui_lfo_depth_steps_finer_below_100);
    TT_RUN(ui_width_is_inactive_off_pulse);
    TT_RUN(ui_wave_turn_redraws_the_picture);
    TT_RUN(ui_wave_picture_follows_the_wave);
    TT_RUN(ui_xmod_steps_the_cross_mod_positions);
    TT_RUN(ui_sel_walks_the_dco_pages);
    TT_RUN(ui_dco_values_read_as_the_panel);
    TT_RUN(ui_section_led_follows_the_ring);
    TT_RUN(ui_sel_opens_dco1);
    TT_RUN(ui_steady_turns_still_reach_the_columns);
    TT_RUN(ui_choice_meter_lights_its_position);
    TT_RUN(ui_hpf_off_the_panel_snaps_the_way_it_turns);
    TT_RUN(ui_hpf_reads_as_its_positions);
    TT_RUN(ui_meter_bar_spans_the_value);
    TT_RUN(ui_hpf_turns_in_jx_order);
    TT_RUN(ui_vcf_slots_are_the_jx_panels);
    TT_RUN(ui_patch_number_is_p_and_its_place);
    TT_RUN(ui_presets_load_redraws_the_values_and_keeps_the_page);
    TT_RUN(ui_presets_discard_takes_the_warning_down);
    TT_RUN(ui_presets_discard_window_is_the_warning_on_screen);
    TT_RUN(ui_presets_zero_clicks_does_nothing);
    TT_RUN(ui_banner_shows_for_one_and_a_half_seconds);
    TT_RUN(ui_presets_warning_cancels_on_anything_else);
    TT_RUN(ui_presets_again_discards_and_loads);
    TT_RUN(ui_presets_with_edits_warns_first);
    TT_RUN(ui_presets_wrap);
    TT_RUN(ui_presets_fast_turn_moves_four);
    TT_RUN(ui_presets_click_loads_the_next_patch);
    TT_RUN(ui_slot_number_is_two_digits);
    TT_RUN(ui_octave_label_reads_like_the_hosts);
    TT_RUN(ui_octave_change_marks_the_header);
    TT_RUN(ui_play_turn_shows_its_readout_for_3_s);
    TT_RUN(ui_env_on_5a_steps_to_5b);
    TT_RUN(ui_home_hold_keeps_the_page);
    TT_RUN(ui_redraw_goes_one_band_at_a_time);
    TT_RUN(ui_focus_value_splits_number_and_unit);
    TT_RUN(ui_full_redraw_takes_the_pending_bits);
    TT_RUN(ui_edit_led_shows_an_edited_patch);
    TT_RUN(ui_section_leds_light_their_page);
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
    TT_RUN(ui_play_knobs_are_the_performance_four);
    TT_RUN(ui_fast_turn_moves_the_fast_step_per_click);
    TT_RUN(ui_edit_hold_compares_with_the_stored_patch);
    TT_RUN(ui_edit_hold_with_no_edits_says_so);
    TT_RUN(ui_compare_ignores_knobs_and_presets);
    TT_RUN(ui_edit_led_blinks_during_compare);
    TT_RUN(ui_param_set_during_compare_reaches_the_edit_buffer);
    TT_RUN(ui_compare_redraws_the_header_and_values);
    TT_RUN(ui_edited_state_change_redraws_the_footer);
    TT_RUN(ui_vcf_page_knobs_move_their_params_one_step);
    TT_RUN(ui_vcf_knob3_click_moves_resonance);
}
