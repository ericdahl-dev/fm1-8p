/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p settings storage: GLO's settings (ui_settings.h) as the FM-1's per-module settings record
 * (fm1-dock's load / save: up to 240 bytes, which the host keeps in RAM and writes to flash itself,
 * 2 s after the last change with no key held and no note in). Plain C, no host code: the module glue
 * passes the host's load and save, and the desktop tests a fake. docs/patches.md, Settings. */
#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H
#include <stdint.h>
#include "ui_settings.h"

/* The record is always this long: the host loads a record only at the length it was saved, so a
 * new setting takes a (tag, value) pair of the room left, never a longer record. */
#define SET_REC_BYTES 64u
#define SET_NO_TAG 0xFFu                /* settings_tag_offset: a tag this firmware doesn't know */
#define SET_WAIT_MS 2000u               /* a change saves this long after it, once nothing sounds */

/* the host's record: load 0 when loaded (n bytes as saved), -1 none; save keeps n bytes */
typedef struct {
    int (*load)(void *ctx, void *p, uint32_t n);
    void (*save)(void *ctx, const void *p, uint32_t n);
    void *ctx;
} settings_io;

/* The setting a tag names, as its offset in ui_settings, or SET_NO_TAG. Tags are frozen. */
uint8_t settings_tag_offset(uint32_t tag);
/* s into rec (SET_REC_BYTES) and back: decode starts from the defaults, skips tags it doesn't know
 * and clamps each value into its range; a damaged record leaves the defaults. */
void settings_encode(const ui_settings *s, uint8_t *rec);
void settings_decode(const uint8_t *rec, ui_settings *s);
/* At the first draw: the host's record into s (none, or io 0: the defaults). */
void settings_load(ui_settings *s, const settings_io *io);
/* s into the host's record now. */
void settings_save(const ui_settings *s, const settings_io *io);
/* Each draw: while *dirty, once SET_WAIT_MS have passed since changed_ms and silent, s is saved and
 * *dirty clears (io 0: it just clears). 1 when it saved. */
uint32_t settings_tick(const ui_settings *s, uint32_t *dirty, uint32_t changed_ms, uint32_t ms,
                       uint32_t silent, const settings_io *io);
#endif
