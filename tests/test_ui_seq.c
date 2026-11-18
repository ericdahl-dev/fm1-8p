/* SEQ view (interaction.md 5, firmware/ui_seq.c): the sequencer on the panel (#39, S2). */
#include "../firmware/ui_seq.h"

/* A SEQ tap from PLAY or EDIT opens SEQ view; another goes back where it came from, on the same page. */
TT_TEST(seq_tap_opens_and_goes_back)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_SEQ);
    ui_tap(UI_BTN_SEQ, 1200);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    ui_tap(UI_BTN_ENV, 1400);                                 /* 5A */
    ui_tap(UI_BTN_SEQ, 1600);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    ui_tap(UI_BTN_SEQ, 1800);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_u.page, (uint32_t)UI_PAGE_5A);
}

/* PLAY starts and stops the sequencer in every state (the button table): on what was asked, so taps
 * alternate even before the interrupt has let a stop's notes go */
TT_TEST(seq_play_starts_and_stops_anywhere)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_PLAY, 1000);
    TT_CHECK(s->want);
    ui_tap(UI_BTN_PLAY, 1200);
    TT_CHECK(!s->want);
    ui_tap(UI_BTN_PLAY, 1400);
    TT_CHECK(s->want);
    ui_tap(UI_BTN_SEQ, 1600);                                 /* in SEQ view too */
    ui_tap(UI_BTN_PLAY, 1800);
    TT_CHECK(!s->want);
    ui_tap(UI_BTN_SEQ, 2000);
    ui_tap(UI_BTN_GLO, 2200);                                 /* and in GLO */
    ui_tap(UI_BTN_PLAY, 2400);
    TT_CHECK(s->want);
    TT_CHECK_EQ(ui_u.state, UI_GLO);
}

/* SELECT moves the cursor through the pattern's length, around; page S1 PATTERN's knobs set TEMPO
 * (1 BPM a click, 10 fast), LENGTH, SWING and GATE (interaction.md 5) */
TT_TEST(seq_select_and_the_pattern_page)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_u.seq_page, 0u);                          /* S1 PATTERN */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -12, 1100);    /* LENGTH 16 - 12 */
    TT_CHECK_EQ(seq_length(s), 4u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 5, 1200);     /* 0 + 5 in 4 steps: 1 */
    TT_CHECK_EQ(ui_u.seq_cursor, 1u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, -2, 1300);
    TT_CHECK_EQ(ui_u.seq_cursor, 3u);
    (void)ui_take(&ui_u, 1350);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1400);      /* 120.0 + 1 BPM */
    TT_CHECK_EQ(seq_tempo(s), SEQ_TEMPO_DEF + 10u);
    TT_CHECK(ui_take(&ui_u, 1450) & UI_D_FOOT);              /* (the footer has the tempo) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 3, 1500);      /* fast: 10 BPM a click */
    TT_CHECK_EQ(seq_tempo(s), SEQ_TEMPO_DEF + 10u + 300u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 100, 1600);    /* SWING to its top */
    TT_CHECK_EQ(s->edit.swing_pct, 75u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, -200, 1700);   /* GATE to its bottom */
    TT_CHECK_EQ(s->edit.gate_pct, (uint8_t)SEQ_GATE_MIN);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);                         /* (no page change, no patch edit) */
    TT_CHECK(!play_edited(&ui_p));
}

/* ALGORITHM walks the pages, around: S2 STEP's VEL sets the cursor step's velocity, S3 PLAY's
 * TRANSPOSE the transpose; their other knobs wait (S7, #42) */
TT_TEST(seq_step_and_play_pages)
{
    static const uint8_t C[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 2, C, 1, 100);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 2, 1100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1200);  /* S2 STEP */
    TT_CHECK_EQ(ui_u.seq_page, 1u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -2, 1300);
    TT_CHECK_EQ(seq_get_step(s, 2)->vel, 98u);
    TT_CHECK_EQ(seq_get_step(s, 2)->note[0], 60u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1400);      /* BEND (seq_s2_bend_sets_the_step_bend_in_percent) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1500);  /* S3 PLAY */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -2, 1600);
    TT_CHECK_EQ(seq_transpose(s), -2);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1700);  /* around */
    TT_CHECK_EQ(ui_u.seq_page, 0u);
}

/* SEQ view on S2 STEP with the cursor on step i, a C4 there (and an E4 on step 0, to stay as it is) */
static seq *s2_setup(uint32_t i)
{
    static const uint8_t C[1] = {60}, E[1] = {64};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 0, E, 1, 90);
    seq_set_step(s, i, C, 1, 100);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, (int32_t)i, 1100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1200);  /* S2 STEP */
    return s;
}

static int s2_detail_is(const char *want)
{
    char t[UI_SEQ_TEXT_LEN];
    ui_seq_detail(&ui_u, 1, t);
    if (strcmp(t, want) != 0) {
        printf("    BEND reads \"%s\", not \"%s\"\n", t, want);
        return 0;
    }
    return 1;
}

/* S2's BEND sets the cursor step's bend in 1 % steps of the bend range (10 % a fast turn), -100 % the
 * engine's lowest bend and +100 % its highest, 0 the center; the column reads the percent, the detail
 * line the semitones it gives at BEND RANGE now (#42, interaction.md 5) */
TT_TEST(seq_s2_bend_sets_the_step_bend_in_percent)
{
    char n[UI_VALUE_LEN], un[UI_VALUE_LEN];
    seq *s = s2_setup(3);
    play_set_param(&ui_p, FM_PARAM_BEND_RANGE, 2);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 1300);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 1310);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 2 * 8191 / 100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1320);      /* fast: 10 % a click, to 52 */
    ui_seq_column(&ui_u, 1, n, un);
    TT_CHECK(strcmp(n, "+52") == 0 && strcmp(un, "%") == 0);
    TT_CHECK(s2_detail_is("+52 % = +1.0 st"));
    play_set_param(&ui_p, FM_PARAM_BEND_RANGE, 12);           /* the semitones follow BEND RANGE */
    TT_CHECK(s2_detail_is("+52 % = +6.2 st"));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 9, 1330);      /* past the top: +100 % */
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 8191);
    TT_CHECK(s2_detail_is("+100 % = +12.0 st"));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -30, 1340);    /* past the bottom: -100 % */
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, -8192);
    play_set_param(&ui_p, FM_PARAM_BEND_RANGE, 2);
    TT_CHECK(s2_detail_is("-100 % = -2.0 st"));
    ui_seq_column(&ui_u, 1, n, un);
    TT_CHECK(strcmp(n, "-100") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 10, 1350);     /* -100 + 10 x 10: the center */
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 0);
    TT_CHECK(s2_detail_is("0 % = 0.0 st"));
    TT_CHECK_EQ(seq_get_step(s, 3)->note[0], 60u);            /* the notes and velocity stay */
    TT_CHECK_EQ(seq_get_step(s, 3)->vel, 100u);
}

/* S2's MOD sets the cursor step's mod wheel, 0..127 (10 a click fast), and keeps its bend (#42) */
TT_TEST(seq_s2_mod_sets_the_step_mod)
{
    char n[UI_VALUE_LEN], un[UI_VALUE_LEN];
    seq *s = s2_setup(3);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1300);      /* bend +50 %, to keep */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 1310);
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 1u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 5, 1320);      /* fast: 10 a click */
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 51u);
    ui_seq_column(&ui_u, 2, n, un);
    TT_CHECK(strcmp(n, "51") == 0 && un[0] == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 20, 1330);     /* past the top */
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 127u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -20, 1340);    /* past the bottom */
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 0u);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 50 * 8191 / 100);
    TT_CHECK_EQ(seq_get_step(s, 3)->note[0], 60u);
    TT_CHECK_EQ(seq_get_step(s, 3)->vel, 100u);
}

/* S2's MOVE and the LFO button, in REC too, switch the cursor step between JUMP and GLIDE, keeping its
 * bend and mod (#42, interaction.md 5's button table) */
TT_TEST(seq_s2_move_and_lfo_switch_jump_and_glide)
{
    char n[UI_VALUE_LEN], un[UI_VALUE_LEN];
    seq *s = s2_setup(3);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1300);      /* bend +50 % and mod 2, to keep */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 2, 1310);
    ui_seq_column(&ui_u, 3, n, un);
    TT_CHECK(strcmp(n, "JUMP") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 1320);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    ui_seq_column(&ui_u, 3, n, un);
    TT_CHECK(strcmp(n, "GLIDE") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 1330);      /* (two values: held at the end) */
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, -1, 1340);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 0u);
    ui_tap(UI_BTN_LFO, 1400);                                 /* LFO toggles it */
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    ui_tap(UI_BTN_LFO, 1500);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 0u);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);                          /* (and no LFO page: still SEQ view) */
    ui_tap(UI_BTN_REC, 1600);                                 /* in REC the same, and the cursor stays */
    TT_CHECK(ui_u.seq_rec);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_LFO), (uint32_t)UI_LED_GLOW);
    ui_tap(UI_BTN_LFO, 1700);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    TT_CHECK_EQ(ui_u.seq_cursor, 3u);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 50 * 8191 / 100);
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 2u);
    TT_CHECK_EQ(seq_get_step(s, 3)->note[0], 60u);
}

/* On a rest or a tie, S2 shows the step's bend, mod and move (what it keeps, #42) but marks them
 * inactive, and its knobs and LFO change nothing; on a note or chord they're active */
TT_TEST(seq_s2_on_a_rest_or_tie_shows_and_keeps)
{
    char n[UI_VALUE_LEN], un[UI_VALUE_LEN];
    seq *s = s2_setup(3);
    uint32_t k;
    seq_set_step_bend_mod(s, 3, 50 * 8191 / 100, 20, 1);
    for (k = 0; k < 4u; k++)
        TT_CHECK(ui_seq_active(&ui_u, k));
    seq_clear_step(s, 3);                                     /* a rest: it keeps them */
    for (k = 0; k < 4u; k++)
        TT_CHECK(!ui_seq_active(&ui_u, k));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1300);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 5, 1310);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, -1, 1320);
    ui_tap(UI_BTN_LFO, 1400);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 50 * 8191 / 100);
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 20u);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    ui_seq_column(&ui_u, 1, n, un);
    TT_CHECK(strcmp(n, "+50") == 0);
    ui_seq_column(&ui_u, 3, n, un);
    TT_CHECK(strcmp(n, "GLIDE") == 0);
    seq_toggle_tie(s, 3);                                     /* a tie: the same */
    TT_CHECK(!ui_seq_active(&ui_u, 1));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -5, 1500);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 50 * 8191 / 100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1600);  /* S3: the knobs there aren't the step's */
    TT_CHECK(ui_seq_active(&ui_u, 0));
}

