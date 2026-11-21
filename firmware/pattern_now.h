/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pattern identity (#324), main loop only. Four words (docs/ui/interaction.md, Running, queued and stored
 * patterns):
 *   running pattern  what the sequencer plays now, with its slot and name
 *   queued pattern   the slot chosen to take over at the end of the pass, with its name: "slot B next",
 *                    so a save into its slot changes what takes over, and a delete of it cancels it
 *   stored pattern   what the running pattern's slot holds: "edited" compares the two
 *   takeover         the moment at the end of a pass when the queued pattern becomes the running one
 * Every call first takes in a takeover the interrupt has made since the last call (in no particular
 * call), so none of them depends on another having run first. The handoff to the interrupt is seq's
 * (seq.h: one writer per field); the slot and name stay here, kept with the ask they went with. */
#ifndef FM1_8P_PATTERN_NOW_H
#define FM1_8P_PATTERN_NOW_H

#include <stdint.h>
#include "seq.h"
#include "pattern_store.h"

typedef struct {
    seq *s;
    pat_lib *lib;                       /* the slots, NULL until read (pattern_attach) */
    const store_io *io;
    uint32_t running;                   /* the running pattern's slot (0..PAT_SLOTS-1, 0 from init) */
    char running_name[STORE_NAME_LEN + 1u];
    int32_t queued;                     /* the queued pattern's slot, -1 none */
    char queued_name[STORE_NAME_LEN + 1u];
    uint32_t queued_ask;                /* seq_load_pattern's ask it went with (seq_adopted) */
    uint32_t takeovers;                 /* +1 a takeover: a snapshot's token */
} pattern_now;

void pattern_init(pattern_now *n, seq *s);
/* The slots are read: the running slot's pattern loads (slot 0 from init). */
void pattern_attach(pattern_now *n, pat_lib *lib, const store_io *io);
/* The running pattern's slot, and its name into name (STORE_NAME_LEN + 1 chars, "" for an empty slot)
 * unless name is NULL. */
uint32_t pattern_running(pattern_now *n, char *name);
/* The queued pattern's slot (-1: none), and its name as pattern_running's. */
int32_t pattern_queued(pattern_now *n, char *name);
/* PATTERN EDITED: the running pattern (as edited) differs from its stored pattern. */
uint32_t pattern_edited(pattern_now *n);
/* Slot q (an empty one: the empty pattern). Stopped, it loads at once. Playing, it is queued for the
 * end of the pass, and q the running slot cancels a queued one (interaction.md, Pattern slots). */
void pattern_queue(pattern_now *n, uint32_t q);
/* 1 once for each takeover since the last call (each draw: the screen follows). */
uint32_t pattern_takeover(pattern_now *n);
/* The running pattern as edited, for a save that may wait: returns its token for pattern_save. */
uint32_t pattern_snapshot(pattern_now *n, seq_pattern *snap);
/* snap, named `name`, into slot dest: 0, or -1 when the write failed (nothing changes then).
 * - no takeover since the snapshot (token): dest becomes the running slot, and its stored pattern snap;
 * - a takeover since: the running pattern stays; when dest is its slot, its stored pattern is snap;
 * - dest the queued slot: the queued pattern is snap now, named `name`. */
int pattern_save(pattern_now *n, uint32_t dest, const seq_pattern *snap, const char *name, uint32_t token);
/* Empties slot dest (#303): 0, or -1 when the write failed. The running pattern plays on, edited against
 * the empty pattern when dest is its slot; dest the queued slot cancels the queue. */
int pattern_delete(pattern_now *n, uint32_t dest);

#endif
