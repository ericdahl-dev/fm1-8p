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

static void run_ui_seq_tests(void)
{
    TT_RUN(seq_tap_opens_and_goes_back);
    TT_RUN(seq_play_starts_and_stops_anywhere);
    TT_RUN(seq_step_keys_are_taken_in_seq_view);
    TT_RUN(seq_chord_and_step_key_writes_the_step);
    TT_RUN(seq_chord_keeps_the_first_voices_notes);
}
