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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1400);      /* BEND waits: nothing */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1500);  /* S3 PLAY */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -2, 1600);
    TT_CHECK_EQ(seq_transpose(s), -2);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1700);  /* around */
    TT_CHECK_EQ(ui_u.seq_page, 0u);
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
 * stopped, lit while playing with a dark flash on each beat's first step; REC off until step record
 * (S5). In SEQ view SEL (rest) and ENV (tie) glow, LFO (MOVE, #42) and FX (BACK, in REC) are off. */
TT_TEST(seq_button_leds)
{
    ui_setup();
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEQ), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_PLAY), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_REC), (uint32_t)UI_LED_OFF);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEQ), (uint32_t)UI_LED_LIT);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_SEL), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_ENV), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_led(&ui_u, UI_BTN_LFO), (uint32_t)UI_LED_OFF);
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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1100);  /* S2 */
    ui_seq_column(&ui_u, 1, num, unit);
    TT_CHECK(strcmp(num, "--") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1200);  /* S3 */
    seq_set_transpose(s, 3);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "+3") == 0 && strcmp(unit, "st") == 0);
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

/* MIDI clock in at 120 BPM: n ticks, from host time 0 (24 MHz, 500000 counts a tick) */
static void seq_clock_ticks(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        seq_clock_in(play_seq(&ui_p), 0xF8u, i * (SEQ_CLK_HZ / 48u));
}

/* Under external clock, TEMPO shows the clock's tempo and MIDI CLOCK (interaction.md 5); on INT, the
 * pattern's tempo in BPM */
TT_TEST(seq_tempo_shows_the_midi_clock)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN], want[UI_VALUE_LEN];
    uint32_t t;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(unit, "BPM") == 0);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    seq_clock_ticks(96);
    t = seq_clock_tempo(play_seq(&ui_p));
    TT_CHECK(t >= 1190u && t <= 1210u);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(unit, "MIDI CLOCK") == 0);
    snprintf(want, sizeof want, "%u", (unsigned)(t / 10u));
    TT_CHECK(strncmp(num, want, strlen(want)) == 0);
}

/* A TEMPO turn under external clock changes nothing and says TEMPO FOLLOWS MIDI CLOCK (case 48) */
TT_TEST(seq_tempo_turn_under_the_clock_says_so)
{
    uint32_t before;
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    seq_clock_ticks(96);
    before = seq_tempo(play_seq(&ui_p));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 5, 1100);
    TT_CHECK_EQ(seq_tempo(play_seq(&ui_p)), before);
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
    TT_CHECK(strcmp(ui_banner(&ui_u), "TEMPO FOLLOWS MIDI CLOCK") == 0);
    TT_CHECK(strlen(ui_banner(&ui_u)) <= 28u);
}

/* The transport's clock (interaction.md 7): INT, MIDI, or WAITING FOR CLOCK while PLAY waits (case 47) */
TT_TEST(seq_clock_text_names_the_source)
{
    char t[UI_SEQ_TEXT_LEN];
    ui_setup();
    ui_seq_clock_text(&ui_u, t);
    TT_CHECK(strcmp(t, "INT") == 0);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);      /* G13 MIDI, no clock yet */
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
    seq_clock_ticks(96);                                      /* 120 BPM: as shown already, so nothing */
    (void)ui_take(&ui_u, 1400);
    TT_CHECK_EQ(ui_take(&ui_u, 1500) & (UI_D_FOOT | UI_D_HEAD | UI_D_COL(0)), 0u);
    for (i = 96; i < 192; i++)                                /* faster: 140 BPM */
        seq_clock_in(play_seq(&ui_p), 0xF8u, 96u * (SEQ_CLK_HZ / 48u) + (i - 96u) * (SEQ_CLK_HZ / 56u));
    TT_CHECK(ui_take(&ui_u, 1600) & UI_D_COL(0));
}

/* G13 MIDI before any clock comes in: TEMPO shows the pattern's tempo (the clock has none yet), still
 * marked MIDI CLOCK; a turn still says the clock sets it */
TT_TEST(seq_tempo_before_the_clock_comes_in)
{
    char num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    ui_seq_column(&ui_u, 0, num, unit);
    TT_CHECK(strcmp(num, "120") == 0);
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

static void run_ui_seq_tests(void)
{
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
    TT_RUN(seq_section_buttons_and_the_way_out);
    TT_RUN(seq_button_leds);
    TT_RUN(seq_texts);
    TT_RUN(seq_view_redraws_and_edit_hold);
    TT_RUN(seq_footer_says_the_pattern);
    TT_RUN(seq_presets_loads_patterns_when_stopped);
    TT_RUN(seq_presets_queues_the_next_pattern_while_playing);
    TT_RUN(seq_stops_when_the_picker_opens);
}
