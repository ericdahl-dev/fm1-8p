/* The settings store (firmware/settings_store.c): GLO's settings as the FM-1's per-module settings
 * record, tag-keyed, clamped on load (interaction.md 4, GLO; docs/patches.md, Settings). */
#include "../firmware/settings_store.h"

/* the host's record, as fm1-dock keeps it: one, loaded only at the length it was saved */
static uint8_t set_host[SET_REC_BYTES + 8u];
static uint32_t set_host_len, set_host_saves;

static int set_fake_load(void *ctx, void *p, uint32_t n)
{
    uint32_t i;
    (void)ctx;
    if (!set_host_len || set_host_len != n)
        return -1;
    for (i = 0; i < n; i++)
        ((uint8_t *)p)[i] = set_host[i];
    return 0;
}

static void set_fake_save(void *ctx, const void *p, uint32_t n)
{
    uint32_t i;
    (void)ctx;
    for (i = 0; i < n && i < sizeof set_host; i++)
        set_host[i] = ((const uint8_t *)p)[i];
    set_host_len = n;
    set_host_saves++;
}

static const settings_io set_fake_io = { set_fake_load, set_fake_save, 0 };

static void set_host_blank(void)
{
    uint32_t i;
    for (i = 0; i < sizeof set_host; i++)
        set_host[i] = 0;
    set_host_len = 0;
    set_host_saves = 0;
}

/* a record by hand: version 1, then (tag, value) pairs */
static void set_host_record(const uint8_t *pairs, uint32_t n)
{
    uint32_t i;
    set_host_blank();
    set_host[0] = (uint8_t)(3u + 2u * n);
    set_host[1] = 1;
    set_host[2] = (uint8_t)n;
    for (i = 0; i < 2u * n; i++)
        set_host[3u + i] = pairs[i];
    set_host_len = SET_REC_BYTES;
}

static uint32_t set_same(const ui_settings *a, const ui_settings *b)
{
    uint32_t i, same = 1;
    for (i = 0; i < sizeof *a; i++)
        same &= ((const uint8_t *)a)[i] == ((const uint8_t *)b)[i];
    return same;
}

/* Tags name settings for good: this list is the frozen record, as STORE_TAG_NAMES is for patches. */
TT_TEST(settings_tags_keep_their_fields)
{
    static const struct { uint32_t tag; uint8_t offset; } FROZEN[] = {
        {1, offsetof(ui_settings, midi_in)},       {2, offsetof(ui_settings, midi_out)},
        {3, offsetof(ui_settings, clock_src)},     {4, offsetof(ui_settings, clock_out)},
        {5, offsetof(ui_settings, cc_recv)},       {6, offsetof(ui_settings, cc_send)},
        {7, offsetof(ui_settings, prog_change)},   {8, offsetof(ui_settings, ext_cc)},
        {9, offsetof(ui_settings, vel_curve_pct)}, {10, offsetof(ui_settings, fixed_vel)},
        {11, offsetof(ui_settings, key_leds)},     {12, offsetof(ui_settings, theme)},
    };
    uint32_t i;
    TT_CHECK_EQ(sizeof FROZEN / sizeof FROZEN[0], UI_SETTINGS_COUNT);   /* every setting has a tag */
    for (i = 0; i < sizeof FROZEN / sizeof FROZEN[0]; i++)
        TT_CHECK_EQ(settings_tag_offset(FROZEN[i].tag), FROZEN[i].offset);
    TT_CHECK_EQ(settings_tag_offset(0), SET_NO_TAG);
    TT_CHECK_EQ(settings_tag_offset(13), SET_NO_TAG);
}

/* Saved and loaded back, every setting is as it was; the record is always SET_REC_BYTES long (the
 * host loads a record only at the length it was saved). */
TT_TEST(settings_round_trip)
{
    ui_settings a, b;
    set_host_blank();
    ui_settings_default(&a);
    a.midi_in = 16;
    a.midi_out = 3;
    a.prog_change = 3;
    a.vel_curve_pct = 200;
    a.fixed_vel = 1;
    a.theme = UI_THEME_NIGHT;
    settings_save(&a, &set_fake_io);
    TT_CHECK_EQ(set_host_len, SET_REC_BYTES);
    TT_CHECK_EQ(set_host[1], 1u);              /* version 1 */
    TT_CHECK_EQ(set_host[2], UI_SETTINGS_COUNT);
    ui_settings_default(&b);
    settings_load(&b, &set_fake_io);
    TT_CHECK(set_same(&a, &b));
}

/* No record (a fresh FM-1, or a host without one): the defaults. */
TT_TEST(settings_no_record_loads_the_defaults)
{
    ui_settings s, d;
    set_host_blank();
    ui_settings_default(&d);
    s = d;
    s.theme = UI_THEME_NIGHT;                   /* (not what load leaves) */
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
}

/* An older record, without some tags: those settings load at their defaults. */
TT_TEST(settings_missing_tags_load_their_defaults)
{
    static const uint8_t pairs[] = {12, UI_THEME_NIGHT, 2, 9};
    ui_settings s, d;
    set_host_record(pairs, 2);
    ui_settings_default(&d);
    d.theme = UI_THEME_NIGHT;
    d.midi_out = 9;
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
}

/* A newer record, with tags this firmware doesn't know: those entries are skipped, the rest load. */
TT_TEST(settings_unknown_tags_are_skipped)
{
    static const uint8_t pairs[] = {200, 7, 1, 5, 0, 3, 13, 1, 10, 64};
    ui_settings s, d;
    set_host_record(pairs, 5);
    ui_settings_default(&d);
    d.midi_in = 5;
    d.fixed_vel = 64;
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
}

