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

/* The split keyboard (interaction.md 5): in SEQ view keys 0-15 are steps and make no sound; their up
 * goes where their down went, even if SEQ view closed in between. Outside it they play. */
TT_TEST(seq_step_keys_are_taken_in_seq_view)
{
    ui_setup();
    TT_CHECK_EQ(ui_key(&ui_u, 3, 1, 900), 0);                /* PLAY: a note */
    TT_CHECK_EQ(ui_key(&ui_u, 3, 0, 950), 0);
    ui_tap(UI_BTN_SEQ, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, 3, 1, 1100), 1);               /* a step: taken */
    TT_CHECK_EQ(ui_key(&ui_u, 16, 1, 1150), 0);              /* A4, a chord key: it plays */
    TT_CHECK_EQ(ui_key(&ui_u, 16, 0, 1160), 0);
    ui_tap(UI_BTN_SEQ, 1200);                                 /* closed while step 3 is down */
    TT_CHECK_EQ(ui_key(&ui_u, 3, 0, 1300), 1);               /* its up still goes to SEQ */
    TT_CHECK_EQ(ui_key(&ui_u, 3, 1, 1400), 0);               /* and now it plays */
    TT_CHECK_EQ(ui_key(&ui_u, 3, 0, 1450), 0);
}

/* A chord held (chord keys or MIDI in, as the module reports them) and a step key: the chord is
 * written there at its highest velocity, and the cursor moves there. More step keys while it's held
 * write it again; a step key with nothing held only moves the cursor. */
TT_TEST(seq_chord_and_step_key_writes_the_step)
{
    seq *s;
    const seq_step *st;
    ui_setup();
    s = play_seq(&ui_p);
    ui_tap(UI_BTN_SEQ, 1000);
    ui_chord_note(&ui_u, 60, 100, 1);
    ui_chord_note(&ui_u, 64, 90, 1);
    ui_chord_note(&ui_u, 67, 110, 1);
    TT_CHECK_EQ(ui_key(&ui_u, 3, 1, 1100), 1);
    st = seq_get_step(s, 3);
    TT_CHECK_EQ(st->kind, (uint8_t)SEQ_NOTES_ON);
    TT_CHECK_EQ(st->n, 3u);
    TT_CHECK(st->note[0] == 60 && st->note[1] == 64 && st->note[2] == 67);
    TT_CHECK_EQ(st->vel, 110u);
    TT_CHECK_EQ(ui_u.seq_cursor, 3u);
    ui_key(&ui_u, 3, 0, 1150);
    ui_key(&ui_u, 7, 1, 1200);                                /* again, still held */
    ui_key(&ui_u, 7, 0, 1250);
    TT_CHECK_EQ(seq_get_step(s, 7)->n, 3u);
    ui_chord_note(&ui_u, 60, 0, 0);
    ui_chord_note(&ui_u, 64, 0, 0);
    ui_chord_note(&ui_u, 67, 0, 0);
    ui_key(&ui_u, 10, 1, 1300);                               /* nothing held: the cursor only */
    ui_key(&ui_u, 10, 0, 1350);
    TT_CHECK_EQ(ui_u.seq_cursor, 10u);
    TT_CHECK_EQ(seq_get_step(s, 10)->kind, (uint8_t)SEQ_REST);
}

/* Only as many notes as the voices are kept, the first pressed, and the screen says MAX n NOTES */
TT_TEST(seq_chord_keeps_the_first_voices_notes)
{
    static const uint8_t N[6] = {48, 52, 55, 59, 62, 65};
    const seq_step *st;
    uint32_t i;
    char want[16];
    ui_setup();
    ui_tap(UI_BTN_SEQ, 1000);
    for (i = 0; i < 6u; i++)
        ui_chord_note(&ui_u, N[i], 80, 1);
    ui_key(&ui_u, 0, 1, 1100);
    st = seq_get_step(play_seq(&ui_p), 0);
    TT_CHECK_EQ(st->n, (uint8_t)PLAY_VOICES);
    for (i = 0; i < PLAY_VOICES; i++)
        TT_CHECK_EQ(st->note[i], N[i]);
    snprintf(want, sizeof want, "MAX %u NOTES", (unsigned)PLAY_VOICES);
    TT_CHECK(strcmp(ui_banner(&ui_u), want) == 0);
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
 * to SEQ view; PRESETS and SAVE do nothing until pattern slots (S6). */
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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 3700);    /* no patch load from SEQ view */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(!ui_u.warned);
    ui_tap(UI_BTN_SAVE, 3800);
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

