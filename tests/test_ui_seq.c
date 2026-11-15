/* SEQ view (interaction.md 5, firmware/ui_seq.c): the sequencer on the panel (#39, S2). */

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
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1400);      /* 120.0 + 1 BPM */
    TT_CHECK_EQ(seq_tempo(s), SEQ_TEMPO_DEF + 10u);
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
}
