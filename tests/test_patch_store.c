/* Patch storage (firmware/patch_store.c): the flash record a saved patch becomes (#3, #48). */
#include "../firmware/patch_store.h"
#include "../firmware/patches.h"

static uint8_t store_rec[STORE_SLOT_BYTES];

/* A factory patch as a store_patch: its name and every engine value. */
static void store_from_factory(uint32_t n, store_patch *p)
{
    memset(p, 0, sizeof *p);
    memcpy(p->name, PATCHES[n].name, strlen(PATCHES[n].name) < STORE_NAME_LEN ? strlen(PATCHES[n].name) : STORE_NAME_LEN);
    patch_values(&PATCHES[n], p->v);
}

/* Every factory patch comes back from its record bit for bit: each value and the name. */
TT_TEST(store_record_round_trips_every_factory_patch)
{
    static store_patch in, out;
    uint32_t n, id;
    int same = 1;
    for (n = 0; n < PATCH_COUNT; n++) {
        store_from_factory(n, &in);
        memset(&out, 0x55, sizeof out);
        same &= store_decode(store_rec, store_encode(&in, store_rec), &out) == 0;
        same &= strcmp(in.name, out.name) == 0;
        for (id = 0; id < FM_PARAM_COUNT; id++)
            same &= in.v[id] == out.v[id];
    }
    TT_CHECK(same);
}

/* A record from older firmware has fewer tags: the params added since load at their engine default,
 * and the rest as saved. (The record's last 4 entries cut off, as if those params were newer.) */
TT_TEST(store_record_from_older_firmware_loads_new_params_at_their_default)
{
    static store_patch in, out;
    uint32_t id, used, cut = 4, newer[4], k;
    int same = 1;
    store_from_factory(0, &in);
    for (id = 0; id < FM_PARAM_COUNT; id++)     /* nothing at its default, so a miss shows */
        in.v[id] = fm_param_info_get((uint16_t)id)->def == fm_param_info_get((uint16_t)id)->min
                       ? fm_param_info_get((uint16_t)id)->max : fm_param_info_get((uint16_t)id)->min;
    used = store_encode(&in, store_rec);
    for (k = 0; k < cut; k++)                   /* the params the cut entries carry */
        newer[k] = store_tag_param(store_rec[used - 3u * (k + 1u)]);
    store_rec[0] = (uint8_t)(used - 3u * cut);
    store_rec[14] = (uint8_t)(store_rec[14] - cut);
    memset(&out, 0x55, sizeof out);
    same &= store_decode(store_rec, used, &out) == 0;
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        int is_new = 0;
        for (k = 0; k < cut; k++) is_new |= newer[k] == id;
        same &= out.v[id] == (is_new ? fm_param_info_get((uint16_t)id)->def : in.v[id]);
    }
    TT_CHECK(same);
}

/* A record from newer firmware has tags this one doesn't know: those entries are skipped and the
 * rest load as saved. (An entry with tag 254 added at the end.) */
TT_TEST(store_record_from_newer_firmware_skips_unknown_params)
{
    static store_patch in, out;
    uint32_t id, used;
    int same = 1;
    store_from_factory(5, &in);
    used = store_encode(&in, store_rec);
    TT_CHECK(store_tag_param(254) == FM_PARAM_COUNT);
    store_rec[used] = 254;
    store_rec[used + 1u] = 0x34;
    store_rec[used + 2u] = 0x12;
    store_rec[0] = (uint8_t)(used + 3u);
    store_rec[14] = (uint8_t)(store_rec[14] + 1u);
    memset(&out, 0x55, sizeof out);
    same &= store_decode(store_rec, used + 3u, &out) == 0;
    for (id = 0; id < FM_PARAM_COUNT; id++)
        same &= out.v[id] == in.v[id];
    TT_CHECK(same);
}

/* An empty or damaged slot is not a patch: erased flash (0xFF), zeros, another version, a length that
 * disagrees with the entry count, or a record longer than the bytes read. */
TT_TEST(store_empty_or_damaged_slot_reads_empty)
{
    static store_patch in, out;
    uint32_t used;
    store_from_factory(3, &in);
    memset(store_rec, 0xFF, sizeof store_rec);
    TT_CHECK_EQ(store_decode(store_rec, sizeof store_rec, &out), -1);
    memset(store_rec, 0, sizeof store_rec);
    TT_CHECK_EQ(store_decode(store_rec, sizeof store_rec, &out), -1);
    used = store_encode(&in, store_rec);
    store_rec[1] = 2;
    TT_CHECK_EQ(store_decode(store_rec, used, &out), -1);
    store_encode(&in, store_rec);
    store_rec[14] = (uint8_t)(store_rec[14] - 1u);
    TT_CHECK_EQ(store_decode(store_rec, used, &out), -1);
    store_encode(&in, store_rec);
    TT_CHECK_EQ(store_decode(store_rec, used - 1u, &out), -1);
    TT_CHECK_EQ(store_decode(store_rec, 10, &out), -1);
    TT_CHECK_EQ(store_decode(store_rec, used, &out), 0);
}

