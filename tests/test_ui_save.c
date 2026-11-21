/* SAVE (firmware/ui_save.c): naming a patch and writing it to a user slot (interaction.md, SAVE). */

/* A fresh unit with the user slots on the RAM flash, the VCF page open. */
static void save_setup(void)
{
    store_flash_blank();
    (void)play_init(&ui_p);
    play_attach_store(&ui_p, &store_lib_a, &store_fake_io);
    while (play_store_tick(&ui_p)) {}            /* as the module's first draws */
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
 * a P slot), 8 (a row: 11 to 21) on a fast turn; nothing loads. */
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
    TT_CHECK_EQ(ui_u.save_dest, 7u);            /* +8: 88 to 18 */
    TT_CHECK(play_patch(&ui_p) == t);
    TT_CHECK_EQ(play_prog(&ui_p), PLAY_PROG_P + PATCH_SOUNDTRACK);
}

/* In SAVE, ALGORITHM steps the destination to the next (or previous) empty slot, one empty slot a
 * click, wrapping; with every slot used it stays. */
TT_TEST(save_algorithm_finds_empty_slots)
{
    static store_patch other;
    static const uint32_t used[] = {0, 1, 2, 5};
    uint32_t i;
    save_setup();
    store_from_factory(9, &other);
    for (i = 0; i < 4u; i++)
        TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, used[i], &other), 0);
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.save_dest, 3u);            /* 14: the first empty */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1100);
    TT_CHECK_EQ(ui_u.save_dest, 4u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1200);
    TT_CHECK_EQ(ui_u.save_dest, 6u);            /* 5 (16) is used */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, -2, 1300);
    TT_CHECK_EQ(ui_u.save_dest, 3u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, -1, 1400);
    TT_CHECK_EQ(ui_u.save_dest, 63u);           /* wraps past the used 11..13 */
    for (i = 0; i < STORE_USER_SLOTS; i++)
        if (!store_slot_used(&store_lib_a, i))
            TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, i, &other), 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1500);
    TT_CHECK_EQ(ui_u.save_dest, 63u);           /* all used: stays */
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

/* The name: KNOB2 or SELECT moves the cursor over its 12 places (clamped), KNOB1 steps the character
 * there through space, A-Z, 0-9 and '-', KNOB3 inserts a space (clockwise) or deletes
 * (counterclockwise). The patch is saved with the name as edited, trailing spaces trimmed. */
TT_TEST(save_edits_the_name)
{
    static store_patch out;
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.save_cursor, 0u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1100);         /* S -> T */
    TT_CHECK(strncmp(ui_u.save_name, "TOUNDTRACK", 10) == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 10, 1200);
    TT_CHECK_EQ(ui_u.save_cursor, 10u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1300);         /* space -> A -> B */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 5, 1400);
    TT_CHECK_EQ(ui_u.save_cursor, 11u);                          /* clamped */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, -1, 1450);        /* space -> '-' (wraps) */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -20, 1500);
    TT_CHECK_EQ(ui_u.save_cursor, 0u);                           /* clamped */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 3, 1520);         /* SELECT moves it too */
    TT_CHECK_EQ(ui_u.save_cursor, 3u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, -3, 1540);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -1, 1600);        /* delete the T */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 1, 1700);         /* a space at 0 */
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK(strcmp(out.name, " OUNDTRACKB-") == 0);
}

/* KNOB3 counts every click, fast turns too: 3 counterclockwise deletes 3, 2 clockwise inserts 2. */
TT_TEST(save_insert_delete_counts_every_click)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -3, 1100);        /* SOU gone */
    TT_CHECK(strcmp(ui_u.save_name, "NDTRACK     ") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, 1, 1200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 2, 1300);         /* two spaces at 1 */
    TT_CHECK(strcmp(ui_u.save_name, "N  DTRACK   ") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, -40, 1400);       /* more than there is: all from 1 */
    TT_CHECK(strcmp(ui_u.save_name, "N           ") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 40, 1500);
    TT_CHECK(strcmp(ui_u.save_name, "N           ") == 0);
    TT_CHECK_EQ(ui_u.save_cursor, 1u);
}

/* A HOME tap cancels: SAVE CANCELED, nothing is written and the state goes back. Every other button
 * (EDIT too), and KNOB4, do nothing in SAVE. */
TT_TEST(save_cancels_on_home_only_and_ignores_the_rest)
{
    static store_patch out;
    int32_t res;
    save_setup();
    res = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_ENV, 1100);
    save_tap(UI_BTN_SEL, 1200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 4, 1300);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), res);
    save_tap(UI_BTN_EDIT, 1350);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    save_tap(UI_BTN_HOME, 1400);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVE CANCELED") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), -1);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), -1);
    TT_CHECK_EQ(store_flash.saves, 0u);
}

/* A canceled SAVE keeps its name as a draft (one: the last canceled): the next SAVE of the same patch
 * offers it again, and another patch gets its own name. Once written, the draft is gone. */
