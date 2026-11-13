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
    for (n = 0; n < 4u; n++) {
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
        before = (fm_param_info_get(K[k].id)->min + fm_param_info_get(K[k].id)->max) / 2;
        play_set_param(&ui_p, K[k].id, before);         /* away from the ends: the patch may sit at one */
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

/* At the end of its range a value stays put (no wrap). */
TT_TEST(ui_knob_clamps_at_the_ends_of_the_range)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_ENC | ui_goto(FM_PARAM_FILTER_RESONANCE), 120, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), 1000);
    ui_event(&ui_u, UI_EV_ENC | ui_goto(FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -200, 0);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_ENV_AMOUNT_CENTS), -16400);
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
    TT_CHECK_EQ(ui_take(&ui_u, 200), UI_D_COL(1) | UI_D_FOCUS | UI_D_SLOT);
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
    TT_CHECK(ui_column_shows(FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400, "+24", "st"));
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
 * 5B goes back to 5A while the rest of its ring isn't built. */
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
    TT_CHECK_EQ(ui_page_slot(&ui_u, 0)->num, 51);
}

/* On PLAY a new last note marks the note's strip, coalesced like the other redraws, and only once. */
TT_TEST(ui_new_last_note_marks_the_note_strip)
{
    ui_setup();
    (void)ui_take_full(&ui_u, 0);
    play_key_down(&ui_p, 3, 100);
    TT_CHECK_EQ(ui_take(&ui_u, 10), 0u);       /* within the frame */
    TT_CHECK_EQ(ui_take(&ui_u, 100), UI_D_NOTE);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), 0u);
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
        {55, FM_PARAM_FILTER_ENV_AMOUNT_CENTS}, {56, FM_PARAM_FILTER_KEY_TRACK}, {57, 0}, {58, 0},
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
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC1_CENTS, 1240, "+12.4 st"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC2_CENTS, -3200, "-32.0 st"));
    TT_CHECK(ui_shows(FM_PARAM_PITCH_ENV_OSC1_CENTS, 0, "0.0 st"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_FINE_CENTS, 5, "+5 ct"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_FINE_CENTS, -100, "-100 ct"));
    TT_CHECK(ui_shows(FM_PARAM_OSC2_COARSE_SEMIS, 7, "+7 st"));
    TT_CHECK(ui_shows(FM_PARAM_OSC1_PULSE_WIDTH_PCT, 50, "50 %"));
}

/* SEL steps its ring (interaction.md 4): 1A, 1B, 2A, 2B, then pages not built yet are skipped
 * (3A, 4A, 4B) and it comes back to 1A. */
TT_TEST(ui_sel_walks_the_dco_pages)
{
    static const uint8_t WANT[5] = {11, 15, 21, 25, 11};
    uint32_t i;
    ui_setup();
    for (i = 0; i < 5u; i++) {
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
    TT_RUN(ui_new_last_note_marks_the_note_strip);
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
    TT_RUN(ui_knobs_do_nothing_in_play);
    TT_RUN(ui_fast_turn_moves_the_fast_step_per_click);
    TT_RUN(ui_vcf_page_knobs_move_their_params_one_step);
    TT_RUN(ui_vcf_knob3_click_moves_resonance);
}