/* A value outside its param's range (a later firmware narrowed it) loads clamped into the range,
 * as the engine would take it: each entry set to 32767, then to -32768. */
TT_TEST(store_value_out_of_range_loads_clamped)
{
    static store_patch in, out;
    uint32_t id, used, e;
    int hi = 1, lo = 1;
    store_from_factory(7, &in);
    used = store_encode(&in, store_rec);
    for (e = 15; e < used; e += 3) { store_rec[e + 1u] = 0xFF; store_rec[e + 2u] = 0x7F; }
    TT_CHECK_EQ(store_decode(store_rec, used, &out), 0);
    for (id = 0; id < FM_PARAM_COUNT; id++) hi &= out.v[id] == fm_param_info_get((uint16_t)id)->max;
    for (e = 15; e < used; e += 3) { store_rec[e + 1u] = 0x00; store_rec[e + 2u] = 0x80; }
    TT_CHECK_EQ(store_decode(store_rec, used, &out), 0);
    for (id = 0; id < FM_PARAM_COUNT; id++) lo &= out.v[id] == fm_param_info_get((uint16_t)id)->min;
    TT_CHECK(hi);
    TT_CHECK(lo);
}

/* The tags never move: a record saved today must load into the same params on every later firmware.
 * Tag n's param, by its engine name, as first released (append new tags at the end only). */
static const char *const STORE_TAG_NAMES[] = {
    "amp_attack_ms", "amp_decay_ms", "amp_sustain_permille", "amp_release_ms",
    "bend_range_semitones", "voice_mode", "unison_detune_cents", "osc1_wave",
    "osc1_pulse_width_pct", "osc1_octave", "osc1_fine_cents", "osc2_wave", "osc2_pulse_width_pct",
    "osc2_octave", "osc2_fine_cents", "osc2_level_permille", "chorus_mode", "lfo_wave",
    "lfo_rate_centihz", "lfo_delay_ms", "lfo_pitch_cents", "amp_velocity_pct", "lfo_pw_pct",
    "osc_sync", "osc_xmod_permille", "filter_cutoff_cents", "filter_resonance_permille",
    "filter_key_track_permille", "filter_env_attack_ms", "filter_env_decay_ms",
    "filter_env_sustain_permille", "filter_env_release_ms", "filter_env_amount_cents",
    "highpass_step", "osc2_coarse_semis", "osc_mix_env_permille", "osc_mix_env_source",
    "filter_env_source", "lfo_pitch_osc2_cents", "pitch_env_osc1_cents", "pitch_env_osc2_cents",
    "pitch_env_source", "pitch_env_dyna", "osc1_level_permille", "osc_ring", "filter_env_dyna",
    "amp_key_follow_permille", "filter_env_key_follow_permille", "amp_dyna", "osc_mix_env_dyna",
    "amp_level_permille", "setting_glide", "amp_gate",
};

TT_TEST(store_tags_keep_their_params)
{
    uint32_t t, n = sizeof STORE_TAG_NAMES / sizeof STORE_TAG_NAMES[0];
    int same = 1;
    for (t = 0; t < n; t++)
        same &= store_tag_param(t) < FM_PARAM_COUNT &&
                strcmp(fm_param_info_get(store_tag_param(t))->name, STORE_TAG_NAMES[t]) == 0;
    TT_CHECK(same);
}

/* Every param has exactly one tag (a new param needs one, or it is never saved), and every range fits
 * the record's int16, so a value saves exactly. */
TT_TEST(store_every_param_has_one_tag_and_fits_int16)
{
    uint32_t id, t, hits;
    int ok = 1;
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *in = fm_param_info_get((uint16_t)id);
        for (hits = 0, t = 0; t < 256u; t++) hits += store_tag_param(t) == id;
        ok &= hits == 1 && in->min >= -32768 && in->max <= 32767;
    }
    TT_CHECK(ok);
}

/* A RAM stand-in for fm1-dock's banks: up to 4, each as saved (len -1: not there); `fail` makes a save
 * fail. */