TT_TEST(save_keeps_a_canceled_name_as_a_draft)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 1, 1100);         /* S -> T */
    save_tap(UI_BTN_HOME, 1200);
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK(strcmp(ui_u.save_name, "TOUNDTRACK  ") == 0);      /* the draft */
    TT_CHECK_EQ(ui_u.save_cursor, 0u);
    save_tap(UI_BTN_HOME, 2200);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 2300);       /* the next patch, and back */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -1, 2400);
    save_tap(UI_BTN_SAVE, 3000);
    TT_CHECK(strcmp(ui_u.save_name, "TOUNDTRACK  ") == 0);
    save_tap(UI_BTN_HOME, 3100);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 3200);       /* another patch: its own name */
    save_tap(UI_BTN_SAVE, 3300);
    TT_CHECK(strncmp(ui_u.save_name, "TOUNDTRACK", 10) != 0);
    save_tap(UI_BTN_SAVE, 3500);                                /* written */
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
    TT_CHECK_EQ(ui_u.save_draft_prog, UI_SAVE_NO_DRAFT);
}


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
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);
    ui_event(&ui_u, UI_EV_ENC | enc, 3, 1300);  /* an edit while waiting */
    (void)ui_take(&ui_u, 2000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);
    (void)ui_take(&ui_u, 2800);                 /* every 1.5 s: how to force it */
    TT_CHECK(strcmp(ui_banner(&ui_u), "HOLD SAVE: NOW") == 0);
    (void)ui_take(&ui_u, 4300);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);   /* still, past 1.5 s */
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

/* While a write waits, the first HOME tap asks (HOME AGAIN TO CANCEL SAVE), and a second within the
 * banner's 1.5 s cancels it: SAVE CANCELED, nothing written, and nothing else (EDIT stays: those taps
 * don't go home). A second tap later only asks again. */
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
    TT_CHECK(strcmp(ui_banner(&ui_u), "HOME AGAIN TO CANCEL SAVE") == 0);
    TT_CHECK(ui_u.save_waiting);
    save_tap(UI_BTN_HOME, 3400);                /* too late: asks again */
    TT_CHECK(strcmp(ui_banner(&ui_u), "HOME AGAIN TO CANCEL SAVE") == 0);
    TT_CHECK(ui_u.save_waiting);
    save_tap(UI_BTN_HOME, 3900);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVE CANCELED") == 0);
    TT_CHECK(!ui_u.save_waiting);
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

/* In SAVE every button that does nothing there has its LED off (interaction.md): SEL, ENV, FX and LFO
 * go dark while SAVE is open and glow again after it. */
TT_TEST(save_leds_of_idle_buttons_are_off)
{
    static const uint32_t idle[] = { UI_BTN_SEL, UI_BTN_ENV, UI_BTN_FX, UI_BTN_LFO };
    uint32_t i;
    int off = 1, back = 1;
    save_setup();
    save_tap(UI_BTN_HOME, 500);                 /* PLAY: they glow */
    save_tap(UI_BTN_SAVE, 1000);
    for (i = 0; i < 4; i++) off &= ui_led(&ui_u, idle[i]) == UI_LED_OFF;
    save_tap(UI_BTN_HOME, 1200);
    for (i = 0; i < 4; i++) back &= ui_led(&ui_u, idle[i]) == UI_LED_GLOW;
    TT_CHECK(off);
    TT_CHECK(back);
}

/* The number jump is PLAY's and EDIT's: SEL down in SAVE leaves the keys to the notes (ui_key 0: the
 * key sounds) and the keypad LEDs off. */
TT_TEST(save_sel_does_not_take_the_keys)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1100);
    TT_CHECK_EQ(ui_keypad_led(&ui_u, UI_KEY_G3), UI_LED_OFF);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_G3, 1, 1200), 0);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_G3, 0, 1300), 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1400);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
}

/* SEL held as SAVE opens ends the keypad: in SAVE, and after it, the keys are notes again. */
TT_TEST(save_opened_with_sel_held_ends_the_keypad)
{
    save_setup();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 900);
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_keypad_led(&ui_u, UI_KEY_G3), UI_LED_OFF);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_G3, 1, 1200), 0);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_G3, 0, 1250), 0);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1300);
    save_tap(UI_BTN_HOME, 1400);                /* cancel: back to EDIT */
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_A3, 1, 1600), 0);
    TT_CHECK_EQ(ui_key(&ui_u, UI_KEY_A3, 0, 1650), 0);
}

/* SAVE has its own screen (draw.c draws every band through ui_screen), and opening it takes the number
 * jump's screen down: after SAVE, the page shows, not the jump. */
TT_TEST(save_has_the_screen_and_takes_the_jump_down)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_SAVE);
    save_tap(UI_BTN_HOME, 1200);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PAGE);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 2000);
    (void)ui_take(&ui_u, 2500);                 /* SEL held 400 ms: the jump's screen */
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_JUMP);
    save_tap(UI_BTN_SAVE, 2600);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_SAVE);
    TT_CHECK_EQ(ui_take(&ui_u, 2700) & UI_D_FULL, UI_D_FULL);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 2800);
    save_tap(UI_BTN_HOME, 2900);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PAGE);
    (void)ui_take(&ui_u, 3500);
    TT_CHECK_EQ(ui_screen(&ui_u), UI_SCREEN_PAGE);
}