/* S2's VEL changes only the cursor step's velocity: its notes, bend, mod and move stay, and so does
 * every other step (#42: a VEL turn once rewrote the step and centered its bend) */
TT_TEST(seq_s2_vel_keeps_the_step_and_the_others)
{
    seq *s = s2_setup(3);
    seq_step before[SEQ_STEPS];
    uint32_t i;
    seq_set_step_bend_mod(s, 0, -4000, 99, 0);
    seq_set_step_bend_mod(s, 3, 50 * 8191 / 100, 20, 1);
    for (i = 0; i < SEQ_STEPS; i++)
        before[i] = *seq_get_step(s, i);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -2, 1300);
    TT_CHECK_EQ(seq_get_step(s, 3)->vel, 98u);
    TT_CHECK_EQ(seq_get_step(s, 3)->bend, 50 * 8191 / 100);
    TT_CHECK_EQ(seq_get_step(s, 3)->mod, 20u);
    TT_CHECK_EQ(seq_get_step(s, 3)->glide, 1u);
    TT_CHECK_EQ(seq_get_step(s, 3)->n, 1u);
    TT_CHECK_EQ(seq_get_step(s, 3)->note[0], 60u);
    for (i = 0; i < SEQ_STEPS; i++)
        if (i != 3u)
            TT_CHECK(memcmp(seq_get_step(s, i), &before[i], sizeof before[i]) == 0);
}

/* Every S2 column shows the cursor step, so all four redraw when the cursor moves or the step turns into
 * a rest or a tie: SELECT, SEL, ENV, and in REC SEL, ENV and FX (the review of f5a1471) */
TT_TEST(seq_s2_columns_follow_the_cursor_step)
{
    const uint32_t ALL = UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3);
    (void)s2_setup(3);
    (void)ui_take_full(&ui_u, 1250);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 1, 1300);
    TT_CHECK_EQ(ui_take(&ui_u, 1310) & ALL, ALL);
    ui_tap(UI_BTN_SEL, 1400);
    TT_CHECK_EQ(ui_take(&ui_u, 1510) & ALL, ALL);
    ui_tap(UI_BTN_ENV, 1600);
    TT_CHECK_EQ(ui_take(&ui_u, 1710) & ALL, ALL);
    ui_tap(UI_BTN_REC, 1800);
    (void)ui_take_full(&ui_u, 1910);
    ui_tap(UI_BTN_SEL, 2000);
    TT_CHECK_EQ(ui_take(&ui_u, 2110) & ALL, ALL);
    ui_tap(UI_BTN_ENV, 2200);
    TT_CHECK_EQ(ui_take(&ui_u, 2310) & ALL, ALL);
    ui_tap(UI_BTN_FX, 2400);
    TT_CHECK_EQ(ui_take(&ui_u, 2510) & ALL, ALL);
}

/* The section buttons in SEQ view (not in REC): SEL clears the cursor step, held 1.5 s the whole
 * pattern; ENV toggles TIE on it. EDIT goes to the last edit page, HOME to PLAY, GLO to GLO and back
 * to SEQ view; PRESETS browses patterns there, never patches, and SAVE opens SAVE for the pattern. */
TT_TEST(seq_section_buttons_and_the_way_out)
{
    static const uint8_t C[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 0, C, 1, 100);
    seq_set_step(s, 5, C, 1, 100);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 5, 1100);
    ui_tap(UI_BTN_SEL, 1200);
    TT_CHECK_EQ(seq_get_step(s, 5)->kind, (uint8_t)SEQ_REST);
    TT_CHECK_EQ(seq_get_step(s, 0)->kind, (uint8_t)SEQ_NOTES_ON);
    ui_tap(UI_BTN_ENV, 1300);
    TT_CHECK_EQ(seq_get_step(s, 5)->kind, (uint8_t)SEQ_TIE);
    ui_tap(UI_BTN_ENV, 1400);
    TT_CHECK_EQ(seq_get_step(s, 5)->kind, (uint8_t)SEQ_REST);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 2000);   /* held 1.5 s: all of it */
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 3600);
    TT_CHECK_EQ(seq_get_step(s, 0)->kind, (uint8_t)SEQ_REST);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 3700);    /* a pattern, not a patch (edited: a warning) */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(!ui_u.warned);
    ui_tap(UI_BTN_SAVE, 3800);                  /* SAVE: the pattern's (test_ui_save.c) */
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK(ui_u.save_pattern);
    ui_tap(UI_BTN_HOME, 3850);                  /* canceled: back to SEQ view */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    ui_tap(UI_BTN_GLO, 3900);
    TT_CHECK_EQ(ui_u.state, UI_GLO);
    ui_tap(UI_BTN_GLO, 4000);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    ui_tap(UI_BTN_EDIT, 4100);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    ui_tap(UI_BTN_SEQ, 4200);
    ui_tap(UI_BTN_HOME, 4300);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
}

/* render audio until the sequencer's playhead is on step `want` (or a second has gone) */
static void seq_render_to(int32_t want)
{
    uint32_t i, k;
    for (k = 0; k < 400u && seq_step_now(play_seq(&ui_p)) != want; k++) {
        for (i = 0; i < 2 * PLAY_BLOCK; i++)
            ui_out[i] = 0;
        play_render(&ui_p, ui_out, PLAY_BLOCK);
    }
}

/* The button LEDs (interaction.md 5): SEQ lit in SEQ view and glowing elsewhere; PLAY glowing when
 * stopped, lit while playing with a dark flash on each beat's first step; REC (seq_rec_button_leds). In
 * SEQ view SEL (rest), ENV (tie) and LFO (MOVE, #42) glow, FX (BACK, in REC) is off. */
TT_TEST(seq_button_leds)
{
    ui_setup();
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEQ), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_PLAY), (uint32_t)UI_LED_GLOW);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEQ), (uint32_t)UI_LED_LIT);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_LFO), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_FX), (uint32_t)UI_LED_OFF);
    ui_tap(UI_BTN_PLAY, 1200);
    seq_render_to(0);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_PLAY), (uint32_t)UI_LED_OFF);   /* the beat's first step */
    seq_render_to(1);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_PLAY), (uint32_t)UI_LED_LIT);
}

/* What SEQ view reads out: a knob's value for its column (TEMPO in BPM, a sign on TRANSPOSE, -- for a
 * knob that waits); a step as its notes by name, REST or TIE; while a step is armed, the notes played */
TT_TEST(seq_texts)
{
    static const uint8_t CH[3] = {60, 64, 67};
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN], t[UI_SEQ_TEXT_LEN];
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "120") == 0 && strcmp(unit, "BPM") == 0);
    seq_set_tempo(s, 1235);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "123.5") == 0);
    ui_seq_column(&ui_u, 3, num, unit);
    TT_CHECK(strcmp(num, "50") == 0 && strcmp(unit, "%") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1100);  /* S2: an empty step's bend, the center */
    ui_seq_column(&ui_u, 1, num, unit);
    TT_CHECK(strcmp(num, "0") == 0 && strcmp(unit, "%") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1200);  /* S3 */
    seq_set_transpose(s, 3);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "+3") == 0 && strcmp(unit, "st") == 0);
    ui_seq_column(&ui_u, 1, num, unit);                      /* KEY TRANSPOSE waits */
    TT_CHECK(strcmp(num, "--") == 0);
    seq_set_step(s, 1, CH, 3, 100);
    seq_toggle_tie(s, 2);
    ui_seq_step_text(&ui_u, 1, t);
    TT_CHECK(strcmp(t, "C4 E4 G4") == 0);
    ui_seq_step_text(&ui_u, 2, t);
    TT_CHECK(strcmp(t, "TIE") == 0);
    ui_seq_step_text(&ui_u, 0, t);
    TT_CHECK(strcmp(t, "REST") == 0);
    ui_seq_chord_text(&ui_u, t);
    TT_CHECK(strcmp(t, "") == 0);
    ui_key(&ui_u, 0, 1, 1300);                                /* armed on step 1: the notes played so far */
    ui_key(&ui_u, 0, 0, 1350);
    ui_chord_note(&ui_u, 57, 90, 1);
    ui_chord_note(&ui_u, 61, 90, 1);
    ui_seq_chord_text(&ui_u, t);
    TT_CHECK(strcmp(t, "A3 C#4") == 0);
}

/* SEQ view redraws its step strip on each step played and its focus row when the chord held changes;
 * an EDIT hold there is no compare (EDIT's tap leaves SEQ view) */
TT_TEST(seq_view_redraws_and_edit_hold)
{
    static const uint8_t C[1] = {60};
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1100);
    ui_tap(UI_BTN_PLAY, 1200);
    (void)ui_take(&ui_u, 1300);
    seq_render_to(1);
    TT_CHECK(ui_take(&ui_u, 1400) & UI_D_SLOT);
    TT_CHECK_EQ(ui_take(&ui_u, 1500) & UI_D_SLOT, 0u);
    ui_chord_note(&ui_u, 60, 100, 1);
    TT_CHECK(ui_take(&ui_u, 1600) & UI_D_FOCUS);
    ui_chord_note(&ui_u, 60, 0, 0);
    ui_tap(UI_BTN_PLAY, 1700);                                /* stop; an edit, then EDIT held */
    seq_set_step(play_seq(&ui_p), 0, C, 1, 100);
    ui_cc(&ui_u, 23, 10, 1800);                               /* (a patch edit: compare would have one) */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_EDIT, 0, 2000);
    (void)ui_take(&ui_u, 2500);
    TT_CHECK(!ui_u.comparing);
    TT_CHECK(!ui_p.comparing);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_EDIT, 0, 2600);
}

/* SEQ view's footer (visual.md): the pattern playing, PTN 11 on a fresh FM-1, its name, `*` once
 * edited; and NEXT nn while one is queued (PRESETS while playing) */
TT_TEST(seq_footer_says_the_pattern)
{
    static const uint8_t C[1] = {60};
    char left[UI_SEQ_TEXT_LEN], right[UI_SEQ_TEXT_LEN];
    ui_setup();
    ui_seq_footer_text(&ui_u, left, right);
    TT_CHECK(strcmp(left, "PTN 11") == 0);
    TT_CHECK(strcmp(right, "") == 0);
    seq_set_step(play_seq(&ui_p), 0, C, 1, 100);
    ui_seq_footer_text(&ui_u, left, right);
    TT_CHECK(strcmp(left, "PTN 11*") == 0);
    seq_play(play_seq(&ui_p));
    seq_render_to(0);
    play_load_pattern(&ui_p, 1);                              /* queued: 12 */
    ui_seq_footer_text(&ui_u, left, right);
    TT_CHECK(strcmp(right, "NEXT 12") == 0);
}

