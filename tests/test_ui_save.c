/* SAVE (firmware/ui_save.c): naming a patch and writing it to a user slot (interaction.md, SAVE). */

/* A fresh unit with the user slots on the RAM flash, the VCF page open. */
static void save_setup(void)
{
    store_flash_blank();
    (void)play_init(&ui_p);
    play_attach_store(&ui_p, &store_lib_a, &store_fake_io);
    play_store_tick(&ui_p);                     /* as the module's first draw */
    ui_init(&ui_u, &ui_p);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
}

static void save_tap(uint32_t button, uint32_t ms)
{
    ui_event(&ui_u, UI_EV_BTN_DOWN | button, 0, ms);
    ui_event(&ui_u, UI_EV_BTN_UP | button, 0, ms + 50u);
}

/* A SAVE tap in EDIT opens SAVE: the destination starts at the current slot, and for a P patch (never a
 * destination) at the first empty user slot; the name is the patch's. */
TT_TEST(save_tap_opens_save_at_the_first_empty_slot)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_dest, 0u);            /* 11 */
    TT_CHECK(strcmp(ui_u.save_name, "SOUNDTRACK  ") == 0);   /* 12 places to edit */
}

/* With nothing sounding, a second SAVE tap onto an empty slot writes the edit buffer and its name there:
 * SAVED 11 shows, that slot is now the patch playing, unedited, and the state goes back to EDIT. */
TT_TEST(save_writes_the_edit_buffer_when_silent)
{
    static store_patch out;
    uint32_t enc;
    int32_t edited;
    save_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 500);
    edited = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK(strcmp(out.name, "SOUNDTRACK") == 0);
    TT_CHECK_EQ(out.v[FM_PARAM_FILTER_RESONANCE], edited);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
    TT_CHECK_EQ(play_prog(&ui_p), 0u);
    TT_CHECK(!play_edited(&ui_p));
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
}

/* In SAVE, PRESETS picks the destination: every user slot, empty ones too, wrapping within 11..88 (never
 * a P slot), 4 on a fast turn; nothing loads. */
TT_TEST(save_presets_picks_the_destination)
{
    const patch *t;
    save_setup();
    t = play_patch(&ui_p);
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1100);
    TT_CHECK_EQ(ui_u.save_dest, 1u);            /* 12 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -2, 1200);
    TT_CHECK_EQ(ui_u.save_dest, 63u);           /* 88: wrapped */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 3, 1300);
    TT_CHECK_EQ(ui_u.save_dest, 3u);            /* +4 */
    TT_CHECK(play_patch(&ui_p) == t);
    TT_CHECK_EQ(play_prog(&ui_p), PLAY_PROG_P + PATCH_SOUNDTRACK);
}

/* Onto a slot holding another patch, the first SAVE tap asks (REPLACE 12 OTHER?, then SAVE AGAIN on
 * the screen) and writes nothing; the second writes. Onto the slot the patch came from, one tap writes. */
TT_TEST(save_asks_before_replacing_another_patch)
{
    static store_patch other, out;
    save_setup();
    store_from_factory(9, &other);
    memset(other.name, 0, sizeof other.name);
    memcpy(other.name, "OTHER", 5);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 1, &other), 0);
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1100);
    save_tap(UI_BTN_SAVE, 1200);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_CONFIRM);
    TT_CHECK(strcmp(ui_u.save_msg, "REPLACE 12 OTHER?") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    TT_CHECK(strcmp(out.name, "OTHER") == 0);
    save_tap(UI_BTN_SAVE, 1400);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    TT_CHECK(strcmp(out.name, "SOUNDTRACK") == 0);
    TT_CHECK_EQ(play_prog(&ui_p), 1u);
    save_tap(UI_BTN_SAVE, 2000);                /* again, onto 12 itself: one tap */
    TT_CHECK_EQ(ui_u.save_dest, 1u);
    save_tap(UI_BTN_SAVE, 2200);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 12") == 0);
}

/* The name: SELECT moves the cursor over its 12 places (clamped), KNOB1 steps the character there
 * through space, A-Z, 0-9 and '-', KNOB2 inserts a space (clockwise) or deletes (counterclockwise).
 * The patch is saved with the name as edited, trailing spaces trimmed. */
TT_TEST(save_edits_the_name)
{
    static store_patch out;
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.save_cursor, 0u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1100);         /* S -> T */
    TT_CHECK(strncmp(ui_u.save_name, "TOUNDTRACK", 10) == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 10, 1200);
    TT_CHECK_EQ(ui_u.save_cursor, 10u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1300);         /* space -> A -> B */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 5, 1400);
    TT_CHECK_EQ(ui_u.save_cursor, 11u);                          /* clamped */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -1, 1450);        /* space -> '-' (wraps) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, -20, 1500);
    TT_CHECK_EQ(ui_u.save_cursor, 0u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -1, 1600);        /* delete the T */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 1700);         /* a space at 0 */
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK(strcmp(out.name, " OUNDTRACKB-") == 0);
}

