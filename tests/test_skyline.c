/* The skyline with a laser line (Eric, 2026-11-27): PLAY's art and the boot screen's, drawn from
 * docs/ui/mockups/skyline_art.h (made by skyline.py --table) a canvas strip at a time, as draw.c draws them,
 * on the theme's bg, must give the mockup's pixels: the art rows' FNV-1a hash as skyline.py draws them
 * (the PNGs in docs/ui/mockups/skyline/ are those pixels, three times as big). */
#include "../firmware/skyline.h"
#include "../firmware/ui_map.h"
#include "../docs/ui/mockups/skyline_art.h"

static uint16_t sky_fb[240 * 240];
static int32_t sky_oy, sky_rows;              /* the canvas strip: screen rows sky_oy .. sky_oy + sky_rows - 1 */
static uint32_t sky_calls;

static void sky_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t xx, yy;                           /* canvas coordinates, clipped to the strip as cv_rect does */
    sky_calls++;
    for (yy = y < 0 ? 0 : y; yy < y + h && yy < sky_rows; yy++)
        for (xx = x < 0 ? 0 : x; xx < x + w && xx < 240; xx++)
            sky_fb[(sky_oy + yy) * 240 + xx] = c;
}

/* screen's art in strips (top, rows) on bg, then the hash of rows top .. top + n - 1 */
static uint32_t sky_draw_hash(uint32_t screen, uint8_t theme, uint16_t bg, const int32_t (*strip)[2],
                              uint32_t strips, int32_t top, int32_t n)
{
    uint32_t s, v = 0x811C9DC5u;
    int32_t i;
    for (i = 0; i < 240 * 240; i++)
        sky_fb[i] = 0x1234u;                  /* anything not drawn shows */
    for (s = 0; s < strips; s++) {
        sky_oy = strip[s][0];
        sky_rows = strip[s][1];
        for (i = 0; i < 240 * sky_rows; i++)
            sky_fb[sky_oy * 240 + i] = bg;    /* cv_begin's clear */
        skyline_draw(screen, theme, sky_oy, sky_rows, sky_rect);
    }
    for (i = top * 240; i < (top + n) * 240; i++) {
        v = (v ^ (sky_fb[i] & 0xFFu)) * 0x01000193u;
        v = (v ^ (uint32_t)(sky_fb[i] >> 8)) * 0x01000193u;
    }
    return v;
}

TT_TEST(skyline_play_slot_is_the_mockups_pixels_in_both_themes)
{
    static const int32_t SLOT[1][2] = {{78, 60}};   /* draw.c's slot canvas: rows 78..137 */
    TT_CHECK_EQ(UI_MEMBRANE.bg, SKY_BG_MEMBRANE);
    TT_CHECK_EQ(UI_NIGHT_PANEL.bg, SKY_BG_NIGHT_PANEL);
    TT_CHECK_EQ(sky_draw_hash(SKYLINE_PLAY, UI_MEMBRANE.sky, UI_MEMBRANE.bg, SLOT, 1, SKY_PLAY_TOP, SKY_PLAY_ROWS),
                SKY_PLAY_HASH_MEMBRANE);
    TT_CHECK_EQ(sky_draw_hash(SKYLINE_PLAY, UI_NIGHT_PANEL.sky, UI_NIGHT_PANEL.bg, SLOT, 1, SKY_PLAY_TOP,
                              SKY_PLAY_ROWS), SKY_PLAY_HASH_NIGHT_PANEL);
}

TT_TEST(skyline_boot_art_is_the_mockups_pixels_in_both_themes)
{
    static const int32_t BOOT[2][2] = {{0, 80}, {80, 60}};   /* draw.c's boot strips over the art: rows 0..139 */
    TT_CHECK_EQ(sky_draw_hash(SKYLINE_BOOT, UI_MEMBRANE.sky, UI_MEMBRANE.bg, BOOT, 2, SKY_BOOT_TOP, SKY_BOOT_ROWS),
                SKY_BOOT_HASH_MEMBRANE);
    TT_CHECK_EQ(sky_draw_hash(SKYLINE_BOOT, UI_NIGHT_PANEL.sky, UI_NIGHT_PANEL.bg, BOOT, 2, SKY_BOOT_TOP,
                              SKY_BOOT_ROWS), SKY_BOOT_HASH_NIGHT_PANEL);
}

/* A strip costs its own fills only: those that reach none of its rows aren't drawn (each is a cv_rect call
 * on the device). PLAY's slot takes all 204; the boot art's 147 take 87 in rows 0..79 and 69 in 80..139
 * (9 cross row 80); the text strips below the art take none. */
static uint32_t sky_strip_calls(uint32_t screen, int32_t oy, int32_t rows)
{
    sky_calls = 0;
    sky_oy = oy;
    sky_rows = rows;
    skyline_draw(screen, SKYLINE_MEMBRANE, oy, rows, sky_rect);
    return sky_calls;
}

TT_TEST(skyline_strips_draw_only_the_fills_that_reach_them)
{
    TT_CHECK_EQ(sky_strip_calls(SKYLINE_PLAY, 78, 60), (uint32_t)SKY_PLAY_N);
    TT_CHECK_EQ(sky_strip_calls(SKYLINE_BOOT, 0, 80), 87u);
    TT_CHECK_EQ(sky_strip_calls(SKYLINE_BOOT, 80, 60), 69u);
    TT_CHECK_EQ(sky_strip_calls(SKYLINE_BOOT, 140, 64), 0u);
    TT_CHECK_EQ(sky_strip_calls(SKYLINE_BOOT, 204, 36), 0u);
}

static void run_skyline_tests(void)
{
    TT_RUN(skyline_strips_draw_only_the_fills_that_reach_them);
    TT_RUN(skyline_play_slot_is_the_mockups_pixels_in_both_themes);
    TT_RUN(skyline_boot_art_is_the_mockups_pixels_in_both_themes);
}