/* PRESETS in SEQ view browses patterns (interaction.md 5, Pattern slots). Stopped, each click loads the
 * next one at once, around 64 slots; with PATTERN EDITED the first click only warns, and another the
 * same way within 1.5 s discards and loads. A fast turn moves a row (8). */
TT_TEST(seq_presets_loads_patterns_when_stopped)
{
    static const uint8_t C[1] = {60};
    uint32_t patch;
    ui_setup();
    patch = play_prog(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1100);
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 1u);               /* 12 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -2, 1200);   /* around: 88 */
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 63u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 3, 1300);    /* fast: a row */
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 7u);
    TT_CHECK_EQ(play_prog(&ui_p), patch);                    /* (the patch stays) */
    seq_set_step(play_seq(&ui_p), 0, C, 1, 100);             /* edited: warned first */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 2000);
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 7u);
    TT_CHECK(strcmp(ui_banner(&ui_u), "PTN EDITED - TURN TO DISCARD") == 0);
    TT_CHECK(strlen(ui_banner(&ui_u)) <= 28u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 2500);
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 8u);
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
}

/* Playing, a click picks the next pattern, NEXT PATTERN nn; it takes over after the running pattern's
 * pass. Turning back to the running pattern's number cancels it. */
TT_TEST(seq_presets_queues_the_next_pattern_while_playing)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_tap(UI_BTN_PLAY, 1100);
    seq_render_to(0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1200);
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 0u);
    TT_CHECK_EQ(play_pattern_next(&ui_p), 1);
    TT_CHECK(strcmp(ui_banner(&ui_u), "NEXT PATTERN 12") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1300);    /* on from the queued one */
    TT_CHECK_EQ(play_pattern_next(&ui_p), 2);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -2, 1400);   /* back to the running one: none */
    TT_CHECK_EQ(play_pattern_next(&ui_p), -1);
}

/* The host's picker opens on HOME held alone 1.5 s, and the module sees HOME let go then (fm1-dock
 * dock.c: "the module saw it let go"): the sequencer stops, in every state (interaction.md case 62). A
 * HOME tap doesn't stop it. */
TT_TEST(seq_stops_when_the_picker_opens)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_PLAY, 1000);
    ui_tap(UI_BTN_HOME, 1200);                                /* a tap: plays on */
    TT_CHECK(s->want);
    ui_tap(UI_BTN_SEQ, 1300);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_HOME, 0, 2000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_HOME, 0, 3500);    /* held 1.5 s: the picker */
    TT_CHECK(!s->want);
    ui_tap(UI_BTN_PLAY, 4000);                                /* from GLO too */
    ui_tap(UI_BTN_SEQ, 4100);
    ui_tap(UI_BTN_GLO, 4200);
    TT_CHECK_EQ(ui_u.state, UI_GLO);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_HOME, 0, 5000);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_HOME, 0, 6600);
    TT_CHECK(!s->want);
}

/* the key (0..26 from F3) of step i (0..15): the white keys F3..G5 in order (Eric, over decision Q1) */
static const uint8_t SEQ_WK[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t SEQ_BK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* a key that plays while armed, as the module reports it: ui_key leaves it, it sounds, and the note
 * joins the chord (ui_chord_note) */
static void seq_note(uint32_t k, uint32_t note, uint32_t vel, int down, uint32_t ms)
{
    TT_CHECK_EQ(ui_key(&ui_u, k, down, ms), 0);
    ui_chord_note(&ui_u, note, vel, down);
}

/* SEQ view's keyboard (Eric): the 16 white keys F3..G5 are the steps; the 11 black keys do nothing
 * (silent). A white key's up goes where its down went, even after SEQ view closed. Outside it, every
 * key plays. */
TT_TEST(seq_white_keys_are_steps_and_black_keys_nothing)
{
    uint32_t i;
    ui_setup();
    TT_CHECK_EQ(ui_key(&ui_u, 2, 1, 900), 0);                /* PLAY: a note */
    TT_CHECK_EQ(ui_key(&ui_u, 2, 0, 950), 0);
    ui_tap(UI_BTN_SEQ, 1000);
    for (i = 0; i < 11u; i++) {                               /* black: taken, nothing happens */
        TT_CHECK_EQ(ui_key(&ui_u, SEQ_BK[i], 1, 1100 + 10 * i), 1);
        TT_CHECK_EQ(ui_key(&ui_u, SEQ_BK[i], 0, 1105 + 10 * i), 1);
        TT_CHECK(!ui_u.seq_armed);
    }
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[15], 1, 1300), 1);      /* G5: step 16, taken */
    TT_CHECK_EQ(ui_u.seq_cursor, 15u);
    ui_tap(UI_BTN_SEQ, 1400);                                 /* closed while it's down */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[15], 0, 1500), 1);      /* its up still goes to SEQ */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[15], 1, 1600), 0);      /* and now it plays */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[15], 0, 1650), 0);
}

/* Tap a step, then play (Eric): the white key moves the cursor there and arms it; then all 27 keys
 * (and MIDI in) play, and the notes played go to that step, the first PLAY_VOICES pressed at the
 * highest velocity, when the last is let go; the cursor moves on to the next step and the white keys
 * are steps again. */
TT_TEST(seq_tap_a_step_then_play_writes_it)
{
    seq *s;
    const seq_step *st;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[4], 1, 1100), 1);       /* C4: step 5 */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[4], 0, 1150), 1);
    TT_CHECK_EQ(ui_u.seq_cursor, 4u);
    TT_CHECK(ui_u.seq_armed);
    seq_note(SEQ_WK[4], 60, 100, 1, 1200);                    /* the same key now plays: C4 */
    seq_note(SEQ_BK[3], 61, 90, 1, 1210);                     /* a black key plays too: C#4 */
    seq_note(SEQ_WK[6], 64, 110, 1, 1220);                    /* E4 */
    seq_note(SEQ_WK[6], 64, 0, 0, 1300);                      /* let go one by one: */
    TT_CHECK_EQ(seq_get_step(s, 4)->kind, (uint8_t)SEQ_REST); /* (not yet) */
    seq_note(SEQ_BK[3], 61, 0, 0, 1310);
    seq_note(SEQ_WK[4], 60, 0, 0, 1320);                      /* the last: written */
    st = seq_get_step(s, 4);
    TT_CHECK_EQ(st->kind, (uint8_t)SEQ_NOTES_ON);
    TT_CHECK_EQ(st->n, 3u);
    TT_CHECK(st->note[0] == 60 && st->note[1] == 61 && st->note[2] == 64);
    TT_CHECK_EQ(st->vel, 110u);
    TT_CHECK_EQ(ui_u.seq_cursor, 5u);                         /* on to the next */
    TT_CHECK(!ui_u.seq_armed);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[0], 1, 1400), 1);       /* white keys are steps again */
    ui_key(&ui_u, SEQ_WK[0], 0, 1450);
    ui_chord_note(&ui_u, 67, 100, 1);                         /* MIDI in plays and writes the same */
    ui_chord_note(&ui_u, 67, 0, 0);
    TT_CHECK_EQ(seq_get_step(s, 0)->note[0], 67u);
    TT_CHECK_EQ(ui_u.seq_cursor, 1u);
}

/* Only the voices' worth of notes is kept, the first pressed (MAX n NOTES); the cursor wraps at the
 * pattern's length */
TT_TEST(seq_step_keeps_the_first_voices_notes_and_wraps)
{
    static const uint8_t N[6] = {48, 52, 55, 59, 62, 65};
    const seq_step *st;
    uint32_t i;
    char want[16] = "MAX 0 NOTES";
    ui_setup();
    seq_set_length(play_seq(&ui_p), 4);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_key(&ui_u, SEQ_WK[3], 1, 1100);                        /* step 4, the last */
    ui_key(&ui_u, SEQ_WK[3], 0, 1150);
    for (i = 0; i < 6u; i++)
        ui_chord_note(&ui_u, N[i], 80, 1);
    for (i = 0; i < 6u; i++)
        ui_chord_note(&ui_u, N[i], 0, 0);
    st = seq_get_step(play_seq(&ui_p), 3);
    TT_CHECK_EQ(st->n, (uint8_t)PLAY_VOICES);
    for (i = 0; i < PLAY_VOICES; i++)
        TT_CHECK_EQ(st->note[i], N[i]);
    want[4] = (char)('0' + PLAY_VOICES);
    TT_CHECK(strcmp(ui_banner(&ui_u), want) == 0);
    TT_CHECK_EQ(ui_u.seq_cursor, 0u);                         /* around */
}

/* A tapped step with nothing played only moves the cursor: after UI_SEQ_ARM_MS with no note, or at
 * any other control, the white keys are steps again; a note down keeps it armed till it's let go */
TT_TEST(seq_armed_step_lets_go_without_a_note)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_key(&ui_u, SEQ_WK[2], 1, 1100);
    ui_key(&ui_u, SEQ_WK[2], 0, 1150);
    (void)ui_take(&ui_u, 1100 + UI_SEQ_ARM_MS - 10u);
    TT_CHECK(ui_u.seq_armed);
    (void)ui_take(&ui_u, 1100 + UI_SEQ_ARM_MS);               /* the time's up */
    TT_CHECK(!ui_u.seq_armed);
    TT_CHECK_EQ(ui_u.seq_cursor, 2u);
    TT_CHECK_EQ(seq_get_step(s, 2)->kind, (uint8_t)SEQ_REST);
    ui_key(&ui_u, SEQ_WK[7], 1, 5000);                        /* again; a knob lets go of it */
    ui_key(&ui_u, SEQ_WK[7], 0, 5050);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 5100);
    TT_CHECK(!ui_u.seq_armed);
    ui_key(&ui_u, SEQ_WK[8], 1, 6000);                        /* a note down: no time limit */
    ui_key(&ui_u, SEQ_WK[8], 0, 6050);
    seq_note(SEQ_WK[1], 55, 100, 1, 6100);
    (void)ui_take(&ui_u, 6000 + 10u * UI_SEQ_ARM_MS);
    TT_CHECK(ui_u.seq_armed);
    seq_note(SEQ_WK[1], 55, 0, 0, 60000);
    TT_CHECK_EQ(seq_get_step(s, 8)->note[0], 55u);
}

/* The key LEDs in SEQ view (interaction.md 5): on the white keys, a step past the length off, a rest
 * glowing, a note lit, a tie lit with a short dark blink every 500 ms, the cursor blinking lit and glow
 * at 4 Hz when stopped (armed: 8 Hz), the playhead its step inverted; black keys dark, and while armed
 * a held key lit. Outside SEQ view, while playing, the playhead's white key is lit. */