/* The key LEDs in SEQ view (interaction.md 5): a step key past the length off, a rest glowing, a
 * note lit, a tie lit with a short dark blink every 500 ms, the cursor blinking lit and glow at 4 Hz
 * when stopped, the playhead its step inverted; chord keys glow, and are lit while held. Outside SEQ
 * view, while playing, the playhead's key is lit. */
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
    TT_CHECK_EQ(ui_key_led(&ui_u, 1, 0), (uint32_t)UI_LED_GLOW);   /* a rest */
    TT_CHECK_EQ(ui_key_led(&ui_u, 2, 0), (uint32_t)UI_LED_LIT);    /* a note */
    TT_CHECK_EQ(ui_key_led(&ui_u, 3, 0), (uint32_t)UI_LED_LIT);    /* a tie, between its blinks */
    ui_u.now_ms = 10450;
    TT_CHECK_EQ(ui_key_led(&ui_u, 3, 0), (uint32_t)UI_LED_OFF);    /* its blink */
    TT_CHECK_EQ(ui_key_led(&ui_u, 9, 0), (uint32_t)UI_LED_OFF);    /* past the length */
    ui_u.now_ms = 10000;
    TT_CHECK_EQ(ui_key_led(&ui_u, 5, 0), (uint32_t)UI_LED_LIT);    /* the cursor: lit, then glow */
    ui_u.now_ms = 10125;
    TT_CHECK_EQ(ui_key_led(&ui_u, 5, 0), (uint32_t)UI_LED_GLOW);
    TT_CHECK_EQ(ui_key_led(&ui_u, 20, 0), (uint32_t)UI_LED_GLOW);  /* a chord key */
    TT_CHECK_EQ(ui_key_led(&ui_u, 20, 1), (uint32_t)UI_LED_LIT);
    ui_tap(UI_BTN_PLAY, 1200);
    seq_render_to(2);
    TT_CHECK_EQ(ui_key_led(&ui_u, 2, 0), (uint32_t)UI_LED_OFF);    /* the playhead on a note */
    seq_render_to(4);
    TT_CHECK_EQ(ui_key_led(&ui_u, 4, 0), (uint32_t)UI_LED_LIT);    /* on a rest */
    TT_CHECK_EQ(ui_key_led(&ui_u, 5, 0), (uint32_t)UI_LED_GLOW);   /* (playing: no cursor blink) */
    ui_tap(UI_BTN_SEQ, 1400);                                 /* PLAY state, still playing */
    TT_CHECK_EQ(ui_key_led(&ui_u, 4, 0), (uint32_t)UI_LED_LIT);
    TT_CHECK_EQ(ui_key_led(&ui_u, 2, 0), (uint32_t)UI_LED_OFF);
    TT_CHECK_EQ(ui_key_led(&ui_u, 2, 1), (uint32_t)UI_LED_LIT);    /* a held key */
}

/* What SEQ view reads out: a knob's value for its column (TEMPO in BPM, a sign on TRANSPOSE, -- for a
 * knob that waits); a step as its notes by name, REST or TIE; the chord held now */
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

static void run_ui_seq_tests(void)
{
    TT_RUN(seq_tap_opens_and_goes_back);
    TT_RUN(seq_play_starts_and_stops_anywhere);
    TT_RUN(seq_step_keys_are_taken_in_seq_view);
    TT_RUN(seq_chord_and_step_key_writes_the_step);
    TT_RUN(seq_chord_keeps_the_first_voices_notes);
    TT_RUN(seq_select_and_the_pattern_page);
    TT_RUN(seq_step_and_play_pages);
    TT_RUN(seq_section_buttons_and_the_way_out);
    TT_RUN(seq_button_leds);
    TT_RUN(seq_key_leds);
    TT_RUN(seq_texts);
    TT_RUN(seq_view_redraws_and_edit_hold);
    TT_RUN(seq_footer_says_the_pattern);
    TT_RUN(seq_stops_when_the_picker_opens);
}