/* With no patch store (a host without one, or a SAVE before the first draw loads it), SAVE still opens,
 * and a second tap onto a slot other than the patch's own says SAVE FAILED: it never reads the store. */
TT_TEST(save_without_a_store_says_save_failed)
{
    (void)play_init(&ui_p);                     /* no play_attach_store: lib is NULL */
    ui_init(&ui_u, &ui_p);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, 0);
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK(ui_u.save_dest != play_prog(&ui_p));
    save_tap(UI_BTN_SAVE, 1200);
    TT_CHECK(ui_u.state != UI_SAVE);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVE FAILED") == 0);
}

/* The stress test (test_ui.c's ui_hammer) with the user slots on the RAM flash, so SAVE names, writes,
 * asks before replacing and waits for silence among everything else */
TT_TEST(save_hammered_with_a_store_keeps_its_states)
{
    uint32_t i, used = 0;
    save_setup();
    TT_CHECK_EQ(ui_hammer(), 0u);
    for (i = 0; i < 64u; i++)
        used += store_slot_used(&store_lib_a, i) != 0;
    printf("    %u user slots written\n", (unsigned)used);
    TT_CHECK(used > 0u);                        /* (the writes happened: SAVE was really hammered) */
}

/* #201: with the sequencer running on rests, no voice sounds, yet a SAVE waits: the transport counts as
 * sound. A stop lets it write. */
TT_TEST(save_waits_while_the_sequencer_runs)
{
    uint32_t k;
    save_setup();
    seq_play(play_seq(&ui_p));                  /* every step a rest: nothing sounds */
    save_render(4);
    TT_CHECK_EQ(ui_p.sounding, 0u);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    (void)ui_take(&ui_u, 2000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);
    TT_CHECK_EQ(store_flash.saves, 0u);
    seq_stop(play_seq(&ui_p));
    for (k = 0; k < 4 && !play_silent(&ui_p); k++)
        save_render(1);
    (void)ui_take(&ui_u, 2500);
    TT_CHECK_EQ(store_flash.saves, 1u);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
}

/* #201: PLAY armed and waiting for MIDI clock (G13 MIDI) counts as running: a start could come mid-write,
 * so a SAVE waits; PLAY again (disarmed) lets it write */
TT_TEST(save_waits_while_play_waits_for_the_clock)
{
    save_setup();
    seq_set_clock_source(play_seq(&ui_p), SEQ_CLK_MIDI);
    save_tap(UI_BTN_PLAY, 900);
    TT_CHECK(seq_waiting(play_seq(&ui_p)));
    save_render(4);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    (void)ui_take(&ui_u, 2000);
    TT_CHECK_EQ(store_flash.saves, 0u);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);
    save_tap(UI_BTN_PLAY, 2100);                /* disarmed */
    TT_CHECK(!seq_waiting(play_seq(&ui_p)));
    save_render(4);
    (void)ui_take(&ui_u, 2500);
    TT_CHECK_EQ(store_flash.saves, 1u);
}

/* #201: with the sequencer playing, holding SAVE 1 s still forces the waiting write (Q5), and HOME still
 * cancels one */
TT_TEST(save_force_and_cancel_work_while_the_sequencer_runs)
{
    save_setup();
    seq_play(play_seq(&ui_p));
    save_render(4);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    (void)ui_take(&ui_u, 1300);
    TT_CHECK(ui_u.save_waiting);
    save_tap(UI_BTN_HOME, 1400);                /* HOME asks, */
    TT_CHECK(ui_u.save_waiting);
    save_tap(UI_BTN_HOME, 1600);                /* and again cancels */
    TT_CHECK(!ui_u.save_waiting);
    TT_CHECK_EQ(store_flash.saves, 0u);
    save_tap(UI_BTN_SAVE, 2000);
    save_tap(UI_BTN_SAVE, 2200);
    (void)ui_take(&ui_u, 2300);
    TT_CHECK(ui_u.save_waiting);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SAVE, 0, 2400);
    (void)ui_take(&ui_u, 3500);                 /* held 1.1 s: forced */
    TT_CHECK_EQ(store_flash.saves, 1u);
    TT_CHECK(seq_playing(play_seq(&ui_p)));
}

/* ---- SAVE in SEQ view: the pattern (interaction.md, SAVE; S6) ---- */

static pat_lib save_plib;

/* save_setup with the pattern slots too, in SEQ view, step 1 holding a C4 (the pattern edited) */
static void save_seq_setup(void)
{
    static const uint8_t c4 = 60;
    store_flash_blank();
    (void)play_init(&ui_p);
    play_attach_store(&ui_p, &store_lib_a, &store_fake_io);
    play_attach_patterns(&ui_p, &save_plib);
    while (play_store_tick(&ui_p)) {}
    ui_init(&ui_u, &ui_p);
    save_tap(UI_BTN_SEQ, 100);
    seq_set_step(play_seq(&ui_p), 0, &c4, 1, 100);
}