typedef struct {
    uint8_t data[4][8176];
    int32_t len[4];
    int fail;
    uint32_t saves;
} store_fake;

static int store_fake_load(void *ctx, uint32_t idx, void *p, uint32_t n)
{
    store_fake *f = ctx;
    if (idx >= 4 || f->len[idx] < 0) return -1;
    memcpy(p, f->data[idx], n < (uint32_t)f->len[idx] ? n : (uint32_t)f->len[idx]);
    return f->len[idx];
}

static int store_fake_save(void *ctx, uint32_t idx, const void *p, uint32_t n)
{
    store_fake *f = ctx;
    if (idx >= 4 || f->fail || n > 8176) return -1;
    memcpy(f->data[idx], p, n);
    f->len[idx] = (int32_t)n;
    f->saves++;
    return 0;
}

static store_fake store_flash;
static store_lib store_lib_a, store_lib_b;
static const store_io store_fake_io = { store_fake_load, store_fake_save, &store_flash };

static void store_flash_blank(void)
{
    uint32_t i;
    memset(&store_flash, 0xFF, sizeof store_flash);
    for (i = 0; i < 4; i++) store_flash.len[i] = -1;
    store_flash.fail = 0;
    store_flash.saves = 0;
}

/* A patch saved to a user slot is there after a power cycle: a fresh library loads it from flash. */
TT_TEST(store_slot_survives_a_power_cycle)
{
    static store_patch in, out;
    uint32_t s, id;
    int same = 1;
    store_flash_blank();
    store_lib_load(&store_lib_a, &store_fake_io);
    for (s = 0; s < STORE_USER_SLOTS; s += 21) {   /* 0, 21, 42, 63: both banks */
        store_from_factory(s % PATCH_COUNT, &in);
        same &= store_slot_write(&store_lib_a, &store_fake_io, s, &in) == 0;
    }
    store_lib_load(&store_lib_b, &store_fake_io);
    for (s = 0; s < STORE_USER_SLOTS; s += 21) {
        store_from_factory(s % PATCH_COUNT, &in);
        same &= store_slot_read(&store_lib_b, s, &out) == 0 && strcmp(out.name, in.name) == 0;
        for (id = 0; id < FM_PARAM_COUNT; id++) same &= out.v[id] == in.v[id];
    }
    TT_CHECK(same);
}

/* A unit that never saved has no banks: every slot reads empty, whatever was in RAM before. */
TT_TEST(store_slots_start_empty)
{
    static store_patch in, out;
    uint32_t s;
    int empty = 1;
    store_flash_blank();
    store_from_factory(1, &in);
    memset(&store_lib_b, 0, sizeof store_lib_b);
    for (s = 0; s < STORE_USER_SLOTS; s++)     /* stale records in RAM */
        (void)store_encode(&in, store_lib_b.img[s / STORE_BANK_SLOTS] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS));
    store_lib_load(&store_lib_b, &store_fake_io);
    for (s = 0; s < STORE_USER_SLOTS; s++) empty &= store_slot_read(&store_lib_b, s, &out) == -1;
    TT_CHECK(empty);
}

/* A save that fails (no free bank, a flash error) says so, and the slot stays as it was: what RAM
 * shows is what flash holds. */
TT_TEST(store_failed_write_leaves_the_slot_as_it_was)
{
    static store_patch a, b, out;
    store_flash_blank();
    store_lib_load(&store_lib_a, &store_fake_io);
    store_from_factory(2, &a);
    store_from_factory(9, &b);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 40, &a), 0);
    store_flash.fail = 1;
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 40, &b), -1);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 41, &b), -1);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 40, &out), 0);
    TT_CHECK(strcmp(out.name, a.name) == 0);
    TT_CHECK_EQ(store_slot_read(&store_lib_a, 41, &out), -1);
}

/* PRESETS goes through the user slots that hold a patch, in slot order, then P1..P32, then wraps;
 * empty slots are skipped (Eric, 2026-11-13), so a fresh unit browses P1..P32 alone. */