TT_TEST(seq_key_leds)
{
    static const uint8_t C[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 2, C, 1, 100);
    seq_toggle_tie(s, 3);
    seq_set_length(s, 8);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_u.seq_cursor = 5;
    ui_u.now_ms = 10000;                                      /* (a time off every blink) */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[1], 0), (uint32_t)UI_LED_GLOW);   /* a rest */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[2], 0), (uint32_t)UI_LED_LIT);    /* a note */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[3], 0), (uint32_t)UI_LED_LIT);    /* a tie, between blinks */
    ui_u.now_ms = 10450;
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[3], 0), (uint32_t)UI_LED_OFF);    /* its blink */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[9], 0), (uint32_t)UI_LED_OFF);    /* past the length */
    ui_u.now_ms = 10000;
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[5], 0), (uint32_t)UI_LED_LIT);    /* the cursor: lit, glow */
    ui_u.now_ms = 10125;
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[5], 0), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_BK[0], 0), (uint32_t)UI_LED_OFF);    /* a black key */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_BK[0], 1), (uint32_t)UI_LED_OFF);
    ui_key(&ui_u, SEQ_WK[6], 1, 11000);                       /* armed on step 7 */
    ui_key(&ui_u, SEQ_WK[6], 0, 11050);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_BK[0], 1), (uint32_t)UI_LED_LIT);    /* held while armed */
    ui_u.now_ms = 10000;
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[6], 0), (uint32_t)UI_LED_LIT);    /* armed: 8 Hz */
    ui_u.now_ms = 10063;
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[6], 0), (uint32_t)UI_LED_GLOW);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 0, 11100);      /* (let go of the arming) */
    ui_tap(UI_BTN_PLAY, 12000);
    seq_render_to(2);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[2], 0), (uint32_t)UI_LED_OFF);    /* the playhead on a note */
    seq_render_to(4);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[4], 0), (uint32_t)UI_LED_LIT);    /* on a rest */
    ui_tap(UI_BTN_SEQ, 12400);                                /* PLAY state, still playing */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[4], 0), (uint32_t)UI_LED_LIT);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[2], 1), (uint32_t)UI_LED_LIT);    /* a held key */
}

/* The piano roll's pitch range (visual.md, S5a): it fits the pattern's notes, at least an octave; 3 px a
 * semitone up to 24 rows, 2 px up to 36; it refits only when a note falls outside, so it doesn't jump */
TT_TEST(seq_roll_fits_the_notes)
{
    static const uint8_t LOW[1] = {48}, HIGH[1] = {72}, MID[1] = {60}, TOP[1] = {90};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_seq_roll_fit(&ui_u);                             /* empty: an octave around C4 */
    TT_CHECK_EQ(ui_u.roll_pps, 3u);
    TT_CHECK(ui_u.roll_lo <= 60u && 60u < ui_u.roll_lo + 72u / 3u);
    seq_set_step(s, 0, LOW, 1, 100);
    seq_set_step(s, 1, HIGH, 1, 100);                         /* 48..72: 25 rows, 2 px */
    TT_CHECK(ui_seq_roll_fit(&ui_u));
    TT_CHECK_EQ(ui_u.roll_pps, 2u);
    TT_CHECK(ui_u.roll_lo <= 48u && 72u < ui_u.roll_lo + 72u / 2u);
    seq_set_step(s, 2, MID, 1, 100);                          /* inside: no refit */
    TT_CHECK(!ui_seq_roll_fit(&ui_u));
    seq_set_step(s, 3, TOP, 1, 100);                          /* 48..90: wider than 36, centered */
    TT_CHECK(ui_seq_roll_fit(&ui_u));
    TT_CHECK_EQ(ui_u.roll_pps, 2u);
}

/* The roll redraws column by column (visual.md: never whole while it plays): a cursor move marks the
 * old and new columns, an edit its column, a playhead move the old and new, opening SEQ view all 16 */
TT_TEST(seq_roll_redraws_only_what_changed)
{
    static const uint8_t C[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);            /* all of it */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 3, 1100);      /* cursor 0 -> 3 */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), (1u << 0) | (1u << 3));
    ui_tap(UI_BTN_ENV, 1200);                                 /* a tie on step 4 */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u) & (1u << 3), 1u << 3);
    seq_set_step(s, 0, C, 1, 100);
    (void)ui_seq_roll_take(&ui_u);
    ui_tap(UI_BTN_PLAY, 1300);
    seq_render_to(1);
    (void)ui_take(&ui_u, 1400);
    TT_CHECK(ui_seq_roll_take(&ui_u) & (1u << 1));            /* the playhead's new column */
    seq_render_to(2);
    (void)ui_take(&ui_u, 1500);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u) & ((1u << 1) | (1u << 2)), (1u << 1) | (1u << 2));
}

/* The readout names a step's chord when it's a common triad or seventh (Eric, R3), from any voicing and
 * inversion; otherwise "" (the notes show alone) */
TT_TEST(seq_chord_names)
{
    static const struct { uint8_t n, note[4]; const char *name; } C[] = {
        {3, {60, 64, 67}, "C MAJ"}, {3, {57, 60, 64}, "A MIN"}, {3, {64, 67, 72}, "C MAJ"},
        {3, {59, 62, 65}, "B DIM"}, {3, {60, 64, 68}, "C AUG"}, {3, {62, 64, 69}, "D SUS2"},
        {3, {62, 67, 69}, "D SUS4"}, {4, {55, 59, 62, 65}, "G 7"}, {4, {53, 57, 60, 64}, "F MAJ7"},
        {4, {48, 51, 55, 58}, "C MIN7"}, {4, {61, 65, 68, 72}, "C# MAJ7"}, {1, {60}, ""},
        {2, {60, 67}, ""}, {3, {60, 61, 62}, ""},
    };
    char name[UI_SEQ_TEXT_LEN];
    uint32_t i;
    for (i = 0; i < sizeof C / sizeof C[0]; i++) {
        ui_seq_chord_name(C[i].note, C[i].n, name);
        if (strcmp(name, C[i].name) != 0)
            printf("    chord %u: \"%s\", not \"%s\"\n", (unsigned)i, name, C[i].name);
        TT_CHECK(strcmp(name, C[i].name) == 0);
    }
}

/* An armed step's frame blinks at 8 Hz: each flip redraws its column alone, and nothing else in the roll */
TT_TEST(seq_roll_blink_redraws_the_armed_column)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[3], 1, 1100), 1);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[3], 0, 1110), 1);       /* armed on step 4 */
    (void)ui_take(&ui_u, 1120);
    (void)ui_seq_roll_take(&ui_u);
    (void)ui_take(&ui_u, 1130);                               /* a flip (125 ms a phase) */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 1u << 3);
    (void)ui_take(&ui_u, 1140);                               /* none */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0u);
    (void)ui_take(&ui_u, 1250);                               /* the next flip */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 1u << 3);
}

/* An armed step's notes count toward the range as they go down: one below it refits the roll */
TT_TEST(seq_roll_fits_an_armed_note)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_seq_roll_fit(&ui_u);                             /* empty: around C4 */
    TT_CHECK(ui_u.roll_lo > 30u);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[0], 1, 1100), 1);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[0], 0, 1110), 1);       /* armed on step 1 */
    seq_note(5, 30, 100, 1, 1200);                            /* F#1, held */
    TT_CHECK(ui_seq_roll_fit(&ui_u));
    TT_CHECK(ui_u.roll_lo <= 30u);
}

/* A pattern loaded from PRESETS redraws every column of the roll */
TT_TEST(seq_roll_redraws_whole_on_a_pattern_load)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1050);
    (void)ui_seq_roll_take(&ui_u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1100);
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 1u);
    (void)ui_take(&ui_u, 1110);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);
}

/* LENGTH, a tie and a rest change what the steps after the cursor hold (a tie runs on until a rest),
 * so each redraws every column */
TT_TEST(seq_roll_redraws_whole_on_length_tie_and_rest)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1050);
    (void)ui_seq_roll_take(&ui_u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -4, 1100);     /* LENGTH 12 */
    (void)ui_take(&ui_u, 1110);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);
    ui_tap(UI_BTN_ENV, 1200);                                 /* a tie */
    (void)ui_take(&ui_u, 1310);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);
    ui_tap(UI_BTN_SEL, 1400);                                 /* a rest */
    (void)ui_take(&ui_u, 1510);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);
}

/* Stopping takes the playhead off: its last column redraws */
TT_TEST(seq_roll_stop_clears_the_playhead)
{
    static const uint8_t C[1] = {60};
    ui_setup();
    seq_set_step(play_seq(&ui_p), 0, C, 1, 100);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_tap(UI_BTN_PLAY, 1100);
    seq_render_to(2);
    (void)ui_take(&ui_u, 1200);
    (void)ui_seq_roll_take(&ui_u);
    ui_tap(UI_BTN_PLAY, 1300);                                /* stop */
    (void)ui_take(&ui_u, 1400);
    TT_CHECK(ui_seq_roll_take(&ui_u) & (1u << 2));
}

/* A queued pattern taking over at the pass's end redraws the whole roll and the readout */
TT_TEST(seq_roll_redraws_whole_when_the_next_pattern_takes_over)
{
    uint32_t d;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_tap(UI_BTN_PLAY, 1100);
    seq_render_to(0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1200);    /* NEXT PATTERN 12 */
    seq_render_to(15);
    (void)ui_take(&ui_u, 1300);
    (void)ui_seq_roll_take(&ui_u);
    seq_render_to(0);                                         /* the pass ends: 12 plays */
    play_pattern_tick(&ui_p);                                 /* (the module's main loop) */
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 1u);
    d = ui_take(&ui_u, 1400);
    TT_CHECK(d & UI_D_FOCUS);
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 0xFFFFu);
}

/* The header shows the tempo (>07 120 INT): a TEMPO turn redraws it, stopped or playing */
TT_TEST(seq_tempo_turn_redraws_the_header)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1050);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1100);      /* 121 BPM */
    TT_CHECK(ui_take(&ui_u, 1110) & UI_D_HEAD);
}

/* Notes wider than the roll (over 36 semitones): it fits once, then stays put; a refit would redraw
 * every column on every step */
TT_TEST(seq_roll_wide_span_fits_once)
{
    static const uint8_t LOW[1] = {30}, HIGH[1] = {90}, MID[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 0, LOW, 1, 100);
    seq_set_step(s, 1, HIGH, 1, 100);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK(ui_seq_roll_fit(&ui_u));
    TT_CHECK(!ui_seq_roll_fit(&ui_u));
    seq_set_step(s, 2, MID, 1, 100);                          /* within what it saw: still put */
    TT_CHECK(!ui_seq_roll_fit(&ui_u));
}

/* Step record (interaction.md 5, way 2): a REC tap while stopped arms it and opens SEQ view, from PLAY
 * or EDIT; in SEQ view it arms where the cursor is */