/* A SAVE tap in SEQ view opens SAVE for the pattern: the destination is the pattern slot playing, the
 * name the pattern's (an empty slot's: blank). A second tap writes the pattern there, not a patch:
 * SAVED PATTERN 11, the pattern no longer edited, and back to SEQ view. */
TT_TEST(save_in_seq_view_writes_the_pattern)
{
    static seq_pattern out;
    char name[STORE_NAME_LEN + 1u];
    save_seq_setup();
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(seq_edited(play_seq(&ui_p)));
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_dest, pattern_running(play_patterns(&ui_p), 0));
    TT_CHECK(strcmp(ui_u.save_name, "            ") == 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1100);   /* "B" */
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), 0);
    TT_CHECK(strcmp(name, "B") == 0);
    TT_CHECK_EQ(out.step[0].n, 1u);
    TT_CHECK_EQ(out.step[0].note[0], 60u);
    TT_CHECK(!store_slot_used(&store_lib_a, 0));          /* no patch written */
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED PATTERN 11") == 0);
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
}

/* A step's bend, mod and move set on S2 are saved with the pattern and come back with it: written to
 * the slot, then read back by browsing away and back with PRESETS (#42) */
TT_TEST(save_keeps_a_steps_bend_mod_and_move)
{
    static seq_pattern out;
    char name[STORE_NAME_LEN + 1u], n[UI_VALUE_LEN], un[UI_VALUE_LEN];
    save_seq_setup();
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 200);   /* S2, the cursor on step 1 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB2, -5, 300);      /* bend -50 % */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB3, 4, 400);       /* mod 40 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, 500);       /* GLIDE */
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), 0);
    TT_CHECK_EQ(out.step[0].bend, -50 * 8192 / 100);
    TT_CHECK_EQ(out.step[0].mod, 40u);
    TT_CHECK_EQ(out.step[0].glide, 1u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 3000);    /* 12, empty: the step is a rest, centered */
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 0)->bend, 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -1, 3100);   /* back to 11 */
    ui_seq_column(&ui_u, 1, n, un);
    TT_CHECK(strcmp(n, "-50") == 0);
    ui_seq_column(&ui_u, 2, n, un);
    TT_CHECK(strcmp(n, "40") == 0);
    ui_seq_column(&ui_u, 3, n, un);
    TT_CHECK(strcmp(n, "GLIDE") == 0);
}

/* PRESETS and ALGORITHM pick among the pattern slots: ALGORITHM finds the next empty pattern slot,
 * whatever the patch slots hold; onto another pattern the first tap asks REPLACE 12 <ITS NAME>? */
TT_TEST(save_in_seq_view_picks_pattern_slots)
{
    static store_patch other;
    static seq_pattern there;
    store_from_factory(9, &other);
    save_seq_setup();
    seq_pattern_empty(&there);
    there.length = 4;
    TT_CHECK_EQ(pat_slot_write(&save_plib, &store_fake_io, 1, &there, "GROOVE"), 0);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 2, &other), 0);   /* a patch in 13 */
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 1100);
    TT_CHECK_EQ(ui_u.save_dest, 2u);            /* 13: its pattern slot is empty; 12 isn't */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -1, 1200);
    TT_CHECK_EQ(ui_u.save_dest, 1u);
    save_tap(UI_BTN_SAVE, 1300);
    TT_CHECK_EQ(ui_u.save_step, (uint32_t)UI_SAVE_CONFIRM);
    TT_CHECK(strcmp(ui_u.save_msg, "REPLACE 12 GROOVE?") == 0);
    save_tap(UI_BTN_SAVE, 1400);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED PATTERN 12") == 0);
    TT_CHECK_EQ(pattern_running(play_patterns(&ui_p), 0), 1u);
}

/* While the sequencer plays, the pattern's write waits like a patch's: SAVING 11 WHEN SILENT, and an
 * edit meanwhile doesn't change what is written; the stop writes the snapshot. */
TT_TEST(save_in_seq_view_waits_for_the_stop)
{
    static const uint8_t e4 = 64;
    static seq_pattern out;
    char name[STORE_NAME_LEN + 1u];
    uint32_t k;
    save_seq_setup();
    seq_play(play_seq(&ui_p));
    save_render(4);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);
    (void)ui_take(&ui_u, 2000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVING 11 WHEN SILENT") == 0);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    seq_set_step(play_seq(&ui_p), 0, &e4, 1, 100);       /* after the snapshot */
    TT_CHECK(!pat_slot_used(&save_plib, 0));
    seq_stop(play_seq(&ui_p));
    for (k = 0; k < 20000u && !play_silent(&ui_p); k++)   /* SOUNDTRACK's release: seconds */
        save_render(1);
    TT_CHECK(play_silent(&ui_p));
    (void)ui_take(&ui_u, 2500);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), 0);
    TT_CHECK_EQ(out.step[0].note[0], 60u);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED PATTERN 11") == 0);
    TT_CHECK(seq_edited(play_seq(&ui_p)));                /* E4 since: still edited */
}

/* A canceled pattern SAVE keeps its name as the pattern's draft; the patch's next SAVE doesn't take it,
 * though both play slot 11. */
