/* The boot screen (#50, Eric's picks 2026-11-15): Horizon art, at least 1.5 s and until the flash banks
 * are read (5 s at most), any knob or button skips it, one LED sweep over the keys. */

/* the boot screen begun at the first ui_take, the banks read at once */
static void boot_setup(void)
{
    ui_setup();
    ui_boot_begin(&ui_u);
}

/* It shows at least 1.5 s from the first draw, then PLAY, redrawn whole */
TT_TEST(boot_shows_at_least_1_5_s)
{
    boot_setup();
    ui_boot_loaded(&ui_u);
    (void)ui_take(&ui_u, 1000);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_BOOT);
    (void)ui_take(&ui_u, 2499);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_BOOT);
    TT_CHECK(ui_take(&ui_u, 2500) & UI_D_FULL);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
}

/* Past 1.5 s it waits for the flash banks; if they never finish (a read that fails or stalls) it goes
 * to PLAY at 5 s anyway, so the screen can't hang */
TT_TEST(boot_waits_for_the_banks_at_most_5_s)
{
    boot_setup();
    (void)ui_take(&ui_u, 1000);
    (void)ui_take(&ui_u, 3000);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_BOOT);   /* banks still reading */
    ui_boot_loaded(&ui_u);
    TT_CHECK(ui_take(&ui_u, 3100) & UI_D_FULL);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
    boot_setup();
    (void)ui_take(&ui_u, 1000);
    (void)ui_take(&ui_u, 5999);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_BOOT);
    (void)ui_take(&ui_u, 6000);                               /* never loaded: the ceiling */
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
}

/* Any knob or button skips it: PLAY at once, and that event does nothing else (a turn changes no value,
 * EDIT opens no page). A key skips it too and still plays (ui_key leaves it to the engine) */
TT_TEST(boot_any_control_skips_it)
{
    int32_t before;
    boot_setup();
    (void)ui_take(&ui_u, 1000);
    before = ui_slot_value(&ui_u, ui_page_slot(&ui_u, 0));
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB1, 3, 1100);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
    TT_CHECK_EQ(ui_slot_value(&ui_u, ui_page_slot(&ui_u, 0)), before);
    TT_CHECK(ui_take(&ui_u, 1200) & UI_D_FULL);
    boot_setup();
    (void)ui_take(&ui_u, 1000);
    ui_tap(UI_BTN_EDIT, 1100);
    TT_CHECK_EQ(ui_u.state, UI_PLAY);
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
    boot_setup();
    (void)ui_take(&ui_u, 1000);
    TT_CHECK_EQ(ui_key(&ui_u, 5, 1, 1100), 0);                /* sounds */
    TT_CHECK_EQ(ui_screen(&ui_u), (uint32_t)UI_SCREEN_PLAY);
    TT_CHECK_EQ(ui_key(&ui_u, 5, 0, 1150), 0);
}

static void run_ui_boot_tests(void)
{
    TT_RUN(boot_shows_at_least_1_5_s);
    TT_RUN(boot_waits_for_the_banks_at_most_5_s);
    TT_RUN(boot_any_control_skips_it);
}