TT_TEST(seq_rec_tap_arms_and_opens_seq_view)
{
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);                                 /* from PLAY */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(ui_u.seq_rec);
    ui_tap(UI_BTN_REC, 1100);                                 /* disarms (9 has the rest) */
    TT_CHECK(!ui_u.seq_rec);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 3, 1200);      /* cursor on step 4 */
    ui_tap(UI_BTN_REC, 1300);
    TT_CHECK(ui_u.seq_rec);
    TT_CHECK_EQ(ui_u.seq_cursor, 3u);
    ui_tap(UI_BTN_SEQ, 1400);                                 /* out of SEQ view */
    ui_tap(UI_BTN_ENV, 1500);                                 /* EDIT */
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    ui_tap(UI_BTN_REC, 1600);                                 /* from EDIT */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(ui_u.seq_rec);
}

/* The cursor stays inside the pattern: a white key for a step past the length arms nothing, and LENGTH
 * turned below the cursor brings it to the last step (found by the hammer) */
TT_TEST(seq_cursor_stays_inside_the_length)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -8, 1100);     /* LENGTH 8 */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[12], 1, 1200), 1);      /* step 13: past it */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[12], 0, 1210), 1);
    TT_CHECK(!ui_u.seq_armed);
    TT_CHECK(ui_u.seq_cursor < 8u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 8, 1300);      /* LENGTH 16 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 13, 1400);     /* cursor on step 14 */
    TT_CHECK_EQ(ui_u.seq_cursor, 13u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -6, 1500);     /* LENGTH 10 */
    TT_CHECK_EQ(ui_u.seq_cursor, 9u);
}

/* While playing, REC does nothing but say STOP TO RECORD (decision Q3), in SEQ view or out of it */
TT_TEST(seq_rec_while_playing_says_stop_to_record)
{
    ui_setup();
    ui_tap(UI_BTN_PLAY, 1000);
    ui_tap(UI_BTN_REC, 1100);                                 /* from PLAY */
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    TT_CHECK(!ui_u.seq_rec);
    TT_CHECK(strcmp(ui_banner(&ui_u), "STOP TO RECORD") == 0);
    ui_tap(UI_BTN_SEQ, 1200);
    ui_tap(UI_BTN_REC, 1300);                                 /* in SEQ view */
    TT_CHECK(!ui_u.seq_rec);
    TT_CHECK(strcmp(ui_banner(&ui_u), "STOP TO RECORD") == 0);
}

/* In REC every key plays (Eric, R1): no white key is a step key, no black key is silent */
TT_TEST(seq_rec_every_key_plays)
{
    uint32_t k;
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    for (k = 0; k < PLAY_KEYS; k++) {
        TT_CHECK_EQ(ui_key(&ui_u, k, 1, 1100 + 20 * k), 0);
        TT_CHECK_EQ(ui_key(&ui_u, k, 0, 1110 + 20 * k), 0);
    }
    TT_CHECK(!ui_u.seq_armed);                                /* (no step tapped) */
}

/* In REC, the keys held make a step; when all are let go it is written at the cursor and the cursor
 * moves on, still recording: no time-out */
TT_TEST(seq_rec_writes_a_step_and_moves_on)
{
    const seq_step *st;
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    seq_note(5, 60, 90, 1, 1100);
    seq_note(9, 64, 110, 1, 1110);
    seq_note(5, 60, 0, 0, 1200);
    seq_note(9, 64, 0, 0, 1210);
    st = seq_get_step(play_seq(&ui_p), 0);
    TT_CHECK_EQ(st->kind, SEQ_NOTES_ON);
    TT_CHECK_EQ(st->n, 2u);
    TT_CHECK_EQ(st->vel, 110u);
    TT_CHECK_EQ(ui_u.seq_cursor, 1u);
    (void)ui_take(&ui_u, 9000);                               /* long after: still recording */
    TT_CHECK(ui_u.seq_rec);
    seq_note(7, 62, 100, 1, 9100);
    seq_note(7, 62, 0, 0, 9200);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 1)->note[0], 62u);
    TT_CHECK_EQ(ui_u.seq_cursor, 2u);
}

/* In REC the cursor goes around at the length */
TT_TEST(seq_rec_wraps_at_the_length)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -12, 1100);    /* LENGTH 4 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 3, 1200);      /* step 4, the last */
    ui_tap(UI_BTN_REC, 1300);
    seq_note(5, 60, 90, 1, 1400);
    seq_note(5, 60, 0, 0, 1500);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 3)->kind, SEQ_NOTES_ON);
    TT_CHECK_EQ(ui_u.seq_cursor, 0u);
    TT_CHECK(ui_u.seq_rec);
}

/* In REC, MIDI notes join the step as keys do */
TT_TEST(seq_rec_takes_midi_notes)
{
    const seq_step *st;
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    ui_chord_note(&ui_u, 48, 100, 1);
    ui_chord_note(&ui_u, 55, 100, 1);
    ui_chord_note(&ui_u, 48, 0, 0);
    ui_chord_note(&ui_u, 55, 0, 0);
    st = seq_get_step(play_seq(&ui_p), 0);
    TT_CHECK_EQ(st->kind, SEQ_NOTES_ON);
    TT_CHECK_EQ(st->n, 2u);
    TT_CHECK_EQ(ui_u.seq_cursor, 1u);
}

/* In REC, SEL writes a rest and ENV a tie at the cursor, each moving on (outside REC they act on the
 * cursor step and stay) */
TT_TEST(seq_rec_sel_and_env_write_and_move_on)
{
    static const uint8_t C[1] = {60};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 0, C, 1, 100);
    seq_set_step(s, 1, C, 1, 100);
    ui_tap(UI_BTN_REC, 1000);
    ui_tap(UI_BTN_SEL, 1100);                                 /* step 1: a rest */
    TT_CHECK_EQ(seq_get_step(s, 0)->kind, SEQ_REST);
    TT_CHECK_EQ(ui_u.seq_cursor, 1u);
    ui_tap(UI_BTN_ENV, 1200);                                 /* step 2: a tie */
    TT_CHECK_EQ(seq_get_step(s, 1)->kind, SEQ_TIE);
    TT_CHECK_EQ(ui_u.seq_cursor, 2u);
    ui_tap(UI_BTN_ENV, 1300);                                 /* step 3: a tie, not a toggle back */
    TT_CHECK_EQ(seq_get_step(s, 2)->kind, SEQ_TIE);
    TT_CHECK_EQ(ui_u.seq_cursor, 3u);
}

/* In REC, FX steps back one step, around, without writing; outside REC it does nothing */
TT_TEST(seq_rec_fx_steps_back)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_tap(UI_BTN_FX, 1100);                                  /* not in REC: nothing */
    TT_CHECK_EQ(ui_u.seq_cursor, 0u);
    ui_tap(UI_BTN_REC, 1200);
    ui_tap(UI_BTN_FX, 1300);                                  /* around: the last step */
    TT_CHECK_EQ(ui_u.seq_cursor, 15u);
    ui_tap(UI_BTN_FX, 1400);
    TT_CHECK_EQ(ui_u.seq_cursor, 14u);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 15)->kind, SEQ_REST);
    TT_CHECK(ui_u.seq_rec);
}

/* A REC tap disarms, and so does leaving SEQ view (SEQ, HOME, EDIT, GLO); a step part-built then is
 * dropped: nothing is written */
TT_TEST(seq_rec_disarms_and_drops_the_step_being_built)
{
    static const uint32_t OUT[4] = {UI_BTN_SEQ, UI_BTN_HOME, UI_BTN_EDIT, UI_BTN_GLO};
    uint32_t i;
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    seq_note(5, 60, 90, 1, 1100);                             /* held */
    ui_tap(UI_BTN_REC, 1200);                                 /* disarmed */
    seq_note(5, 60, 0, 0, 1300);
    TT_CHECK(!ui_u.seq_rec);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 0)->kind, SEQ_REST);
    TT_CHECK_EQ(ui_u.seq_cursor, 0u);
    for (i = 0; i < 4u; i++) {
        uint32_t t = 2000u + 1000u * i;
        if (ui_u.state != UI_SEQ)
            ui_tap(UI_BTN_SEQ, t);
        ui_tap(UI_BTN_REC, t + 100);
        TT_CHECK(ui_u.seq_rec);
        seq_note(5, 60, 90, 1, t + 200);
        ui_tap(OUT[i], t + 300);
        TT_CHECK(ui_u.state != UI_SEQ);
        TT_CHECK(!ui_u.seq_rec);
        seq_note(5, 60, 0, 0, t + 400);
        TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 0)->kind, SEQ_REST);
        if (ui_u.state == UI_GLO)
            ui_tap(UI_BTN_GLO, t + 500);
    }
}

/* PLAY in REC starts the sequencer and disarms, so a performance doesn't record over the pattern */
TT_TEST(seq_rec_play_starts_and_disarms)
{
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    ui_tap(UI_BTN_PLAY, 1100);
    TT_CHECK(play_seq(&ui_p)->want);
    TT_CHECK(!ui_u.seq_rec);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
}

/* REC's LED: lit while step record is armed, glowing when stopped, off while playing (REC does nothing
 * then); FX glows in REC, where it steps back, and is off otherwise */
TT_TEST(seq_rec_button_leds)
{
    ui_setup();
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_GLOW);
    ui_tap(UI_BTN_REC, 1000);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_LIT);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_FX), (uint32_t)UI_LED_GLOW);
    ui_tap(UI_BTN_REC, 1100);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_FX), (uint32_t)UI_LED_OFF);
    ui_tap(UI_BTN_PLAY, 1200);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_OFF);
}

/* Key LEDs in REC (Eric, R2): a key held is lit; the cursor's white key blinks at 8 Hz; the other white
 * keys keep their steps as a dim map (a note or tie glows, a rest is off) */
TT_TEST(seq_rec_key_leds)
{
    static const uint8_t C[1] = {60};
    ui_setup();
    seq_set_step(play_seq(&ui_p), 2, C, 1, 100);
    ui_tap(UI_BTN_REC, 1000);
    (void)ui_take(&ui_u, 1008);                               /* 1008 / 63: even */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[0], 0), (uint32_t)UI_LED_LIT);
    (void)ui_take(&ui_u, 1071);                               /* odd */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[0], 0), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[2], 0), (uint32_t)UI_LED_GLOW);   /* a note: dim */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[3], 0), (uint32_t)UI_LED_OFF);    /* a rest */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_BK[0], 0), (uint32_t)UI_LED_OFF);
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_BK[0], 1), (uint32_t)UI_LED_LIT);    /* held */
    TT_CHECK_EQ(ui_key_led(&ui_u, SEQ_WK[3], 1), (uint32_t)UI_LED_LIT);
}

/* In REC the cursor column has the armed look: it blinks (its column redraws on each flip), the notes
 * going down show there and in the readout's line */