TT_TEST(save_in_seq_view_keeps_its_own_draft)
{
    static store_patch other;
    save_seq_setup();
    store_from_factory(9, &other);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 0, &other), 0);
    TT_CHECK_EQ(play_load_prog(&ui_p, 0), 0);  /* the patch from 11, the pattern from 11 */
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1100);   /* "B" */
    save_tap(UI_BTN_HOME, 1200);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    save_tap(UI_BTN_SAVE, 1400);
    TT_CHECK(ui_u.save_name[0] == 'B');         /* the pattern's draft */
    save_tap(UI_BTN_HOME, 1500);
    save_tap(UI_BTN_SEQ, 1600);                 /* back to EDIT */
    TT_CHECK(ui_u.state != UI_SEQ);
    save_tap(UI_BTN_SAVE, 1700);
    TT_CHECK(ui_u.save_name[0] == other.name[0]);   /* the patch's own name, not B */
    TT_CHECK(ui_u.save_name[0] != 'B');
}

/* slot 12 holds OTHER; SAVE open on it */
static void save_on_other(void)
{
    static store_patch other;
    save_setup();
    store_from_factory(9, &other);
    memset(other.name, 0, sizeof other.name);
    memcpy(other.name, "OTHER", 5);
    (void)store_slot_write(&store_lib_a, &store_fake_io, 1, &other);
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1100);
}

/* #303: in SAVE, SEL held 1.5 s (on its release, as SEQ view's clear) on a slot that holds a patch asks
 * DELETE 12 OTHER? and deletes nothing yet; on an EMPTY slot it does nothing, and a short SEL tap does
 * nothing either */
TT_TEST(save_sel_held_asks_to_delete)
{
    static store_patch out;
    save_on_other();
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1200);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 1300);   /* a tap */
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 1400);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 2900);   /* held 1.5 s */
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_DELETE);
    TT_CHECK(strcmp(ui_u.save_msg, "DELETE 12 OTHER?") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 3000);  /* 13: empty */
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 3100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 4700);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
}

/* SEL held on slot 12 (OTHER): the question */
static void save_ask_delete(uint32_t ms)
{
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, ms);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, ms + 1600u);
}

/* #303: a second SEL tap deletes the slot. Silent, at once: DELETED 12 shows, the slot is empty (in flash
 * too) and SAVE stays open on it, now EMPTY */
TT_TEST(save_sel_again_deletes_the_slot)
{
    static store_patch out;
    save_on_other();
    save_ask_delete(1200);
    TT_CHECK(play_silent(&ui_p));
    save_tap(UI_BTN_SEL, 3000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), -1);
    store_lib_load(&store_lib_b, &store_fake_io);
    TT_CHECK_EQ(store_slot_read(&store_lib_b, 1, &out), -1);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETED 12") == 0);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_dest, 1u);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
    TT_CHECK(!store_slot_used(&store_lib_a, 1));
}

/* #303: with a note sounding, the delete waits as a save does (DELETING 12 WHEN SILENT, back on the screen
 * SAVE came from) and is done when silent (DELETED 12); a waiting delete canceled (HOME twice) deletes
 * nothing, and the next SAVE writes, not deletes */
TT_TEST(save_delete_waits_for_silence_and_cancels)
{
    static store_patch out;
    uint32_t k;
    save_on_other();
    play_note(&ui_p, 60, 100);
    save_render(4);
    save_ask_delete(1200);
    save_tap(UI_BTN_SEL, 3000);
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETING 12 WHEN SILENT") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    save_tap(UI_BTN_HOME, 3200);
    TT_CHECK(strcmp(ui_banner(&ui_u), "HOME AGAIN TO CANCEL DELETE") == 0);   /* (its own words, Codex P3) */
    save_tap(UI_BTN_HOME, 3400);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETE CANCELED") == 0);
    play_note(&ui_p, 60, 0);
    for (k = 0; k < 2000 && !play_silent(&ui_p); k++) save_render(1);
    (void)ui_take(&ui_u, 6000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    save_tap(UI_BTN_SAVE, 7000);                /* SAVE onto 11 (empty): a write */
    save_tap(UI_BTN_SAVE, 7200);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 0, &out), 0);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED 11") == 0);
    play_note(&ui_p, 60, 100);                  /* and a waiting delete is done when silent */
    save_render(4);
    save_tap(UI_BTN_SAVE, 8000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 8100);
    save_ask_delete(8200);
    save_tap(UI_BTN_SEL, 10000);
    play_note(&ui_p, 60, 0);
    for (k = 0; k < 2000 && !play_silent(&ui_p); k++) save_render(1);
    (void)ui_take(&ui_u, 11000);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), -1);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETED 12") == 0);
}

/* #303: the question canceled: HOME cancels it and SAVE stays open (a second HOME leaves, as before); a SAVE
 * tap while it shows asks REPLACE, as on any slot that holds one; nothing is deleted */