/* HOME tap or EDIT tap cancels: nothing is written and the state goes back. Every other button, and
 * KNOB3 and KNOB4, do nothing in SAVE. */
TT_TEST(save_cancels_on_home_or_edit_and_ignores_the_rest)
{
    static store_patch out;
    int32_t res;
    save_setup();
    res = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_ENV, 1100);
    save_tap(UI_BTN_SEL, 1200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 4, 1300);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 4, 1300);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), res);
    save_tap(UI_BTN_HOME, 1400);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), -1);
    save_tap(UI_BTN_SAVE, 2000);
    save_tap(UI_BTN_EDIT, 2100);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), -1);
    TT_CHECK_EQ(store_flash.saves, 0u);
}

/* audio blocks: n of them */
static void save_render(uint32_t n)
{
    while (n--) { memset(ui_out, 0, sizeof ui_out); play_render(&ui_p, ui_out, PLAY_BLOCK); }
}

/* While a note sounds the write waits: the confirming tap takes a snapshot and goes back, every screen
 * shows SAVING WHEN SILENT, and edits meanwhile don't change what is written. On the first silent
 * moment it is written; the edit buffer, changed since, stays edited. */
TT_TEST(save_waits_for_silence)
{
    static store_patch out;
    uint32_t enc, k;
    int32_t snapped;
    save_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 500);
    snapped = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    play_note(&ui_p, 60, 100);
    save_render(4);
    TT_CHECK(!play_silent(&ui_p));
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING WHEN SILENT") == 0);
    ui_event(&ui_u, UI_EV_ENC | enc, 3, 1300);  /* an edit while waiting */
    (void)ui_take(&ui_u, 4000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING WHEN SILENT") == 0);   /* still, past 1.5 s */
    TT_CHECK_EQ(store_flash.saves, 0u);
    play_note(&ui_p, 60, 0);
    for (k = 0; k < 2000 && !play_silent(&ui_p); k++) save_render(1);
    TT_CHECK(play_silent(&ui_p));
    (void)ui_take(&ui_u, 5000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK_EQ(out.v[FM_PARAM_FILTER_RESONANCE], snapped);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
    TT_CHECK(play_edited(&ui_p));
}

/* While a write waits, a SAVE tap does nothing (no second SAVE), and SAVE held 1 s writes the snapshot
 * at once, sound or not (decision Q5: the dropout is accepted). */
TT_TEST(save_held_forces_a_waiting_write)
{
    static store_patch out;
    save_setup();
    play_note(&ui_p, 60, 100);
    save_render(4);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    save_tap(UI_BTN_SAVE, 1400);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(ui_u.save_waiting);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SAVE, 0, 3000);
    (void)ui_take(&ui_u, 3999);
    TT_CHECK_EQ(store_flash.saves, 0u);
    (void)ui_take(&ui_u, 4000);
    TT_CHECK(!play_silent(&ui_p));
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SAVE, 0, 4500);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);           /* the release opens nothing */
}

/* The first HOME tap while a write waits cancels it: SAVE CANCELED, nothing written, and nothing else
 * (EDIT stays: that tap doesn't go home). */
TT_TEST(save_home_cancels_a_waiting_write)
{
    uint32_t k;
    save_setup();
    play_note(&ui_p, 60, 100);
    save_render(4);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    save_tap(UI_BTN_HOME, 1400);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVE CANCELED") == 0);
    play_note(&ui_p, 60, 0);
    for (k = 0; k < 2000 && !play_silent(&ui_p); k++) save_render(1);
    (void)ui_take(&ui_u, 5000);
    TT_CHECK_EQ(store_flash.saves, 0u);
}

/* A write that fails shows SAVE FAILED; the slot stays empty and the edit buffer stays edited. */
TT_TEST(save_failed_write_says_so)
{
    static store_patch out;
    uint32_t enc;
    save_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 500);
    store_flash.fail = 1;
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVE FAILED") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), -1);
    TT_CHECK(play_edited(&ui_p));
    TT_CHECK_EQ(play_prog(&ui_p), PLAY_PROG_P + PATCH_SOUNDTRACK);
}

/* TODO when #187 (ui_key, the number jump) is merged in: SEL down in SAVE, then a key down, gives
 * ui_key == 0 and the key sounds (agent1, 2026-11-13). */

static void run_ui_save_tests(void)
{
    TT_RUN(save_tap_opens_save_at_the_first_empty_slot);
    TT_RUN(save_writes_the_edit_buffer_when_silent);
    TT_RUN(save_presets_picks_the_destination);
    TT_RUN(save_asks_before_replacing_another_patch);
    TT_RUN(save_edits_the_name);
    TT_RUN(save_cancels_on_home_or_edit_and_ignores_the_rest);
    TT_RUN(save_waits_for_silence);
    TT_RUN(save_held_forces_a_waiting_write);
    TT_RUN(save_home_cancels_a_waiting_write);
    TT_RUN(save_failed_write_says_so);
}