TT_TEST(seq_rec_cursor_has_the_armed_look)
{
    char t[UI_SEQ_TEXT_LEN];
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    (void)ui_take(&ui_u, 1001);                               /* phase 8: even */
    (void)ui_seq_roll_take(&ui_u);
    (void)ui_take(&ui_u, 1130);                               /* phase 9: a flip */
    TT_CHECK_EQ(ui_seq_roll_take(&ui_u), 1u << 0);
    seq_note(5, 60, 90, 1, 1140);
    TT_CHECK(ui_seq_roll_take(&ui_u) & 1u);
    ui_seq_chord_text(&ui_u, t);
    TT_CHECK(strcmp(t, "C4") == 0);
}

/* In REC, SEL held does nothing (interaction.md's table: "-"); outside REC it clears the pattern */
TT_TEST(seq_rec_sel_held_keeps_the_pattern)
{
    static const uint8_t C[1] = {60};
    ui_setup();
    seq_set_step(play_seq(&ui_p), 5, C, 1, 100);
    ui_tap(UI_BTN_REC, 1000);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 3000);     /* held 1.9 s */
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 5)->kind, SEQ_NOTES_ON);
    TT_CHECK_EQ(ui_u.seq_cursor, 0u);
}

/* Arming or disarming REC redraws the bottom strip (its legend and key map change), PLAY's disarm too;
 * and REC takes over from a tapped step (way 1): one arming at a time */
TT_TEST(seq_rec_toggle_redraws_the_strip_and_ends_a_tapped_step)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1001);
    ui_tap(UI_BTN_REC, 1100);
    TT_CHECK(ui_take(&ui_u, 1101) & UI_D_FOOT);
    ui_tap(UI_BTN_REC, 1200);
    TT_CHECK(ui_take(&ui_u, 1201) & UI_D_FOOT);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[2], 1, 1300), 1);       /* a step tapped: way 1 */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[2], 0, 1310), 1);
    TT_CHECK(ui_u.seq_armed);
    ui_tap(UI_BTN_REC, 1400);
    TT_CHECK(ui_u.seq_rec);
    TT_CHECK(!ui_u.seq_armed);
    (void)ui_take(&ui_u, 1401);
    ui_tap(UI_BTN_PLAY, 1500);                                /* starts, disarms */
    TT_CHECK(ui_take(&ui_u, 1501) & UI_D_FOOT);
}

/* A step part-built in REC is dropped when SAVE opens (REC disarms: SAVE takes the screen) or a pattern
 * loads from PRESETS: notes let go later write nothing, and the next step starts empty */
TT_TEST(seq_rec_save_and_pattern_load_drop_the_step_being_built)
{
    const seq_step *st;
    ui_setup();
    ui_tap(UI_BTN_REC, 1000);
    seq_note(5, 60, 90, 1, 1100);                             /* held */
    ui_tap(UI_BTN_SAVE, 1200);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK(!ui_u.seq_rec);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_OFF);
    seq_note(5, 60, 0, 0, 1300);
    ui_tap(UI_BTN_HOME, 1400);                                /* SAVE: cancel */
    ui_tap(UI_BTN_HOME, 1500);
    if (ui_u.state != UI_SEQ)
        ui_tap(UI_BTN_SEQ, 1600);
    ui_tap(UI_BTN_REC, 1700);
    seq_note(9, 64, 90, 1, 1800);
    seq_note(9, 64, 0, 0, 1900);
    st = seq_get_step(play_seq(&ui_p), 0);
    TT_CHECK_EQ(st->n, 1u);                                   /* E4 alone, no stale C4 */
    TT_CHECK_EQ(st->note[0], 64u);
    seq_note(5, 60, 90, 1, 2000);                             /* held on step 2 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 2100);    /* edited: warned */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 2200);    /* loads 12 */
    TT_CHECK_EQ(play_pattern_prog(&ui_p), 1u);
    seq_note(5, 60, 0, 0, 2300);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), ui_u.seq_cursor)->kind, SEQ_REST);
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
}

/* MIDI clock in at bpm, ticks i0 .. i0 + n - 1 (host time 24 MHz from 0) */
static void seq_clock_at(double bpm, uint32_t i0, uint32_t n)
{
    double dt = SEQ_CLK_HZ * 60.0 / (bpm * SEQ_CLK_PPQN);
    uint32_t i;
    for (i = i0; i < i0 + n; i++)
        seq_clock_in(play_seq(&ui_p), 0xF8u, (uint32_t)(1000.0 + i * dt));
}

static int seq_tempo_reads(const char *want_num, const char *want_unit)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    ui_seq_column(&ui_u, 0, num, unit);
    if (strcmp(num, want_num) != 0 || strcmp(unit, want_unit) != 0) {
        printf("    TEMPO reads \"%s %s\", not \"%s %s\"\n", num, unit, want_num, want_unit);
        return 0;
    }
    return 1;
}


/* Under external clock, TEMPO shows the clock's tempo and MIDI CLOCK (interaction.md 5); on INT, the
 * pattern's tempo in BPM */
TT_TEST(seq_tempo_shows_the_midi_clock)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    uint32_t t;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "120") == 0 && strcmp(unit, "BPM") == 0);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    seq_clock_at(140.0, 0, 96);                               /* (not the pattern's 120) */
    t = seq_clock_tempo(play_seq(&ui_p));
    TT_CHECK(t >= 1390u && t <= 1410u);
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
}

/* A TEMPO turn under external clock changes nothing and says TEMPO FOLLOWS MIDI CLOCK (case 48) */
TT_TEST(seq_tempo_turn_under_the_clock_says_so)
{
    uint32_t before;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    seq_clock_at(140.0, 0, 96);
    before = seq_tempo(play_seq(&ui_p));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 5, 1100);
    TT_CHECK_EQ(seq_tempo(play_seq(&ui_p)), before);
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
    TT_CHECK(strcmp(ui_banner(&ui_u), "TEMPO FOLLOWS MIDI CLOCK") == 0);
    TT_CHECK(strlen(ui_banner(&ui_u)) <= 28u);
}

/* The transport's clock (interaction.md 7): INT; MIDI NO CLOCK with G13 MIDI and no clock coming (Codex
 * P2-b); MIDI with one; WAITING FOR CLOCK while PLAY waits (case 47) */
TT_TEST(seq_clock_text_names_the_source)
{
    char t[UI_SEQ_TEXT_LEN];
    ui_setup();
    ui_seq_clock_text(&ui_u, t);
    TT_CHECK(strcmp(t, "INT") == 0);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);      /* G13 MIDI, no clock yet */
    ui_seq_clock_text(&ui_u, t);
    TT_CHECK(strcmp(t, "MIDI NO CLOCK") == 0);
    seq_clock_at(140.0, 0, 48);
    ui_seq_clock_text(&ui_u, t);
    TT_CHECK(strcmp(t, "MIDI") == 0);
    ui_tap(UI_BTN_PLAY, 1000);
    TT_CHECK(seq_waiting(play_seq(&ui_p)));
    ui_seq_clock_text(&ui_u, t);
    TT_CHECK(strcmp(t, "WAITING FOR CLOCK") == 0);
}

/* The clock changes with no input (it starts, its tempo moves, PLAY waits): SEQ view's TEMPO, header and
 * footer redraw by themselves; with nothing changed, none of them */
TT_TEST(seq_clock_changes_redraw_by_themselves)
{
    uint32_t d, i;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    (void)ui_take(&ui_u, 1100);
    TT_CHECK_EQ(ui_take(&ui_u, 1200) & (UI_D_FOOT | UI_D_HEAD | UI_D_COL(0)), 0u);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);      /* INT to MIDI */
    d = ui_take(&ui_u, 1300);
    TT_CHECK_EQ(d & (UI_D_FOOT | UI_D_HEAD | UI_D_COL(0)), UI_D_FOOT | UI_D_HEAD | UI_D_COL(0));   /* (the roll's
                                                                                   header: >07 120 MIDI) */
    seq_clock_at(140.0, 0, 96);                               /* a clock at 140: TEMPO, header, footer */
    d = ui_take(&ui_u, 1400);
    TT_CHECK_EQ(d & (UI_D_FOOT | UI_D_HEAD | UI_D_COL(0)), UI_D_FOOT | UI_D_HEAD | UI_D_COL(0));
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
    seq_clock_at(140.0, 96, 96);                              /* steady: nothing */
    TT_CHECK_EQ(ui_take(&ui_u, 1500) & (UI_D_FOOT | UI_D_HEAD | UI_D_COL(0)), 0u);
    for (i = 192; i < 288; i++)                               /* faster: 150 BPM */
        seq_clock_in(play_seq(&ui_p), 0xF8u, (uint32_t)(1000.0 + 192.0 * SEQ_CLK_HZ * 60.0 / (140.0 * 24.0) +
                                                        (i - 192u) * (SEQ_CLK_HZ * 60.0 / (150.0 * 24.0))));
    TT_CHECK(ui_take(&ui_u, 1600) & UI_D_COL(0));
    TT_CHECK(seq_tempo_reads("150", "MIDI CLOCK"));
}

/* G13 MIDI before any clock comes in: TEMPO has no tempo to show (--), marked MIDI CLOCK, not the
 * pattern's (Codex P2-b) */
TT_TEST(seq_tempo_before_the_clock_comes_in)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "--") == 0);
    TT_CHECK(strcmp(unit, "MIDI CLOCK") == 0);
}

/* A 120 BPM clock with +-1.5 ms of jitter: once it has settled, TEMPO shows 120 and the screen doesn't
 * redraw for the clock's small moves (the firmware review: the raw tempo redrew at frame rate) */
TT_TEST(seq_jittery_clock_settles_on_the_screen)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    uint32_t i, r = 777u, redraws = 0, ms = 1000;
    ui_setup();
    ui_tap(UI_BTN_SEQ, ms);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    for (i = 0; i < 960u; i++) {                              /* 40 beats */
        r = r * 1103515245u + 12345u;
        seq_clock_in(play_seq(&ui_p), 0xF8u, 1000000u + i * (SEQ_CLK_HZ / 48u) + (r >> 16) % 72000u - 36000u);
        if (i % 4u == 0) {                                    /* a frame each 4 ticks (about 30 a second) */
            ms += 40u;
            if (i >= 480u && ui_take(&ui_u, ms) & (UI_D_FOOT | UI_D_COL(0)))
                redraws++;
            else if (i < 480u)
                (void)ui_take(&ui_u, ms);
        }
    }
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "120") == 0);
    TT_CHECK_EQ(redraws, 0u);
}

/* A clock's first two ticks a hair apart read as a huge tempo for a moment: the tempo SEQ view shows stays
 * three digits (999 BPM at most), so no text runs past its digits (the firmware review: 39761 BPM wrote
 * past the header's buffer) */