TT_TEST(save_delete_question_cancels)
{
    static store_patch out;
    save_on_other();
    save_ask_delete(1200);
    save_tap(UI_BTN_HOME, 3000);
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
    save_ask_delete(3200);
    save_tap(UI_BTN_SAVE, 5000);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_CONFIRM);
    TT_CHECK(strcmp(ui_u.save_msg, "REPLACE 12 OTHER?") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    TT_CHECK(strcmp(out.name, "OTHER") == 0);
    save_tap(UI_BTN_HOME, 5200);                /* (at REPLACE, one HOME leaves SAVE, as before) */
    TT_CHECK_EQ(ui_u.state, UI_EDIT);
}

/* #303: deleting the slot that is loaded leaves the edit buffer as it is (what plays doesn't change), shown
 * edited: it no longer matches a saved patch */
TT_TEST(save_delete_of_the_loaded_slot_keeps_the_sound_edited)
{
    int32_t res;
    uint32_t enc;
    save_setup();
    enc = ui_goto(FM_PARAM_FILTER_RESONANCE);
    ui_event(&ui_u, UI_EV_ENC | enc, 5, 500);
    res = play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE);
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);                /* saved to 11: loaded, unedited */
    TT_CHECK_EQ(play_prog(&ui_p), 0u);
    TT_CHECK(!play_edited(&ui_p));
    save_tap(UI_BTN_SAVE, 2000);                /* SAVE opens on 11 */
    TT_CHECK_EQ(ui_u.save_dest, 0u);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 2100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 3700);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_DELETE);
    save_tap(UI_BTN_SEL, 4000);
    TT_CHECK(!store_slot_used(&store_lib_a, 0));
    TT_CHECK_EQ(play_get_param(&ui_p, FM_PARAM_FILTER_RESONANCE), res);
    TT_CHECK(play_edited(&ui_p));
}

/* #303: a deleted slot is an empty slot everywhere: PRESETS skips it, SAVE's ALGORITHM (next empty) finds it,
 * and a program change to it is ignored */
TT_TEST(save_deleted_slot_is_empty_everywhere)
{
    save_on_other();                            /* 12 holds OTHER */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + PATCH_COUNT - 1u, 1), 1u);   /* P32 -> 12 */
    save_ask_delete(1200);
    save_tap(UI_BTN_SEL, 3000);
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + PATCH_COUNT - 1u, 1), PLAY_PROG_P);   /* P32 -> P1 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, -1, 3200);   /* on 11 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_ALGORITHM, 1, 3300);  /* the next empty: 12 again */
    TT_CHECK_EQ(ui_u.save_dest, 1u);
    TT_CHECK_EQ(play_load_prog(&ui_p, 1), -1);
    TT_CHECK_EQ(play_prog(&ui_p), PLAY_PROG_P + PATCH_SOUNDTRACK);
}

/* #303 in SEQ view's SAVE: SEL held asks DELETE PATTERN 11 B?; with the pattern playing the delete waits as a
 * save does (DELETING 11 WHEN SILENT) and the playing copy plays on; stopped and silent, the slot is empty
 * (DELETED PATTERN 11), the pattern in the sequencer is as it was, now edited (the slot would load empty) */
TT_TEST(save_in_seq_view_deletes_the_playing_pattern)
{
    static seq_pattern out;
    char name[STORE_NAME_LEN + 1u];
    uint32_t k;
    save_seq_setup();
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1100);   /* "B" */
    save_tap(UI_BTN_SAVE, 2000);
    TT_CHECK(pat_slot_used(&save_plib, 0));
    TT_CHECK(!seq_edited(play_seq(&ui_p)));
    seq_play(play_seq(&ui_p));
    save_render(4);
    save_tap(UI_BTN_SAVE, 3000);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 3100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 4700);
    TT_CHECK(strcmp(ui_u.save_msg, "DELETE 11 B?") == 0);   /* (SEQ's SAVE says PATTERN) */
    save_tap(UI_BTN_SEL, 5000);
    (void)ui_take(&ui_u, 5100);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETING 11 WHEN SILENT") == 0);
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    TT_CHECK(pat_slot_used(&save_plib, 0));
    TT_CHECK(seq_playing(play_seq(&ui_p)));
    seq_stop(play_seq(&ui_p));
    for (k = 0; k < 20000u && !play_silent(&ui_p); k++)
        save_render(1);
    (void)ui_take(&ui_u, 6000);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), -1);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETED PATTERN 11") == 0);
    TT_CHECK_EQ(play_seq(&ui_p)->edit.step[0].note[0], 60u);
    TT_CHECK(seq_edited(play_seq(&ui_p)));
}

/* #303 (FW review): the longest question, a pattern's with a 12-character name, fits the screen's 30
 * characters (8 px each in 240) */
TT_TEST(save_delete_question_fits_the_screen)
{
    save_seq_setup();
    save_tap(UI_BTN_SAVE, 1000);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 2, 1100);
    memcpy(ui_u.save_name, "ABCDEFGHIJKL", 12);
    save_tap(UI_BTN_SAVE, 2000);                /* saved as pattern 11, a 12-character name */
    TT_CHECK(pat_slot_used(&save_plib, 0));
    save_tap(UI_BTN_SAVE, 3000);
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 3100);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 4700);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_DELETE);
    if (strlen(ui_u.save_msg) > 30u) printf("    %u characters: %s\n", (unsigned)strlen(ui_u.save_msg), ui_u.save_msg);
    TT_CHECK(strlen(ui_u.save_msg) <= 30u);
}

