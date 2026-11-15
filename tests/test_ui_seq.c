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

static void run_ui_seq_tests(void)
{
    TT_RUN(seq_tap_opens_and_goes_back);
    TT_RUN(seq_play_starts_and_stops_anywhere);
}