TT_TEST(seq_clock_burst_shows_at_most_three_digits)
{
    uint32_t i, worst = 0;
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_clock_source(s, SEQ_CLK_MIDI);
    seq_clock_in(s, 0xF8u, 1000u);
    seq_clock_in(s, 0xF8u, 1010u);                            /* 10 counts apart */
    for (i = 0; i < 48u; i++) {
        uint32_t t;
        seq_clock_in(s, 0xF8u, 1010u + (i + 1u) * (SEQ_CLK_HZ / 48u));
        t = ui_seq_tempo_shown(&ui_u);
        worst = t > worst ? t : worst;
    }
    TT_CHECK(worst <= 9990u);
}

/* AUTO, a clock at 140 coming, the pattern at 120: TEMPO reads the clock's 140 before PLAY, while PLAY
 * is armed for its next tick, and once it runs; never the pattern's 120 between (Codex P2-d) */
TT_TEST(seq_tempo_stays_the_clocks_while_armed)
{
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_AUTO);
    seq_clock_at(140.0, 0, 96);
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
    ui_tap(UI_BTN_PLAY, 1100);                                /* armed for the next tick */
    TT_CHECK(seq_waiting(play_seq(&ui_p)));
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
    seq_clock_at(140.0, 96, 1);                               /* the tick: running */
    TT_CHECK(!seq_waiting(play_seq(&ui_p)));
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
}

static int seq_footer_clock_is(const char *want)
{
    char t[UI_SEQ_TEXT_LEN];
    ui_seq_clock_text(&ui_u, t);
    if (strcmp(t, want) != 0) {
        printf("    the clock reads \"%s\", not \"%s\"\n", t, want);
        return 0;
    }
    return 1;
}

/* G13 MIDI with no clock there (none yet, or lost): the footer says MIDI NO CLOCK and TEMPO has no tempo
 * to show (--, MIDI CLOCK), not the pattern's; with ticks, the clock's tempo and MIDI; lost again, back
 * to NO CLOCK. PLAY armed waits, and says WAITING FOR CLOCK. AUTO with no clock is INT (Codex P2-b) */
TT_TEST(seq_midi_with_no_clock_says_so)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(s, SEQ_CLK_MIDI);
    TT_CHECK(seq_footer_clock_is("MIDI NO CLOCK"));
    TT_CHECK(seq_tempo_reads("--", "MIDI CLOCK"));
    seq_clock_at(140.0, 0, 96);
    TT_CHECK(seq_footer_clock_is("MIDI"));
    TT_CHECK(seq_tempo_reads("140", "MIDI CLOCK"));
    seq_clock_poll(s, 1000u + 96u * (SEQ_CLK_HZ * 60u / (140u * SEQ_CLK_PPQN)) + SEQ_CLK_HZ / 2u);   /* gone */
    TT_CHECK(seq_footer_clock_is("MIDI NO CLOCK"));
    TT_CHECK(seq_tempo_reads("--", "MIDI CLOCK"));
    ui_tap(UI_BTN_PLAY, 1100);                                /* armed: waiting wins */
    TT_CHECK(seq_footer_clock_is("WAITING FOR CLOCK"));
    ui_tap(UI_BTN_PLAY, 1200);
    seq_set_clock_source(s, SEQ_CLK_AUTO);                    /* AUTO, no clock: its own */
    TT_CHECK(seq_footer_clock_is("INT"));
    TT_CHECK(seq_tempo_reads("120", "BPM"));
}

/* ticks at bpm with the jittery test's +-1.5 ms, from tick i0 at host time *t, n of them (on from there) */
static void seq_clock_jittery(double bpm, uint32_t n, double *t, uint32_t *r)
{
    double dt = SEQ_CLK_HZ * 60.0 / (bpm * SEQ_CLK_PPQN);
    uint32_t i;
    for (i = 0; i < n; i++, *t += dt) {
        *r = *r * 1103515245u + 12345u;
        seq_clock_in(play_seq(&ui_p), 0xF8u, (uint32_t)(*t) + (*r >> 16) % 72000u - 36000u);
    }
}

/* The whole BPM shown holds against a clock's jitter: one hovering at 120.5 shows one value, with no
 * flips once settled (Codex P3-a: 15). A real move shows: 120, 121, 122 each read as such. And a clock
 * slower than 10 BPM (over 250 ms a tick) is never a clock at all: polled as the main loop does, it's
 * gone before its second tick, so no tempo shows for it, not 0 (Codex on #293: the floor test was unreal) */
TT_TEST(seq_tempo_shown_holds_against_jitter_and_follows_moves)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    uint32_t r = 777u, i, shown, flips = 0;
    double t = 1000000.0;
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_clock_source(s, SEQ_CLK_MIDI);
    seq_clock_jittery(120.5, 480, &t, &r);                    /* settled */
    shown = ui_seq_tempo_shown(&ui_u);
    for (i = 0; i < 480u; i++) {
        seq_clock_jittery(120.5, 1, &t, &r);
        if (ui_seq_tempo_shown(&ui_u) != shown)
            flips++, shown = ui_seq_tempo_shown(&ui_u);
    }
    printf("    120.5 BPM with jitter: %u flips, shows %u\n", flips, shown / 10u);
    TT_CHECK_EQ(flips, 0u);
    TT_CHECK(shown == 1200u || shown == 1210u);
    seq_clock_jittery(120.0, 480, &t, &r);
    TT_CHECK(seq_tempo_reads("120", "MIDI CLOCK"));
    seq_clock_jittery(121.0, 480, &t, &r);
    TT_CHECK(seq_tempo_reads("121", "MIDI CLOCK"));
    seq_clock_jittery(122.0, 480, &t, &r);
    TT_CHECK(seq_tempo_reads("122", "MIDI CLOCK"));
    ui_setup();                                               /* a clock at 0.4 BPM, polled: never one */
    s = play_seq(&ui_p);
    seq_set_clock_source(s, SEQ_CLK_MIDI);
    for (i = 0; i < 4u; i++) {
        uint32_t at = 1000u + i * (uint32_t)(SEQ_CLK_HZ * 60.0 / (0.4 * SEQ_CLK_PPQN)), k;
        seq_clock_in(s, 0xF8u, at);
        for (k = 1; k <= 10u; k++)                            /* the main loop polls on (each 100 ms) */
            seq_clock_poll(s, at + k * (SEQ_CLK_HZ / 10u));
        ui_seq_column(&ui_u, 0, num, unit);
        TT_CHECK(strcmp(num, "--") == 0);
        TT_CHECK(seq_footer_clock_is("MIDI NO CLOCK"));
    }
}

/* Tap a step, then play: the step written takes the live MIDI-in pitch bend and CC1 (mod wheel), the
 * last ones that came in, as its bend and mod (#42 S7, interaction.md 5 "Writing steps") */
TT_TEST(seq_a_tapped_step_takes_the_live_bend_and_mod)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_midi_controls(&ui_u, 0xE0, 0x00, 0x60);               /* bend 0x3000: +4096 from the center */
    ui_midi_controls(&ui_u, 0xB0, 1, 77);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[4], 1, 1100), 1);       /* step 5 armed */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[4], 0, 1150), 1);
    seq_note(10, 60, 100, 1, 1200);
    seq_note(10, 60, 0, 0, 1300);                             /* written */
    TT_CHECK_EQ(seq_get_step(s, 4)->kind, (uint8_t)SEQ_NOTES_ON);
    TT_CHECK_EQ(seq_get_step(s, 4)->bend, 4096);
    TT_CHECK_EQ(seq_get_step(s, 4)->mod, 77u);
    TT_CHECK_EQ(seq_get_step(s, 4)->glide, 0u);               /* (a written step jumps) */
}

/* In REC each step takes the bend and mod as its first note goes down (Eric: A): moved between steps,
 * the next one has the new values, and a move while it's held changes nothing; with none come in yet,
 * the center and 0 (no MIDI controller) */
TT_TEST(seq_rec_each_step_takes_the_bend_and_mod_of_its_writing)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_REC, 1000);
    seq_note(5, 60, 90, 1, 1100);                             /* nothing came in: center, 0 */
    seq_note(5, 60, 0, 0, 1200);
    TT_CHECK_EQ(seq_get_step(s, 0)->bend, 0);
    TT_CHECK_EQ(seq_get_step(s, 0)->mod, 0u);
    ui_midi_controls(&ui_u, 0xE0, 0x00, 0x00);               /* the lowest bend */
    ui_midi_controls(&ui_u, 0xB0, 1, 127);
    seq_note(7, 62, 90, 1, 1300);
    seq_note(7, 62, 0, 0, 1400);
    TT_CHECK_EQ(seq_get_step(s, 1)->bend, -8192);
    TT_CHECK_EQ(seq_get_step(s, 1)->mod, 127u);
    ui_midi_controls(&ui_u, 0xE0, 0x7F, 0x7F);               /* the highest, then the note */
    seq_note(9, 64, 90, 1, 1500);
    ui_midi_controls(&ui_u, 0xB0, 1, 3);                      /* while it's held: not this */
    seq_note(9, 64, 0, 0, 1600);
    TT_CHECK_EQ(seq_get_step(s, 2)->bend, 8191);
    TT_CHECK_EQ(seq_get_step(s, 2)->mod, 127u);
    TT_CHECK_EQ(seq_get_step(s, 0)->bend, 0);                 /* (the steps before keep theirs) */
    TT_CHECK_EQ(seq_get_step(s, 1)->mod, 127u);
}

/* Only what comes in counts: a pattern playing its own bends and mod doesn't change the live values a
 * step takes; G11 keeps out another channel's; CC121 (reset all controllers) centers and zeroes them */
TT_TEST(seq_only_midi_in_on_the_channel_sets_the_live_bend_and_mod)
{
    static const uint8_t C[1] = {48};
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    seq_set_step(s, 0, C, 1, 100);
    seq_set_step_bend_mod(s, 0, 6000, 90, 0);                 /* the pattern bends and mods itself */
    ui_tap(UI_BTN_PLAY, 1000);
    seq_render_to(1);                                         /* played: the engine has its bend */
    ui_tap(UI_BTN_PLAY, 1100);
    seq_render_to(-1);
    TT_CHECK_EQ(ui_u.live_bend, 0);
    TT_CHECK_EQ(ui_u.live_mod, 0u);
    ui_u.set.midi_in = 3;                                     /* G11 channel 3 */
    ui_midi_controls(&ui_u, 0xE0, 0x00, 0x60);               /* channel 1: not ours */
    ui_midi_controls(&ui_u, 0xB0, 1, 50);
    TT_CHECK_EQ(ui_u.live_bend, 0);
    TT_CHECK_EQ(ui_u.live_mod, 0u);
    ui_midi_controls(&ui_u, 0xE2, 0x00, 0x60);               /* channel 3 */
    ui_midi_controls(&ui_u, 0xB2, 1, 50);
    TT_CHECK_EQ(ui_u.live_bend, 4096);
    TT_CHECK_EQ(ui_u.live_mod, 50u);
    ui_midi_controls(&ui_u, 0xB2, 7, 20);                     /* another CC: nothing */
    TT_CHECK_EQ(ui_u.live_mod, 50u);
    ui_midi_controls(&ui_u, 0xB2, 121, 0);                    /* reset all controllers */
    TT_CHECK_EQ(ui_u.live_bend, 0);
    TT_CHECK_EQ(ui_u.live_mod, 0u);
}