/* #303 (FW review): a SEL hold that began before SAVE opened (the number jump's) doesn't ask to delete when
 * it is let go, even 1.5 s on, on a slot that holds a patch */
TT_TEST(save_sel_held_from_before_save_does_not_ask)
{
    save_setup();
    save_tap(UI_BTN_SAVE, 1000);
    save_tap(UI_BTN_SAVE, 1200);                /* 11 holds the patch, loaded */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_SEL, 0, 2000);
    save_tap(UI_BTN_SAVE, 2100);                /* SAVE opens on 11 with SEL held */
    TT_CHECK_EQ(ui_u.state, UI_SAVE);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_SEL, 0, 3700);
    TT_CHECK_EQ(ui_u.save_step, UI_SAVE_EDITING);
}

/* #324 T7 (Eric, 2026-11-21; it replaces #303's "takes over, edited against empty"): a pattern slot deleted
 * while it is queued to take over (a forced delete while SEQ plays) cancels the queue: the running
 * pattern plays on, unedited, and nothing is queued (NEXT PATTERN clears) */
TT_TEST(save_delete_of_the_queued_pattern_cancels_the_queue)
{
    static seq_pattern pat;
    uint32_t k, t;
    save_seq_setup();
    t = pattern_snapshot(play_patterns(&ui_p), &pat);
    TT_CHECK_EQ(pattern_save(play_patterns(&ui_p), 0, &pat, "A", t), 0);
    TT_CHECK_EQ(pattern_save(play_patterns(&ui_p), 1, &pat, "B", t), 0);
    pattern_queue(play_patterns(&ui_p), 0);
    seq_play(play_seq(&ui_p));
    save_render(4);
    pattern_queue(play_patterns(&ui_p), 1);                /* queued: 12 takes over after the pass */
    TT_CHECK(seq_next_queued(play_seq(&ui_p)));
    TT_CHECK_EQ(pattern_delete(play_patterns(&ui_p), 1), 0);
    TT_CHECK_EQ(pattern_queued(play_patterns(&ui_p), 0), -1);
    TT_CHECK(!seq_next_queued(play_seq(&ui_p)));
    for (k = 0; k < 200u; k++) {                           /* passes on */
        save_render(1);
        (void)pattern_takeover(play_patterns(&ui_p));
    }
    TT_CHECK_EQ(pattern_running(play_patterns(&ui_p), 0), 0u);
    TT_CHECK(!pattern_edited(play_patterns(&ui_p)));
}

/* #324 T4: B takes over in the audio interrupt and SAVE comes before the next draw: SAVE offers B's slot
 * and B's name, and the write lands there, not over A's slot */
TT_TEST(save_right_after_a_takeover_targets_the_running_slot)
{
    static seq_pattern pat, out;
    static const uint8_t e4 = 64;
    char name[STORE_NAME_LEN + 1u];
    uint32_t k, t;
    save_seq_setup();                                       /* step 1 C4 */
    t = pattern_snapshot(play_patterns(&ui_p), &pat);
    TT_CHECK_EQ(pattern_save(play_patterns(&ui_p), 0, &pat, "A", t), 0);
    seq_set_step(play_seq(&ui_p), 0, &e4, 1, 100);
    t = pattern_snapshot(play_patterns(&ui_p), &pat);
    TT_CHECK_EQ(pattern_save(play_patterns(&ui_p), 1, &pat, "B", t), 0);
    pattern_queue(play_patterns(&ui_p), 0);                /* stopped: A at once */
    seq_play(play_seq(&ui_p));
    save_render(4);
    pattern_queue(play_patterns(&ui_p), 1);
    for (k = 0; k < 40000u && seq_next_queued(play_seq(&ui_p)); k++)
        save_render(1);                                     /* B takes over: no draw since */
    TT_CHECK(!seq_next_queued(play_seq(&ui_p)));
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.save_dest, 1u);                        /* 12, B's */
    TT_CHECK(strcmp(ui_u.save_name, "B           ") == 0);
    save_tap(UI_BTN_SAVE, 1200);                            /* waits: playing */
    seq_stop(play_seq(&ui_p));
    for (k = 0; k < 4000u && !play_silent(&ui_p); k++)
        save_render(1);
    (void)ui_take(&ui_u, 3000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED PATTERN 12") == 0);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 1, &out, name), 0);
    TT_CHECK_EQ(out.step[0].note[0], 64u);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), 0);
    TT_CHECK_EQ(out.step[0].note[0], 60u);                  /* A's slot as it was */
}

/* #324 (FW review): stopped but not silent, a SAVE of the pattern waits; PRESETS then loads another slot
 * at once. When the save lands it writes its snapshot to its slot, and the pattern loaded since stays the
 * running one: its slot, its name, its notes */
