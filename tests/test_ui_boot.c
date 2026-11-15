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

static void run_ui_boot_tests(void)
{
    TT_RUN(boot_shows_at_least_1_5_s);
}