/* Eric (A): a step takes the bend and mod as its first note goes down, not as the last key is let go.
 * Bend, play a chord, let the wheel spring back to the center, let go of the keys: the step keeps the
 * bend (and a note added after the wheel came back doesn't take the center) */
TT_TEST(seq_a_step_keeps_the_bend_its_first_note_had)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[2], 1, 1100), 1);       /* step 3 armed */
    TT_CHECK_EQ(ui_key(&ui_u, SEQ_WK[2], 0, 1150), 1);
    ui_midi_controls(&ui_u, 0xE0, 0x00, 0x50);               /* bent: +2048 */
    ui_midi_controls(&ui_u, 0xB0, 1, 40);
    seq_note(10, 60, 100, 1, 1200);                           /* the chord */
    seq_note(12, 64, 100, 1, 1210);
    ui_midi_controls(&ui_u, 0xE0, 0x00, 0x40);               /* the wheel springs back */
    ui_midi_controls(&ui_u, 0xB0, 1, 0);
    seq_note(14, 67, 100, 1, 1220);                           /* (a note added now joins at the latch) */
    seq_note(10, 60, 0, 0, 1300);
    seq_note(12, 64, 0, 0, 1310);
    seq_note(14, 67, 0, 0, 1320);                             /* all let go: written */
    TT_CHECK_EQ(seq_get_step(s, 2)->n, 3u);
    TT_CHECK_EQ(seq_get_step(s, 2)->bend, 2048);
    TT_CHECK_EQ(seq_get_step(s, 2)->mod, 40u);
}

/* AUTO, no clock, PLAY: it plays its own clock (the pattern's 120). A clock at 140 coming in then, with no
 * start, doesn't take over a transport already running (seq.c starts on the clock only from a start or
 * an armed PLAY), so the screen goes on saying INT and 120, and TEMPO can be turned (Codex on #293) */
TT_TEST(seq_playing_its_own_clock_shows_its_own_tempo)
{
    seq *s;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_tap(UI_BTN_PLAY, 1100);                                /* AUTO, no clock: its own */
    seq_render_to(0);
    seq_clock_at(140.0, 0, 96);                               /* a clock, no start */
    TT_CHECK(s->want && !seq_waiting(s));
    TT_CHECK(seq_footer_clock_is("INT"));
    TT_CHECK(seq_tempo_reads("120", "BPM"));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1200);      /* TEMPO turns: it's the steps' tempo */
    TT_CHECK_EQ(seq_tempo(s), 1210u);
}

/* the whole BPM shown, after n steady ticks at bpm, on from host time *t */
static uint32_t seq_shown_after(double bpm, uint32_t n, double *t)
{
    double dt = SEQ_CLK_HZ * 60.0 / (bpm * SEQ_CLK_PPQN);
    uint32_t i;
    for (i = 0; i < n; i++, *t += dt)
        seq_clock_in(play_seq(&ui_p), 0xF8u, (uint32_t)*t);
    return ui_seq_tempo_shown(&ui_u) / 10u;
}

/* The whole BPM shown moves when the clock is 0.75 BPM off it, the same up and down (Codex on #293: down
 * held 0.71, up 0.8): at 121 shown, 121.74 holds and 121.76 moves to 122; at 122, 121.26 holds and 121.24
 * moves to 121 */
TT_TEST(seq_tempo_shown_moves_at_three_quarters_both_ways)
{
    double t = 1000000.0;
    ui_setup();
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    TT_CHECK_EQ(seq_shown_after(121.0, 480, &t), 121u);
    TT_CHECK_EQ(seq_shown_after(121.74, 480, &t), 121u);
    TT_CHECK_EQ(seq_shown_after(121.76, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after(122.0, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after(121.26, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after(121.24, 480, &t), 121u);
}

/* the whole BPM shown, after n ticks exactly dt counts apart, on from host time *t */
static uint32_t seq_shown_after_counts(uint32_t dt, uint32_t n, uint32_t *t)
{
    uint32_t i;
    for (i = 0; i < n; i++, *t += dt)
        seq_clock_in(play_seq(&ui_p), 0xF8u, *t);
    return ui_seq_tempo_shown(&ui_u) / 10u;
}

/* The 0.75 BPM edges exactly, as tick lengths (Codex on 300c4fd: a floored hundredth moved at 0.746
 * down): at 122 shown, ticks of 494830 counts (121.2538 BPM, 0.7462 under) and 494844 (121.2503) hold,
 * 494848 (121.2493) moves to 121; up from 122, 488800 (122.7496) holds and 488796 (122.7506) moves. (Two
 * counts either side: the smoothed tick settles to within a count, not onto it.) */
TT_TEST(seq_tempo_shown_edges_are_exact)
{
    uint32_t t = 1000000u, at122 = (uint32_t)(SEQ_CLK_HZ * 60.0 / (122.0 * SEQ_CLK_PPQN));
    ui_setup();
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    TT_CHECK_EQ(seq_shown_after_counts(at122, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after_counts(494830u, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after_counts(494844u, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after_counts(494848u, 480, &t), 121u);
    TT_CHECK_EQ(seq_shown_after_counts(at122, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after_counts(488800u, 480, &t), 122u);
    TT_CHECK_EQ(seq_shown_after_counts(488796u, 480, &t), 123u);
}

static void run_ui_seq_tests(void)
{
    TT_RUN(seq_playing_its_own_clock_shows_its_own_tempo);
    TT_RUN(seq_tempo_shown_moves_at_three_quarters_both_ways);
    TT_RUN(seq_tempo_shown_edges_are_exact);
    TT_RUN(seq_a_tapped_step_takes_the_live_bend_and_mod);
    TT_RUN(seq_a_step_keeps_the_bend_its_first_note_had);
    TT_RUN(seq_rec_each_step_takes_the_bend_and_mod_of_its_writing);
    TT_RUN(seq_only_midi_in_on_the_channel_sets_the_live_bend_and_mod);
    TT_RUN(seq_tempo_stays_the_clocks_while_armed);
    TT_RUN(seq_midi_with_no_clock_says_so);
    TT_RUN(seq_tempo_shown_holds_against_jitter_and_follows_moves);
    TT_RUN(seq_cursor_stays_inside_the_length);
    TT_RUN(seq_rec_tap_arms_and_opens_seq_view);
    TT_RUN(seq_rec_while_playing_says_stop_to_record);
    TT_RUN(seq_rec_every_key_plays);
    TT_RUN(seq_rec_writes_a_step_and_moves_on);
    TT_RUN(seq_rec_wraps_at_the_length);
    TT_RUN(seq_rec_takes_midi_notes);
    TT_RUN(seq_rec_sel_and_env_write_and_move_on);
    TT_RUN(seq_rec_fx_steps_back);
    TT_RUN(seq_rec_disarms_and_drops_the_step_being_built);
    TT_RUN(seq_rec_play_starts_and_disarms);
    TT_RUN(seq_rec_button_leds);
    TT_RUN(seq_rec_key_leds);
    TT_RUN(seq_rec_cursor_has_the_armed_look);
    TT_RUN(seq_rec_sel_held_keeps_the_pattern);
    TT_RUN(seq_rec_toggle_redraws_the_strip_and_ends_a_tapped_step);
    TT_RUN(seq_rec_save_and_pattern_load_drop_the_step_being_built);
    TT_RUN(seq_tempo_shows_the_midi_clock);
    TT_RUN(seq_tempo_turn_under_the_clock_says_so);
    TT_RUN(seq_clock_text_names_the_source);
    TT_RUN(seq_clock_changes_redraw_by_themselves);
    TT_RUN(seq_tempo_before_the_clock_comes_in);
    TT_RUN(seq_jittery_clock_settles_on_the_screen);
    TT_RUN(seq_clock_burst_shows_at_most_three_digits);
    TT_RUN(seq_tap_opens_and_goes_back);
    TT_RUN(seq_play_starts_and_stops_anywhere);
    TT_RUN(seq_white_keys_are_steps_and_black_keys_nothing);
    TT_RUN(seq_tap_a_step_then_play_writes_it);
    TT_RUN(seq_step_keeps_the_first_voices_notes_and_wraps);
    TT_RUN(seq_armed_step_lets_go_without_a_note);
    TT_RUN(seq_key_leds);
    TT_RUN(seq_roll_fits_the_notes);
    TT_RUN(seq_roll_redraws_only_what_changed);
    TT_RUN(seq_chord_names);
    TT_RUN(seq_roll_blink_redraws_the_armed_column);
    TT_RUN(seq_roll_fits_an_armed_note);
    TT_RUN(seq_roll_redraws_whole_on_a_pattern_load);
    TT_RUN(seq_roll_redraws_whole_on_length_tie_and_rest);
    TT_RUN(seq_roll_stop_clears_the_playhead);
    TT_RUN(seq_roll_redraws_whole_when_the_next_pattern_takes_over);
    TT_RUN(seq_tempo_turn_redraws_the_header);
    TT_RUN(seq_roll_wide_span_fits_once);
    TT_RUN(seq_select_and_the_pattern_page);
    TT_RUN(seq_step_and_play_pages);
    TT_RUN(seq_s2_bend_sets_the_step_bend_in_percent);
    TT_RUN(seq_s2_mod_sets_the_step_mod);
    TT_RUN(seq_s2_move_and_lfo_switch_jump_and_glide);
    TT_RUN(seq_s2_on_a_rest_or_tie_shows_and_keeps);
    TT_RUN(seq_s2_vel_keeps_the_step_and_the_others);
    TT_RUN(seq_s2_columns_follow_the_cursor_step);
    TT_RUN(seq_section_buttons_and_the_way_out);
    TT_RUN(seq_button_leds);
    TT_RUN(seq_texts);
    TT_RUN(seq_view_redraws_and_edit_hold);
    TT_RUN(seq_footer_says_the_pattern);
    TT_RUN(seq_presets_loads_patterns_when_stopped);
    TT_RUN(seq_presets_queues_the_next_pattern_while_playing);
    TT_RUN(seq_stops_when_the_picker_opens);
}