TT_TEST(save_waiting_through_a_pattern_load_keeps_the_loaded_one)
{
    static seq_pattern b, out;
    char name[STORE_NAME_LEN + 1u];
    uint32_t k;
    save_seq_setup();                                       /* 11, edited: C4 on step 1 */
    seq_pattern_empty(&b);
    TT_CHECK_EQ(pat_slot_write(&save_plib, &store_fake_io, 1, &b, "B"), 0);   /* 12: rests */
    play_note(&ui_p, 60, 100);
    save_render(4);
    TT_CHECK(!play_silent(&ui_p));
    save_tap(UI_BTN_SAVE, 1000);
    TT_CHECK_EQ(ui_u.save_dest, 0u);
    save_tap(UI_BTN_SAVE, 1200);                            /* waits for silence */
    TT_CHECK_EQ(ui_u.state, UI_SEQ);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1300);  /* edited: the warning */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 1400);  /* 12 at once */
    TT_CHECK_EQ(pattern_running(play_patterns(&ui_p), 0), 1u);
    play_note(&ui_p, 60, 0);
    for (k = 0; k < 4000u && !play_silent(&ui_p); k++)
        save_render(1);
    (void)ui_take(&ui_u, 3000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "SAVED PATTERN 11") == 0);
    TT_CHECK_EQ(pat_slot_read(&save_plib, 0, &out, name), 0);
    TT_CHECK_EQ(out.step[0].note[0], 60u);                  /* the snapshot, in 11 */
    TT_CHECK_EQ(pattern_running(play_patterns(&ui_p), name), 1u);
    TT_CHECK(strcmp(name, "B") == 0);
    TT_CHECK_EQ(seq_get_step(play_seq(&ui_p), 0)->kind, SEQ_REST);   /* 12's, as loaded */
}

/* #303: a delete whose flash write fails says DELETE FAILED and leaves the slot as it was */
TT_TEST(save_failed_delete_says_so)
{
    static store_patch out;
    save_on_other();
    save_ask_delete(1200);
    store_flash.fail = 1;
    save_tap(UI_BTN_SEL, 3000);
    TT_CHECK(strcmp(ui_banner(&ui_u), "DELETE FAILED") == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 1, &out), 0);
    TT_CHECK(strcmp(out.name, "OTHER") == 0);
    store_flash.fail = 0;
}

static void run_ui_save_tests(void)
{
    TT_RUN(save_sel_held_asks_to_delete);
    TT_RUN(save_sel_again_deletes_the_slot);
    TT_RUN(save_delete_waits_for_silence_and_cancels);
    TT_RUN(save_delete_question_cancels);
    TT_RUN(save_delete_of_the_loaded_slot_keeps_the_sound_edited);
    TT_RUN(save_deleted_slot_is_empty_everywhere);
    TT_RUN(save_in_seq_view_deletes_the_playing_pattern);
    TT_RUN(save_delete_question_fits_the_screen);
    TT_RUN(save_sel_held_from_before_save_does_not_ask);
    TT_RUN(save_delete_of_the_queued_pattern_cancels_the_queue);
    TT_RUN(save_right_after_a_takeover_targets_the_running_slot);
    TT_RUN(save_waiting_through_a_pattern_load_keeps_the_loaded_one);
    TT_RUN(save_failed_delete_says_so);
    TT_RUN(save_hammered_with_a_store_keeps_its_states);
    TT_RUN(save_tap_opens_save_at_the_first_empty_slot);
    TT_RUN(save_writes_the_edit_buffer_when_silent);
    TT_RUN(save_presets_picks_the_destination);
    TT_RUN(save_algorithm_finds_empty_slots);
    TT_RUN(save_asks_before_replacing_another_patch);
    TT_RUN(save_edits_the_name);
    TT_RUN(save_insert_delete_counts_every_click);
    TT_RUN(save_keeps_a_canceled_name_as_a_draft);
    TT_RUN(save_cancels_on_home_only_and_ignores_the_rest);
    TT_RUN(save_waits_for_silence);
    TT_RUN(save_held_forces_a_waiting_write);
    TT_RUN(save_home_cancels_a_waiting_write);
    TT_RUN(save_failed_write_says_so);
    TT_RUN(save_leds_of_idle_buttons_are_off);
    TT_RUN(save_sel_does_not_take_the_keys);
    TT_RUN(save_opened_with_sel_held_ends_the_keypad);
    TT_RUN(save_has_the_screen_and_takes_the_jump_down);
    TT_RUN(save_without_a_store_says_save_failed);
    TT_RUN(save_waits_while_the_sequencer_runs);
    TT_RUN(save_waits_while_play_waits_for_the_clock);
    TT_RUN(save_force_and_cancel_work_while_the_sequencer_runs);
    TT_RUN(save_in_seq_view_writes_the_pattern);
    TT_RUN(save_keeps_a_steps_bend_mod_and_move);
    TT_RUN(save_in_seq_view_picks_pattern_slots);
    TT_RUN(save_in_seq_view_waits_for_the_stop);
    TT_RUN(save_in_seq_view_keeps_its_own_draft);
}