/* A value outside its setting's range loads clamped into it (UI_SETTINGS_INFO). */
TT_TEST(settings_out_of_range_values_load_clamped)
{
    static const uint8_t pairs[] = {1, 17, 2, 0, 9, 20, 9 + 1, 0, 12, 9};
    ui_settings s;
    set_host_record(pairs, 5);
    settings_load(&s, &set_fake_io);
    TT_CHECK_EQ(s.midi_in, 16u);
    TT_CHECK_EQ(s.midi_out, 1u);
    TT_CHECK_EQ(s.vel_curve_pct, 50u);
    TT_CHECK_EQ(s.fixed_vel, 1u);
    TT_CHECK_EQ(s.theme, (uint32_t)UI_THEME_NIGHT);
}

/* A damaged record (another version, a length that disagrees with the count, a count past the
 * record) is no record: the defaults. */
TT_TEST(settings_damaged_record_loads_the_defaults)
{
    static const uint8_t pairs[] = {12, UI_THEME_NIGHT};
    ui_settings s, d;
    ui_settings_default(&d);
    set_host_record(pairs, 1);
    set_host[1] = 2;                            /* version 2 */
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
    set_host_record(pairs, 1);
    set_host[0] = 7;                            /* length says 2 entries, count says 1 */
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
    set_host_record(pairs, 1);
    set_host[0] = 3u + 2u * 31u;                /* past SET_REC_BYTES */
    set_host[2] = 31;
    settings_load(&s, &set_fake_io);
    TT_CHECK(set_same(&s, &d));
}

/* The write rule: nothing while a change is under 2 s old or anything sounds; then one save, and
 * dirty clears. Not dirty: never a save. */
TT_TEST(settings_tick_saves_once_silent_and_settled)
{
    ui_settings s;
    uint32_t dirty = 1, changed = 1000;
    set_host_blank();
    ui_settings_default(&s);
    s.theme = UI_THEME_NIGHT;
    TT_CHECK_EQ(settings_tick(&s, &dirty, changed, 2999, 1, &set_fake_io), 0u);   /* 1.999 s */
    TT_CHECK_EQ(settings_tick(&s, &dirty, changed, 9000, 0, &set_fake_io), 0u);   /* sounding */
    TT_CHECK_EQ(set_host_saves, 0u);
    TT_CHECK_EQ(dirty, 1u);
    TT_CHECK_EQ(settings_tick(&s, &dirty, changed, 3000, 1, &set_fake_io), 1u);
    TT_CHECK_EQ(set_host_saves, 1u);
    TT_CHECK_EQ(dirty, 0u);
    TT_CHECK_EQ(settings_tick(&s, &dirty, changed, 9000, 1, &set_fake_io), 0u);
    TT_CHECK_EQ(set_host_saves, 1u);
}

/* No host record (fm1-dock before it, or the desktop): load gives the defaults and a tick drops the
 * save quietly. */
TT_TEST(settings_without_io_keeps_the_defaults)
{
    ui_settings s, d;
    uint32_t dirty = 1;
    ui_settings_default(&d);
    settings_load(&s, 0);
    TT_CHECK(set_same(&s, &d));
    TT_CHECK_EQ(settings_tick(&s, &dirty, 0, 5000, 1, 0), 0u);
    TT_CHECK_EQ(dirty, 0u);
}

/* GLO and the store together, as fm1_8p.c's draw() runs them: THEME turned to NIGHT on its page is
 * saved once settled and silent, and a fresh unit loads it back. */
TT_TEST(settings_glo_change_saves_and_loads_back)
{
    uint32_t ms = 1000;
    set_host_blank();
    (void)play_init(&ui_p);
    ui_init(&ui_u, &ui_p);
    settings_load(&ui_u.set, &set_fake_io);     /* the first draw: no record, the defaults */
    ui_event(&ui_u, UI_EV_BTN_DOWN | UI_BTN_GLO, 0, ms);
    ui_event(&ui_u, UI_EV_BTN_UP | UI_BTN_GLO, 0, ms + 50u);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_SELECT, 2, ms + 100u);   /* G31..G34 */
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_KNOB4, 1, ms + 200u);    /* G34 THEME: NIGHT PANEL */
    TT_CHECK_EQ(ui_u.set.theme, (uint32_t)UI_THEME_NIGHT);
    TT_CHECK_EQ(ui_u.set_dirty, 1u);
    TT_CHECK_EQ(settings_tick(&ui_u.set, &ui_u.set_dirty, ui_u.set_changed_ms, ms + 1000u,
                              play_silent(&ui_p), &set_fake_io), 0u);
    TT_CHECK_EQ(settings_tick(&ui_u.set, &ui_u.set_dirty, ui_u.set_changed_ms, ms + 2200u,
                              play_silent(&ui_p), &set_fake_io), 1u);
    (void)play_init(&ui_p);                     /* power off and on */
    ui_init(&ui_u, &ui_p);
    TT_CHECK_EQ(ui_u.set.theme, (uint32_t)UI_THEME_MEMBRANE);
    settings_load(&ui_u.set, &set_fake_io);
    TT_CHECK_EQ(ui_u.set.theme, (uint32_t)UI_THEME_NIGHT);
}

static void run_settings_store_tests(void)
{
    TT_RUN(settings_tags_keep_their_fields);
    TT_RUN(settings_round_trip);
    TT_RUN(settings_no_record_loads_the_defaults);
    TT_RUN(settings_missing_tags_load_their_defaults);
    TT_RUN(settings_unknown_tags_are_skipped);
    TT_RUN(settings_out_of_range_values_load_clamped);
    TT_RUN(settings_damaged_record_loads_the_defaults);
    TT_RUN(settings_tick_saves_once_silent_and_settled);
    TT_RUN(settings_without_io_keeps_the_defaults);
    TT_RUN(settings_glo_change_saves_and_loads_back);
}