TT_TEST(store_presets_ring_skips_empty_slots)
{
    static store_patch in;
    store_flash_blank();
    store_lib_load(&store_lib_a, &store_fake_io);
    (void)play_init(&ui_p);
    ui_p.lib = &store_lib_a;
    ui_init(&ui_u, &ui_p);
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + PATCH_COUNT - 1u, 1), PLAY_PROG_P);    /* P32 -> P1 */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P, -1), PLAY_PROG_P + PATCH_COUNT - 1u);    /* P1 -> P32 */
    store_from_factory(4, &in);
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 9, &in), 0);               /* slot 22 */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + PATCH_COUNT - 1u, 1), 9u);             /* P32 -> 22 */
    TT_CHECK_EQ(play_prog_step(&ui_p, 9, 1), PLAY_PROG_P);                                 /* 22 -> P1 */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P, -1), 9u);                               /* P1 -> 22 */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + 2u, 4), PLAY_PROG_P + 6u);             /* +4: P3 -> P7 */
    TT_CHECK_EQ(play_prog_step(&ui_p, PLAY_PROG_P + 30u, 4), PLAY_PROG_P + 1u);            /* P31 -> P32, 22, P1, P2 */
}

/* A PRESETS click onto a saved slot loads it: the engine gets the slot's values, the program and
 * name follow, and it is not edited. Back to P1 the factory patch loads as before. */
TT_TEST(store_presets_loads_a_user_slot)
{
    static store_patch in;
    char name[STORE_NAME_LEN + 1u];
    uint32_t id, k;
    int same = 1;
    store_flash_blank();
    store_lib_load(&store_lib_a, &store_fake_io);
    (void)play_init(&ui_p);
    ui_p.lib = &store_lib_a;
    ui_init(&ui_u, &ui_p);
    store_from_factory(4, &in);
    memcpy(in.name, "MY PAD", 7);
    in.v[FM_PARAM_FILTER_RESONANCE] = 612;
    TT_CHECK_EQ(store_slot_write(&store_lib_a, &store_fake_io, 9, &in), 0);
    TT_CHECK_EQ(play_load_prog(&ui_p, PLAY_PROG_P + PATCH_COUNT - 1u), 0);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 100);
    TT_CHECK_EQ(play_prog(&ui_p), 9u);
    play_prog_name(&ui_p, name);
    TT_CHECK(strcmp(name, "MY PAD") == 0);
    TT_CHECK(!play_edited(&ui_p));
    for (k = 0; k < 64; k++) { memset(ui_out, 0, sizeof ui_out); play_render(&ui_p, ui_out, PLAY_BLOCK); }
    for (id = 0; id < FM_PARAM_COUNT; id++) same &= fm_engine_get_param(&ui_p.engine, (uint16_t)id) == in.v[id];
    TT_CHECK(same);
    ui_event(&ui_u, UI_EV_ENC | UI_ENC_PRESETS, 1, 200);
    TT_CHECK_EQ(play_prog(&ui_p), PLAY_PROG_P);
    TT_CHECK(play_patch(&ui_p) == &PATCHES[0]);
    play_prog_name(&ui_p, name);
    TT_CHECK(strcmp(name, PATCHES[0].name) == 0);
}

/* A program's number as the screen shows it: user slots 11..88 (two digits 1..8), P1.. after. */
TT_TEST(store_program_numbers_read_like_the_panel)
{
    char s[8];
    ui_prog_number(0, s);   TT_CHECK(strcmp(s, "11") == 0);
    ui_prog_number(7, s);   TT_CHECK(strcmp(s, "18") == 0);
    ui_prog_number(9, s);   TT_CHECK(strcmp(s, "22") == 0);
    ui_prog_number(10, s);  TT_CHECK(strcmp(s, "23") == 0);
    ui_prog_number(63, s);  TT_CHECK(strcmp(s, "88") == 0);
    ui_prog_number(PLAY_PROG_P, s);       TT_CHECK(strcmp(s, "P1") == 0);
    ui_prog_number(PLAY_PROG_P + 31u, s); TT_CHECK(strcmp(s, "P32") == 0);
}

static void run_patch_store_tests(void)
{
    TT_RUN(store_record_round_trips_every_factory_patch);
    TT_RUN(store_record_from_older_firmware_loads_new_params_at_their_default);
    TT_RUN(store_record_from_newer_firmware_skips_unknown_params);
    TT_RUN(store_empty_or_damaged_slot_reads_empty);
    TT_RUN(store_value_out_of_range_loads_clamped);
    TT_RUN(store_tags_keep_their_params);
    TT_RUN(store_every_param_has_one_tag_and_fits_int16);
    TT_RUN(store_slot_survives_a_power_cycle);
    TT_RUN(store_slots_start_empty);
    TT_RUN(store_failed_write_leaves_the_slot_as_it_was);
    TT_RUN(store_presets_ring_skips_empty_slots);
    TT_RUN(store_presets_loads_a_user_slot);
    TT_RUN(store_program_numbers_read_like_the_panel);
}
