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
        before = (fm_param_info_get(K[k].id)->min + fm_param_info_get(K[k].id)->max) / 2;
        play_set_param(&ui_p, K[k].id, before);         /* away from the ends: the patch may sit at one */
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

/* ENV while 4A shows does nothing yet (stepping to 4B comes with #47): no redraw, the focus kept. */
TT_TEST(ui_env_on_4a_does_nothing)
{
    ui_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 0);
    (void)ui_take_full(&ui_u, 100);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 200);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), 0u);
    TT_CHECK_EQ(ui_u.focus, 3u);
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
    TT_CHECK(strcmp(s, "41") == 0);
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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, -1, 0);           /* back to the stored value */
    (void)ui_take_full(&ui_u, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1000);
    TT_CHECK_EQ(ui_take(&ui_u, 1000), (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_u.focus, 3u);
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
    TT_RUN(ui_env_on_4a_does_nothing);
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
    TT_RUN(ui_vcf_knob2_click_moves_resonance);
}
