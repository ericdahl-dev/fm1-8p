/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p settings storage (settings_store.h): GLO's settings as (tag, value) pairs in the FM-1's
 * per-module settings record. docs/patches.md, Settings. */
#include "settings_store.h"

#define VERSION 1u
#define HEADER 3u                               /* length, version, entry count */
#define ENTRY 2u                                /* tag, value */

/* Tags name settings for good: append only, never reorder or reuse (settings_tags_keep_their_fields
 * holds each to its field). Tag 0 is none. */
#define T(f) (uint8_t)offsetof(ui_settings, f)
static const uint8_t TAG_OFFSET[] = {
    SET_NO_TAG,
    T(midi_in), T(midi_out), T(clock_src), T(clock_out), T(cc_recv), T(cc_send),   /* 1..6 */
    T(prog_change), T(ext_cc), T(vel_curve_pct), T(fixed_vel), T(key_leds), T(theme),   /* 7..12 */
};
#define TAGS (uint32_t)(sizeof TAG_OFFSET / sizeof TAG_OFFSET[0])
_Static_assert(TAGS - 1u == UI_SETTINGS_COUNT, "every setting has a tag");
_Static_assert(HEADER + ENTRY * UI_SETTINGS_COUNT <= SET_REC_BYTES, "the settings fit the record");

uint8_t settings_tag_offset(uint32_t tag)
{
    return tag < TAGS ? TAG_OFFSET[tag] : (uint8_t)SET_NO_TAG;
}

void settings_encode(const ui_settings *s, uint8_t *rec)
{
    uint32_t i, n = HEADER;
    for (i = 0; i < SET_REC_BYTES; i++)
        rec[i] = 0;
    for (i = 1; i < TAGS; i++) {
        rec[n] = (uint8_t)i;
        rec[n + 1u] = ((const uint8_t *)s)[TAG_OFFSET[i]];
        n += ENTRY;
    }
    rec[0] = (uint8_t)n;
    rec[1] = VERSION;
    rec[2] = (uint8_t)(TAGS - 1u);
}

void settings_decode(const uint8_t *rec, ui_settings *s)
{
    uint32_t i, k, count = rec[2];
    ui_settings_default(s);
    if (rec[1] != VERSION || rec[0] != HEADER + ENTRY * count || rec[0] > SET_REC_BYTES)
        return;                                 /* none, or damaged: the defaults */
    for (i = 0; i < count; i++) {
        const uint8_t *e = rec + HEADER + ENTRY * i;
        uint8_t off = settings_tag_offset(e[0]);
        if (off == SET_NO_TAG)
            continue;                           /* a newer firmware's setting */
        for (k = 0; k < UI_SETTINGS_COUNT; k++)
            if (UI_SETTINGS_INFO[k].offset == off) {
                const ui_setting_info *in = &UI_SETTINGS_INFO[k];
                ((uint8_t *)s)[off] = e[1] < in->min ? in->min : e[1] > in->max ? in->max : e[1];
            }
    }
}

void settings_load(ui_settings *s, const settings_io *io)
{
    uint8_t rec[SET_REC_BYTES];
    if (!io || io->load(io->ctx, rec, SET_REC_BYTES) != 0) {
        ui_settings_default(s);
        return;
    }
    settings_decode(rec, s);
}

void settings_save(const ui_settings *s, const settings_io *io)
{
    uint8_t rec[SET_REC_BYTES];
    settings_encode(s, rec);
    if (io)
        io->save(io->ctx, rec, SET_REC_BYTES);
}

uint32_t settings_tick(const ui_settings *s, uint32_t *dirty, uint32_t changed_ms, uint32_t ms,
                       uint32_t silent, const settings_io *io)
{
    if (!*dirty)
        return 0;
    if (!io) {
        *dirty = 0;                             /* no record to keep it in */
        return 0;
    }
    if (ms - changed_ms < SET_WAIT_MS || !silent)
        return 0;
    settings_save(s, io);
    *dirty = 0;
    return 1;
}
